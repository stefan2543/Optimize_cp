#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <aio.h>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <getopt.h>
#include <sys/mman.h>
#include <thread>
#include <vector>
#include <mutex>
#include <semaphore.h>
#include <fstream>
char* SOURCE_DIR = nullptr;
char* DESTINATION_DIR = nullptr;
std::mutex threads_mutex;
sem_t sem;
int parse_commandline(int argc, char *argv[]){
  int opt;
     while ((opt = getopt(argc, argv, "s:d:")) != -1) {
    
        switch (opt) {
            case 's':
                SOURCE_DIR = strdup(optarg);
                // std::cout<<REMOTE_SERVER_NAME<<std::endl;
                break;
            case 'd':
                DESTINATION_DIR = strdup(optarg);
                // std::cout<<REMOTE_USERNAME<<std::endl;
                break;

            default:
                std::cerr << "Usage: " << argv[0] << " [-s SOURCE_DIR] [-d DESTINATION_DIR]" << std::endl;
                return 1;
        }
    }
    return 0;
}
void async_copy(off_t file_size, int src_fd, int dest_fd) {
    // Allocate buffer
    char* buf = new char[file_size];

    // Submit asynchronous read operation
    struct aiocb cb;
    memset(&cb, 0, sizeof(cb));
    cb.aio_fildes = src_fd;
    cb.aio_buf = static_cast<void*>(buf);
    cb.aio_nbytes = file_size;
    cb.aio_offset = 0;
    if (aio_read(&cb) == -1) {
        perror("Error submitting read operation: ");
        delete[] buf;
        return;
    }

    // Wait for read operation to complete
    while (aio_error(&cb) == EINPROGRESS) {
        // Perform other tasks here while waiting
    }
    if (aio_error(&cb) != 0) {
        perror("Error completing read operation: ");
        delete[] buf;
        return;
    }

    // Submit asynchronous write operation
    memset(&cb, 0, sizeof(cb));
    cb.aio_fildes = dest_fd;
    cb.aio_buf = static_cast<void*>(buf);
    cb.aio_nbytes = file_size;
    cb.aio_offset = 0;
    if (aio_write(&cb) == -1) {
        perror("Error submitting write operation: ");
        delete[] buf;

        return;
    }

    // Wait for write operation to complete
    while (aio_error(&cb) == EINPROGRESS) {
        // Perform other tasks here while waiting
    }
    if (aio_error(&cb) != 0) {
        perror("Error completing write operation: ");
        delete[] buf;

        return;
    }

    // Free buffer
    delete[] buf;

}
void sync_copy(off_t file_size, int src_fd, int dest_fd) {
    // Memory-map the source file
    void* src_map = mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, src_fd, 0);
    if (src_map == MAP_FAILED) {
        std::cerr << "Failed to memory-map source file (FD: " << src_fd << ", size: " << file_size << "): " << strerror(errno) << std::endl;
        return;
    }

    // Memory-map the destination file
    void* dest_map = mmap(NULL, file_size, PROT_WRITE, MAP_SHARED, dest_fd, 0);
    if (dest_map == MAP_FAILED) {
        std::cerr << "Failed to memory-map destination file (FD: " << dest_fd << ", size: " << file_size << "): " << strerror(errno) << std::endl;
        munmap(src_map, file_size);
        return;
    }

    // Copy data between the memory-mapped files
    memcpy(dest_map, src_map, file_size);

    // Clean up resources
    munmap(src_map, file_size);
    munmap(dest_map, file_size);
}
void copy_file(const char* src_path, const char* dest_path) {
    // Open the source file for reading
    int src_fd = open(src_path, O_RDONLY);
    if (src_fd == -1) {
        std::cerr << "Failed to open source file: " << src_path << std::endl;
        return;
    }
    // Open the destination file for writing
    int dest_fd = open(dest_path, O_WRONLY | O_CREAT | O_TRUNC, 0777);
    if (dest_fd == -1) {
        std::cerr << "Failed to open destination file: " << dest_path << std::endl;
        close(src_fd); // Close the source file descriptor before returning
        return;
    }

    // Get the size of the source file
    struct stat st;
    if (fstat(src_fd, &st) == -1) {
        // handle error
      perror("fstat");
        close(src_fd); // Close the source file descriptor before returning
        close(dest_fd); // Close the destination file descriptor before returning
        return ;
    }
    off_t file_size = st.st_size;

    // Copy the contents of the source file to the destination file
    if (file_size <= 512) {
        // Use synchronous copy for small files
        sync_copy(file_size, src_fd, dest_fd);
    } else {
        // Use asynchronous copy for large files
        async_copy(file_size, src_fd,dest_fd);
    }

    // Close the files
    close(src_fd);
    close(dest_fd);
}
void copy_dir_worker(const char* src_dir, const char* dest_dir, sem_t& sem) {
    std::cout << "Copying directory " << src_dir << " to " << dest_dir << "..." << std::endl;

    DIR* dir = opendir(src_dir);
    if (dir == NULL) {
        perror((std::string("Failed to open source directory: ") + src_dir).c_str());
        return;
    }

    if (mkdir(dest_dir, S_IRWXU | S_IRWXG | S_IRWXO) != 0 && errno != EEXIST) {
        perror((std::string("Failed to create destination directory: ") + dest_dir).c_str());
        closedir(dir);
        return;
    }

    // Create all subdirectories first
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        std::string src_path = std::string(src_dir) + "/" + entry->d_name;
        std::string dest_path = std::string(dest_dir) + "/" + entry->d_name;

        struct stat st;
        if (stat(src_path.c_str(), &st) != 0) {
            perror((std::string("Failed to stat: ") + src_path).c_str());
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            copy_dir_worker(src_path.c_str(), dest_path.c_str(), sem);
        } else if (S_ISREG(st.st_mode)) {
            // Do nothing
        } else {
            std::cerr << "Warning: Skipping unsupported file type: " << src_path << std::endl;
        }
    }

    closedir(dir);

    // Copy all files in the current directory
    dir = opendir(src_dir);
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        std::string src_path = std::string(src_dir) + "/" + entry->d_name;
        std::string dest_path = std::string(dest_dir) + "/" + entry->d_name;

        struct stat st;
        if (stat(src_path.c_str(), &st) != 0) {
            perror((std::string("Failed to stat: ") + src_path).c_str());
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            // Do nothing
        } else if (S_ISREG(st.st_mode)) {
            std::cout << "Copying file " << src_path << " to " << dest_path << "..." << std::endl;

            // Acquire semaphore before copying the file
            sem_wait(&sem);

            // Copy the file in the current thread
            copy_file(src_path.c_str(), dest_path.c_str());

            // Release semaphore after copying the file
            sem_post(&sem);
        } else {
            std::cerr << "Warning: Skipping unsupported file type: " << src_path << std::endl;
        }
    }

    closedir(dir);
}
void copy_dir(const char* src_dir, const char* dest_dir) {
    std::cout << "Copying directory " << src_dir << " to " << dest_dir << "..." << std::endl;

    sem_t sem;
    sem_init(&sem, 0, 5);

    std::thread t(copy_dir_worker, src_dir, dest_dir, std::ref(sem));
    t.join();

    std::cout << "Finished copying directory " << src_dir << " to " << dest_dir << "." << std::endl;

    sem_destroy(&sem);
}
int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <source path> <destination path>" << std::endl;
        return 1;
    }
    parse_commandline(argc,argv);
    struct stat src_stat;
    if (stat(SOURCE_DIR, &src_stat) != 0) {
        std::cerr << "Failed to get source file/directory info" << std::endl;
        return 1;
    }
    // std::cout<<"1"<<std::endl;
    if (S_ISREG(src_stat.st_mode)) {
      // std::cout<<"2"<<std::endl;
        copy_file(SOURCE_DIR, DESTINATION_DIR);
    } else if (S_ISDIR(src_stat.st_mode)) {
      // std::cout<<"3"<<std::endl;
        copy_dir(SOURCE_DIR, DESTINATION_DIR);
    } else {
        std::cerr << "Source path is not a regular file or directory" << std::endl;
        return 1;
    }

    return 0;
}