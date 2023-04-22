import os
import random
import string

DEPTH = 3
BREADTH = 3

DATA_POOL_SIZE = 1024 * 1024 * 20  # 100 MB
DATA_POOL = ''.join(random.choice(string.ascii_letters) for _ in range(DATA_POOL_SIZE))

def generate_random_file(path, is_small):
    # Generate a random file size
    if is_small:
        size = random.randint(100, 400 )  # 0.5 KB to 10 MB
    else:
        size = random.randint(1024 * 10, 1024 * 1024 * 15)  

    # Calculate the start and end indices of the slice to use
    start = random.randint(0, DATA_POOL_SIZE - size)
    end = start + size

    # Get the slice of the data pool to use for this file
    data = DATA_POOL[start:end]

    # Write the data to the file
    with open(path, 'w') as f:
        f.write(data)

def generate_random_tree(path, depth, small_to_large_ratio):
    # Generate random files in this directory
    num_files = 10
    for i in range(num_files):
        filename = f'{path}/file_{i}.txt'
        is_small = random.random() < small_to_large_ratio
        generate_random_file(filename, is_small)

    # Recurse to generate subdirectories
    if depth > 0:
        num_subdirs = random.randint(BREADTH, BREADTH)
        for i in range(num_subdirs):
            subdir_path = f'{path}/subdir_{depth}_{i}'
            os.mkdir(subdir_path)
            generate_random_tree(subdir_path, depth - 1, small_to_large_ratio)

# Create the top-level directory
if not os.path.exists('src'):
    os.mkdir('src')

# Define the ratio of small files to large files
small_to_large_ratio = 0.8  # 80% small files, 20% large files

# Generate the random directory tree
generate_random_tree('src', DEPTH, small_to_large_ratio)