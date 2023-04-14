import os
import random
import string

DEPTH = 5
BREADTH = 5

DATA_POOL_SIZE = 1024 * 1024 * 100  # 100 MB
DATA_POOL = ''.join(random.choice(string.ascii_letters) for _ in range(DATA_POOL_SIZE))

def generate_random_file(path, is_small):
    # Generate a random file size
    if is_small:
        size = random.randint(100, 512 )  # 0.5 KB to 10 MB
    else:
        size = random.randint(1024 * 10, 1024 * 1024 * 10)  # 10 MB to 5 GB

    # Calculate the start and end indices of the slice to use
    start = random.randint(0, DATA_POOL_SIZE - size)
    end = start + size

    # Get the slice of the data pool to use for this file
    data = DATA_POOL[start:end]

    # Write the data to the file
    with open(path, 'w') as f:
        f.write(data)

def generate_random_tree(path, depth):
    # Generate random files in this directory
    num_files = random.randint(1, 10)
    for i in range(num_files):
        filename = f'{path}/file_{i}.txt'
        is_small = random.choice([True, False])
        generate_random_file(filename, is_small)

    # Recurse to generate subdirectories
    if depth > 0:
        num_subdirs = random.randint(1, BREADTH)
        for i in range(num_subdirs):
            subdir_path = f'{path}/subdir_{depth}_{i}'
            os.mkdir(subdir_path)
            generate_random_tree(subdir_path, depth - 1)

# Create the top-level directory
if not os.path.exists('src'):
    os.mkdir('src')

# Generate the random directory tree
generate_random_tree('src', DEPTH)