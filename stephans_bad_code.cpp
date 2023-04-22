#include <iostream>
#include <vector>
#include <filesystem>
#include <chrono>
#include <future>
#include <algorithm>
#include <numeric>

namespace fs = std::filesystem;
using namespace std::chrono;

// Function to calculate the file size
std::uintmax_t get_file_size(const fs::path& path)
{
    return fs::file_size(path);
}

// Function to copy files asynchronously
std::future<void> async_copy(const fs::path& source, const fs::path& destination)
{
    return std::async(std::launch::async, [source, destination]() {
        // std::cout << "Copying " << source << " to " << destination << std::endl;
        fs::copy_file(source, destination, fs::copy_options::overwrite_existing);
    });
}

// Function to recursively copy directories
std::future<void> async_copy_directory(const fs::path& source_dir, const fs::path& destination_dir)
{
    // Create the destination directory if it does not exist
    if (!fs::exists(destination_dir)) {

        fs::create_directory(destination_dir);
    }

    return std::async(std::launch::async, [source_dir, destination_dir]() {
        for (const auto& entry : fs::recursive_directory_iterator(source_dir)) {
            // If the entry is a file, copy it to the destination directory
            fs::path rel_path = fs::relative(entry.path(), source_dir);
            fs::path destination = destination_dir / rel_path;
                
            if (fs::is_regular_file(entry)) {
                // const auto& source = entry.path();
                // const auto& destination = destination_dir / source.relative_path();

                async_copy(entry.path(), destination);
            }
            // If the entry is a directory, recursively copy it to the destination directory
            else if (fs::is_directory(entry)) {

                async_copy_directory(entry.path(), destination);
            }
        }
    });
}

int main(int argc, char* argv[])
{
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <source-dir> <destination-dir>" << std::endl;
        return 1;
    }

    fs::path source_dir(argv[1]);
    fs::path destination_dir(argv[2]);

    if (!fs::exists(source_dir) || !fs::is_directory(source_dir)) {
        std::cerr << "Source directory does not exist or is not a directory: " << source_dir << std::endl;
        return 1;
    }

    // Start the timer
    auto start_time = high_resolution_clock::now();

    // Copy all files and subdirectories asynchronously
    auto future = async_copy_directory(source_dir, destination_dir);

    // Wait for the copy to complete
    future.get();

    // Calculate the total size of all copied files
    auto total_size = std::transform_reduce(fs::recursive_directory_iterator(source_dir), fs::recursive_directory_iterator(), 0ULL, std::plus<>(), get_file_size);

    // Calculate the elapsed time
    auto end_time = high_resolution_clock::now();
    auto elapsed_time = duration_cast<milliseconds>(end_time - start_time).count();

    std::cout << "Copied " << total_size << " bytes in " << elapsed_time << " milliseconds" << std::endl;

    return 0;
}