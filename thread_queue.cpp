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
#include <sys/types.h>
#include <liburing.h>
#include <stack>
#include <memory>

namespace fs = std::filesystem;

char* SOURCE_DIR = nullptr;
char* DESTINATION_DIR = nullptr;
const int BUFFER_SIZE = 16384;
constexpr int NUM_BUFFERS = 5;
constexpr int num_threads = 5;



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

void async_copy(size_t file_size,int src_fd, int dst_fd,bool use_io_uring=false) {

    if(use_io_uring){
io_uring ring;
int ret = io_uring_queue_init(3, &ring, 0);
if (ret) {
    std::cerr << "Error initializing io_uring: " << strerror(-ret) << std::endl;
    return;
}

std::vector<char> buffer(BUFFER_SIZE);
iovec iov = {
    .iov_base = buffer.data(),
    .iov_len = BUFFER_SIZE,
};

io_uring_sqe *sqe;
io_uring_cqe *cqe;
off_t offset = 0; 

while (true) {
    sqe = io_uring_get_sqe(&ring);
    io_uring_prep_readv(sqe, src_fd, &iov, 1, offset); // Replace -1 with offset
    io_uring_submit(&ring);
    ret = io_uring_wait_cqe(&ring, &cqe);

    if (ret < 0) {
        std::cerr << "Error waiting for read completion: " << strerror(-ret) << std::endl;
        break;
    }
    ssize_t bytes_read = cqe->res;
    io_uring_cqe_seen(&ring, cqe);

    if (bytes_read <= 0) {
        break;
    }

    iov.iov_len = bytes_read;
    sqe = io_uring_get_sqe(&ring);
    io_uring_prep_writev(sqe, dst_fd, &iov, 1, offset); 
    io_uring_submit(&ring);
    ret = io_uring_wait_cqe(&ring, &cqe);

    if (ret < 0) {
        std::cerr << "Error waiting for write completion: " << strerror(-ret) << std::endl;
        break;
    }

    ssize_t bytes_written = cqe->res;
    io_uring_cqe_seen(&ring, cqe);

    if (bytes_written != bytes_read) {
        std::cerr << "Error writing data to destination file: partial write" << std::endl;
        break;
    }

    offset += bytes_read; 
}

io_uring_queue_exit(&ring);
    }
    else{


        char buffers[NUM_BUFFERS][BUFFER_SIZE];
        struct aiocb aio_read_cbs[NUM_BUFFERS];
        struct aiocb aio_write_cbs[NUM_BUFFERS];
        size_t total_bytes_written=0;
        int finished_reads[NUM_BUFFERS]={1};
        int finished_writing[NUM_BUFFERS]={1};
        for (int i = 0; i < NUM_BUFFERS; ++i) {
            memset(&aio_read_cbs[i], 0, sizeof(struct aiocb));
            aio_read_cbs[i].aio_fildes = src_fd;
            aio_read_cbs[i].aio_buf = buffers[i];
            aio_read_cbs[i].aio_nbytes = BUFFER_SIZE;

            memset(&aio_write_cbs[i], 0, sizeof(struct aiocb));
            aio_write_cbs[i].aio_fildes = dst_fd;
            aio_write_cbs[i].aio_buf = buffers[i];
        }

        bool read_finished = false;
        off_t current_offset = 0;

        while (!read_finished) {
        // read_finished = true;

        // Submit read requests for all buffers
            for (int i = 0; i < NUM_BUFFERS; ++i) {
                if(finished_reads[i]==1){

                    aio_read_cbs[i].aio_offset = current_offset;
            // std::cout << aio_read_cbs[i].aio_offset << std::endl;
                    if (aio_read(&aio_read_cbs[i]) < 0) {
                        perror("Error submitting aio_read");
                        break;
                    }
                    else{
                        finished_reads[i]=0;
                        current_offset += BUFFER_SIZE;
                    }


                }
            }

        // Wait for all read requests to complete and submit corresponding write requests
            for (int i = 0; i < NUM_BUFFERS; ++i) {

                if (aio_error(&aio_read_cbs[i]) != EINPROGRESS && finished_reads[i]==0 ) {
                // std::cout<<"FINISHED READING"<<std::endl;
                //start writing in here

                    size_t bytes_read = aio_return(&aio_read_cbs[i]);
                    if (bytes_read > 0) {
                        for (int j = 0; j < NUM_BUFFERS; ++j)
                        {
                            if (finished_writing[j]==1 ){
                                aio_write_cbs[j].aio_offset = aio_read_cbs[j].aio_offset;
                                aio_write_cbs[j].aio_nbytes = bytes_read;
                                if (aio_write(&aio_write_cbs[j]) < 0) {
                                    perror("Error submitting aio_write");

                                    break;
                                }
                                else{
                                    finished_writing[j]=0;
                                    finished_reads[i]=1;
                                    break;
                                }

                            }

                // read_finished = false;
                        }

                    }
                }


            }

        // Wait for all write requests to complete
            int total_finished_writers=0;
            for (int i = 0; i < NUM_BUFFERS; ++i) {

                if (aio_error(&aio_write_cbs[i]) != EINPROGRESS && finished_writing[i]==0 ) {
                // Waiting for the write operation to complete
                    finished_writing[i]=1;

                    total_finished_writers+1;
                    size_t bytes_written = aio_return(&aio_write_cbs[i]);
                    total_bytes_written+= bytes_written;


                    if (total_bytes_written>=file_size) {
                        read_finished=true;
                        break;
                    }
                }

            }
        }
    // std::cout << "DONE COPYING" << std::endl;
    }
}
void sync_copy(size_t file_size, int src_fd, int dest_fd, bool slice=false) {
    if(slice){
        int pipefd[2];  
        ssize_t total_bytes_copied = 0;
        ssize_t bytes_copied;
        loff_t offset = 0;
        if (pipe(pipefd) == -1) {
            perror("pipe");
            close(src_fd);
            close(dest_fd);
            exit(EXIT_FAILURE);
        }
        while (total_bytes_copied < file_size) {
            bytes_copied = splice(src_fd, &offset, pipefd[1], NULL, file_size - total_bytes_copied, SPLICE_F_MORE);
            if (bytes_copied == -1) {
                perror("splice");
                close(src_fd);
                close(dest_fd);
                close(pipefd[0]);
                close(pipefd[1]);
                exit(EXIT_FAILURE);
            }

            ssize_t bytes_written = splice(pipefd[0], NULL, dest_fd, NULL, bytes_copied, SPLICE_F_MORE);
            if (bytes_written == -1) {
                perror("splice");
                close(src_fd);
                close(dest_fd);
                close(pipefd[0]);
                close(pipefd[1]);
                exit(EXIT_FAILURE);
            }

            total_bytes_copied += bytes_written;
        }

        close(pipefd[0]);
        close(pipefd[1]);
    }
    else{
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

        std::memcpy(dest_map, src_map, file_size);


    // Clean up resources
        munmap(src_map, file_size);
        munmap(dest_map, file_size);
    }
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

    int dest_fd = open(dest_path,  O_WRONLY | O_CREAT | O_TRUNC, 0644);
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
    if (file_size <= 512) {
        // Use synchronous copy for small files
        sync_copy(file_size, src_fd,dest_fd, true);

    } else {
        async_copy(file_size,src_fd,dest_fd,true);

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

void copy_files_worker(  const std::string& dst_dir) {


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
    bool use_threads=true;
    if(use_threads){
    // Start the worker threads
        for (int i = 0; i < numThreads; ++i)
        {

            std::thread t(copy_files_worker, dst_dir);
            threads.push_back(std::move(t));
        }
    }
    copy_files_worker(dst_dir);
    // Join the worker threads
    if(use_threads){
        for (auto& thread : threads) {
            thread.join();
        }
    }
}
void copy_dir(const char* src_dir, const char* dest_dir) {

    fs::path src_path = src_dir;
    list_files(src_path);
        // Join the worker threads

    copy_dir_worker(src_dir, dest_dir);

    
    copy_files( dest_dir, num_threads);


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

    if (S_ISDIR(src_stat.st_mode)) {

        copy_dir(SOURCE_DIR, DESTINATION_DIR);
    } else {
        std::cerr << "Source path is not a regular file or directory" << std::endl;
        return 1;
    }

    return 0;
}