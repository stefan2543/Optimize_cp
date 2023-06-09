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
#include <cstdlib>
#include <filesystem>
namespace fs = std::filesystem;
char* SOURCE_DIR = nullptr;
char* DESTINATION_DIR = nullptr;
std::mutex threads_mutex;
// sem_t sem;

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
int count_open_files() {
  int count = 0;
  for (const auto& entry : fs::directory_iterator("/proc/self/fd")) {
    if (fs::is_symlink(entry)) {
      count++;
  }
}
return count;
}
void async_copy(size_t file_size, int src_fd, int dest_fd) {
    const size_t buffer_size = 4096;
    const int num_buffers = 4;

    size_t bytes_remaining = file_size;
    size_t bytes_processed = 0;
    std::vector<aiocb> control_blocks(num_buffers);
    std::vector<char*> buffers(num_buffers, nullptr);
    int active_operations = 0;

    for (int i = 0; i < num_buffers; ++i) {
        buffers[i] = new char[buffer_size];
    }

    int current_buf = 0;
    while (active_operations > 0 || bytes_remaining > 0 || bytes_processed < file_size) {
        std::cout<<"stuck?"<<std::endl;
        aiocb *cb = &control_blocks[current_buf];
        char *buf = buffers[current_buf];

        // Check if the current buffer is ready for reuse
        int err = aio_error(cb);
        if (err != ECANCELED && err != EINVAL) {
            if (err == EINPROGRESS) {
                // Buffer still in use, move to the next one
                current_buf = (current_buf + 1) % num_buffers;
                continue;
            }

            ssize_t result = aio_return(cb);
            if (result == -1) {
                perror("Error completing operation: ");
                break;
            }

            if (cb->aio_fildes == src_fd) {
                // Read completed, submit write operation
                memset(cb, 0, sizeof(aiocb));
                cb->aio_fildes = dest_fd;
                cb->aio_buf = static_cast<void*>(buf);
                cb->aio_nbytes = result;
                cb->aio_offset = file_size - bytes_remaining - result;
                if (aio_write(cb) == -1) {
                    perror("Error submitting write operation: ");
                    break;
                }
            } else {
                // Write completed, increment the processed bytes
                bytes_processed += result;
                active_operations--;
            }
        }

        // Submit read operation if there are bytes remaining
        if (bytes_remaining > 0) {
            size_t current_chunk_size = std::min(buffer_size, bytes_remaining);
            bytes_remaining -= current_chunk_size;

            memset(cb, 0, sizeof(aiocb));
            cb->aio_fildes = src_fd;
            cb->aio_buf = static_cast<void*>(buf);
            cb->aio_nbytes = current_chunk_size;
            cb->aio_offset = file_size - bytes_remaining - current_chunk_size;
            if (aio_read(cb) == -1) {
                perror("Error submitting read operation: ");
                break;
            }
            active_operations++;
        }

        current_buf = (current_buf + 1) % num_buffers;
    }

    for (int i = 0; i < num_buffers; ++i) {
        delete[] buffers[i];
    }
}
void sync_copy(off_t file_size, int src_fd, int dest_fd) {
    // Memory-map the source file
    void* src_map = mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, src_fd, 0);
    if (src_map == MAP_FAILED) {
        std::cerr << "Failed to memory-map source file (FD: " << src_fd << ", size: " << file_size << "): " << strerror(errno) << std::endl;
        close(src_fd);
        close(dest_fd);
        return;
    }
    //std::cout << "SRC MMAP FINE" << std::endl;

    // Set the destination file size to match the source file size
    if (ftruncate(dest_fd, file_size) == -1) {
        perror("Error setting file size");
        close(src_fd);
        close(dest_fd);
        return;
    }

    // Memory-map the destination file
    void* dest_map = mmap(NULL, file_size, PROT_WRITE, MAP_SHARED, dest_fd, 0);
    if (dest_map == MAP_FAILED) {
        close(src_fd);
        close(dest_fd);
        std::cerr << "Failed to memory-map destination file (FD: " << dest_fd << ", size: " << file_size << "): " << strerror(errno) << std::endl;
        munmap(src_map, file_size);
        return;
    }
    //std::cout << "DST MMAP FINE" << std::endl;

    // Copy data between the memory-mapped files

    // Allocate a temporary buffer that is aligned to the system's alignment requirements
    // const size_t alignment = 64;  // set to the alignment required by your system
    // char* temp_buffer = static_cast<char*>(aligned_alloc(alignment, file_size));

    // Copy the data from src to dst using the temporary buffer
    std::memcpy(dest_map, src_map, file_size);
    //std::cout << "FIRST MEM COPY" << std::endl;
    // std::memcpy(dest_map, temp_buffer, file_size);
    //std::cout << "SECOND MEM COPY" << std::endl;

    // Free the temporary buffer
    // std::free(temp_buffer);
    //std::cout << "COPY MMAP FINE" << std::endl;

    // Clean up resources
    munmap(src_map, file_size);
    munmap(dest_map, file_size);
}

void copy_file(const char* src_path, const char* dest_path) {

    // Open the source file for reading
    int num_open_files = count_open_files();
    // std::cout << "Number of open files: " << num_open_files << std::endl;
    // std::cout<<"FILE BEING CREATED START "<<dest_path<<std::endl;
    int src_fd = open(src_path, O_RDONLY);
    if (src_fd == -1) {
        perror("Failed to open source file: ");
        exit(1);
        return;
    }
    // std::cout<<"FILE BEING CREATED END "<<dest_path<<std::endl;
    // Open the destination file for writing

    int dest_fd = open(dest_path, O_RDWR | O_CREAT | O_TRUNC, 0777);
    if (dest_fd == -1) {

        std::cerr << "Failed to open destination file: " << dest_path << std::endl;
        exit(1);
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
void copy_dir_worker(const char* src_dir, const char* dest_dir,bool parent) {


    // std::cout<<"====================================================================="<<std::endl;
    // std::cout << "Copying directory " << src_dir << " to " << dest_dir << "..." << std::endl;
    // std::cout<<"====================================================================="<<std::endl;
    // std::cout<<"DIR BEING CREATED START "<<dest_dir<<std::endl;
    DIR* dir = opendir(src_dir);
    if (dir == NULL) {
        perror((std::string("Failed to open source directory: ") + src_dir).c_str());
        return;
    }
    // std::cout<<"DIR BEING CREATED END "<<dest_dir<<std::endl;
    // std::cout<<"MKDIR CRASH START"<<std::endl;
    if (mkdir(dest_dir, S_IRWXU | S_IRWXG | S_IRWXO) != 0) {
        if (errno != EEXIST){
            perror((std::string("Failed to create destination directory: ") + dest_dir).c_str());
            closedir(dir);
            return;
        } else {
            // std::cout << "Destination directory exits" << std::endl;
        }
    }
    // std::cout<<"MKDIR CRASH END"<<std::endl;
    // Create all subdirectories first
    struct dirent* entry;
    unsigned int numThreads = std::thread::hardware_concurrency();
    std::vector<std::thread> threads;
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
        //std::cout << "entry: " << src_path << " in thread" << std::this_thread::get_id() << "..." << std::endl;
        //std::cout << "mode: " << st.st_mode << " to " << S_ISDIR(st.st_mode) << "..." << std::endl;
        if (S_ISDIR(st.st_mode)) {
            if ((threads.size() < numThreads) && parent){
                std::string* src_path_copy = new std::string(src_path);
                std::string* dest_path_copy = new std::string(dest_path);
                // sem_wait(&sem); // Wait for the semaphore

                //std::cout << "dest: " << dest_path_copy << " in thread" << std::this_thread::get_id() << "..." << std::endl;
                std::thread t(copy_dir_worker, src_path_copy->c_str(), dest_path_copy->c_str(),false);
                threads.push_back(std::move(t));
                

            } else{
                copy_dir_worker(src_path.c_str(), dest_path.c_str(),false);
            }
            //t.join();
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

        std::string src_path_f = std::string(src_dir) + "/" + entry->d_name;
        std::string dest_path_f = std::string(dest_dir) + "/" + entry->d_name;

        
        struct stat st;
        if (stat(src_path_f.c_str(), &st) != 0) {
            perror((std::string("Failed to stat: ") + src_path_f).c_str());
            continue;
        }

        //std::cout << "entry: " << src_path_f << " in thread" << std::this_thread::get_id() << "..." << std::endl;
        //std::cout << "mode: " << st.st_mode << " to " << S_ISREG(st.st_mode) << "..." << std::endl;

        if (S_ISDIR(st.st_mode)) {
            // Do nothing
        } else if (S_ISREG(st.st_mode)) {
            // std::cout << "Copying file " << src_path_f << " to " << dest_path_f << "..." << std::endl;

            // Acquire semaphore before copying the file
            //sem_wait(&sem);

            // Copy the file in the current thread
            copy_file(src_path_f.c_str(), dest_path_f.c_str());

            // Release semaphore after copying the file
            //sem_post(&sem);
        } else {
            std::cerr << "Warning: Skipping unsupported file type: " << src_path_f << std::endl;
        }
    }

    closedir(dir);
    // std::cout<<"JOIN START"<<std::endl;
    for (std::thread & th : threads){
    // If thread Object is Joinable then Join that thread.
        if (th.joinable()){
            th.join();

        }
    }
    // std::cout<<"JOIN END"<<std::endl;
}
void copy_dir(const char* src_dir, const char* dest_dir) {
    // std::cout << "Copying directory " << src_dir << " to " << dest_dir << "..." << std::endl;

    //sem_t sem;
    //sem_init(&sem, 0, 5);

    //std::thread t(copy_dir_worker, src_dir, dest_dir, std::ref(sem));
    //t.join();
    copy_dir_worker(src_dir, dest_dir,false);

    // std::cout << "Finished copying directory " << src_dir << " to " << dest_dir << "." << std::endl;

    //sem_destroy(&sem);
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
    // sem_init(&sem, 0, max_concurrent_threads);
    if (S_ISDIR(src_stat.st_mode)) {
      // std::cout<<"3"<<std::endl;
        copy_dir(SOURCE_DIR, DESTINATION_DIR);
    } else {
        std::cerr << "Source path is not a regular file or directory" << std::endl;
        return 1;
    }
    // sem_destroy(&sem);

    return 0;
}