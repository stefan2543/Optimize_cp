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
#include <algorithm>
#include <boost/asio.hpp>
#include <queue>
#include <mutex>
#include <sys/types.h>

#include <stack>
#include <memory>
using namespace boost::asio;

using namespace boost::system;
using namespace std::placeholders;
namespace fs = std::filesystem;
using namespace std;
char* SOURCE_DIR = nullptr;
char* DESTINATION_DIR = nullptr;
const int BUFFER_SIZE = 4096;
constexpr int NUM_BUFFERS = 5;
void async_copy(size_t file_size,int src_fd, int dest_fd);
int parse_commandline(int argc, char *argv[]);
void sync_copy(off_t file_size, int src_fd, int dest_fd);
void copy_dir_worker(const char* src_dir, const char* dest_dir);
void copy_dir(const char* src_dir, const char* dest_dir);
void copyFilesWorker(std::vector<std::string>::iterator start, std::vector<std::string>::iterator end, const std::string& dst_dir);
void copy_files(const std::vector<std::string>& src_paths, const std::string& dst_dir, int numThreads);


template<typename T>
class ThreadSafeStack {
public:
    ThreadSafeStack() = default;
    ThreadSafeStack(const ThreadSafeStack& other) {
        std::lock_guard<std::mutex> lock(other.mutex_);
        data_ = other.data_;
    }

    ThreadSafeStack& operator=(const ThreadSafeStack&) = delete;

    void push(T new_value) {
        std::lock_guard<std::mutex> lock(mutex_);
        data_.push(std::move(new_value));
    }

    std::shared_ptr<T> pop() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (data_.empty()) {
            throw std::runtime_error("Attempt to pop from empty stack.");
        }
        std::shared_ptr<T> const res(std::make_shared<T>(std::move(data_.top())));
        data_.pop();
        return res;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return data_.empty();
    }

private:
    mutable std::mutex mutex_;
    std::stack<T> data_;
};

ThreadSafeStack<std::string> file_list;



void list_files(const fs::path& dir_path) {
    for (const auto& entry : fs::directory_iterator(dir_path)) {
        const auto& path = entry.path();
        if (fs::is_directory(path)) {
            list_files(path); // recursively traverse subdirectory
        } else if (fs::is_regular_file(path)) {
            file_list.push(path.string()); // add file to list
        }
    }
}
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

void async_copy(size_t file_size, int src_fd, int dest_fd) {
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
        std::cerr << "Failed to memory-map destination file (FD: " << dest_fd << ", size: " << file_size << "): " << strerror(errno) << std::endl;
        munmap(src_map, file_size);
        return;
    }
    //std::cout << "DST MMAP FINE" << std::endl;

    // Copy data between the memory-mapped files

    // Allocate a temporary buffer that is aligned to the system's alignment requirements
    const size_t alignment = 64;  // set to the alignment required by your system
    char* temp_buffer = static_cast<char*>(aligned_alloc(alignment, file_size));

    // Copy the data from src to dst using the temporary buffer
    std::memcpy(temp_buffer, src_map, file_size);
    //std::cout << "FIRST MEM COPY" << std::endl;
    std::memcpy(dest_map, temp_buffer, file_size);
    //std::cout << "SECOND MEM COPY" << std::endl;

    // Free the temporary buffer
    std::free(temp_buffer);
    //std::cout << "COPY MMAP FINE" << std::endl;

    // Clean up resources
    munmap(src_map, file_size);
    munmap(dest_map, file_size);
}
void copy_file(const char* src_path, const char* dest_path) {
    // Open the source file for reading
    // std::cout<<"FILE BEING CREATED: "<<dest_path<<std::endl;
    int src_fd = open(src_path, O_RDONLY | O_LARGEFILE);

    if (src_fd == -1) {
        std::cerr << "Failed to open source file: " << src_path << std::endl;
        return;
    }
    // Open the destination file for writing

    int dest_fd = open(dest_path,  O_WRONLY | O_CREAT | O_TRUNC | O_LARGEFILE, 0644);
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
    size_t file_size = st.st_size;
    if (ftruncate(dest_fd, file_size) == -1) {
        perror("Error setting destination file size");
        close(src_fd);
        close(dest_fd);
        
    }
    // Copy the contents of the source file to the destination file
    if (file_size <= 50) {
        // Use synchronous copy for small files
        sync_copy(file_size, src_fd,dest_fd);

    } else {
        async_copy(file_size,src_fd,dest_fd);

    }

    // Close the files
    close(src_fd);
    close(dest_fd);
}
void copy_dir_worker(const char* src_dir, const char* dest_dir) {
    // std::cout << "Copying directory " << src_dir << " to " << dest_dir << "..." << std::endl;

    DIR* dir = opendir(src_dir);
    if (dir == NULL) {
        perror((std::string("Failed to open source directory: ") + src_dir).c_str());
        return;
    }

    if (mkdir(dest_dir, S_IRWXU | S_IRWXG | S_IRWXO) != 0) {
        if (errno != EEXIST){
            perror((std::string("Failed to create destination directory: ") + dest_dir).c_str());
            closedir(dir);
            return;
        } else {
        }
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

            copy_dir_worker(src_path.c_str(), dest_path.c_str());

        } else if (S_ISREG(st.st_mode)) {

        } else {
            std::cerr << "Warning: Skipping unsupported file type: " << src_path << std::endl;
        }
    }

    closedir(dir);


}

void copy_files_worker(const std::string& dst_dir) {


    while (!file_list.empty()) {

        std::string src_path = *file_list.pop();
        std::string path = src_path.substr(src_path.find_first_of("/"));
        std::string dst_path = dst_dir + path;
        // std::cout << "COPYING: " << src_path << " TO " << dst_path << std::endl;
        copy_file(src_path.c_str(), dst_path.c_str());
    }
}

void copy_files(const std::string& dst_dir, int numThreads) {
    std::vector<std::thread> threads;
    std::vector<std::pair<int, int>> start_end_positions;

    // Calculate the work range for each thread
    
    
    // Start the worker threads
    for (int i = 0; i < numThreads; ++i)
    {
       
        std::thread t(copy_files_worker, dst_dir);
        threads.push_back(std::move(t));
    }
    copy_files_worker(dst_dir);
    // Join the worker threads
    for (auto& thread : threads) {
        thread.join();
    }
}
void copy_dir(const char* src_dir, const char* dest_dir) {
    // std::cout << "Copying directory " << src_dir << " to " << dest_dir << "..." << std::endl;

    //sem_t sem;
    //sem_init(&sem, 0, 5);

    //std::thread t(copy_dir_worker, src_dir, dest_dir, std::ref(sem));
    //t.join();
    
    fs::path src_path = src_dir;
    list_files(src_path);
        // Join the worker threads
    // auto start_time = std::chrono::high_resolution_clock::now();

    copy_dir_worker(src_dir, dest_dir);
    // auto end_time = std::chrono::high_resolution_clock::now();
    // std::cout << "Elapsed time: " << std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count() << "ms\n";
    const int num_threads = 5;
    copy_files( dest_dir, num_threads);

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
    // std::cout<<"1"<<std::endl;
    //if (S_ISREG(src_stat.st_mode)) {
      // std::cout<<"2"<<std::endl;
        //copy_file(SOURCE_DIR, DESTINATION_DIR);
    if (S_ISDIR(src_stat.st_mode)) {
      // std::cout<<"3"<<std::endl;
        copy_dir(SOURCE_DIR, DESTINATION_DIR);
    } else {
        std::cerr << "Source path is not a regular file or directory" << std::endl;
        return 1;
    }

    return 0;
}