#!/bin/bash

# Define source and destination directories
src="src"
dst="dst"
results_file="async_threads_results.txt"
# Define number of test runs
num_runs=5

# Define file size for test files (in bytes)
file_size=1000000

# Define optimized_cp command
optimized_cp="./queue -s src/ -d dst"

# Define Linux cp command
linux_cp="cp -r src dst"

# Create results file if it doesn't exist
if [ ! -f "$results_file" ]; then
    touch "$results_file"
fi
# Define function to flush memory/caches
flush_caches () {
    echo "Flushing memory/caches..."
    sync
    sudo sh -c "echo 3 > /proc/sys/vm/drop_caches"
}

# Delete destination directory before each test


# Run tests
rm -rf $src
rm -rf $dst
python create_src_dir.py
echo "Running tests..."
for ((i=1; i<=$num_runs; i++)); do
    echo "Test $i..."

    # Flush memory/caches

    flush_caches
    # rm -rf $src
    rm -rf $dst
    # python create_src_dir.py
    # Time optimized_cp
    start=$(date +%s.%N)
    $optimized_cp 
    end=$(date +%s.%N)
    elapsed_optimized_cp=$(echo "$end - $start" | bc)

    # Flush memory/caches
    flush_caches
    rm -rf $dst
    # rm -rf $src
    # python create_src_dir.py
    # Time Linux cp
    start=$(date +%s.%N)
    $linux_cp
    end=$(date +%s.%N)
    elapsed_linux_cp=$(echo "$end - $start" | bc)
    
    # Print results to results file
    echo "Test $i:" >> "$results_file"
    echo "optimized_cp: $elapsed_optimized_cp seconds" >> "$results_file"
    echo "Linux cp: $elapsed_linux_cp seconds" >> "$results_file"
done

# Calculate average and standard deviation
echo "Calculating average and standard deviation..."
elapsed_optimized_cp_avg=$(grep optimized_cp: "$results_file" | awk '{sum+=$2} END {print sum/NR}')
elapsed_linux_cp_avg=$(grep 'Linux cp:' "$results_file" | awk '{sum+=$3} END {print sum/NR}')
elapsed_optimized_cp_stddev=$(grep optimized_cp: "$results_file" | awk -v avg="$elapsed_optimized_cp_avg" '{sum+=($2-avg)^2} END {print sqrt(sum/NR)}')
elapsed_linux_cp_stddev=$(grep 'Linux cp:' "$results_file" | awk -v avg="$elapsed_linux_cp_avg" '{sum+=($3-avg)^2} END {print sqrt(sum/NR)}')

# Print results
echo "Results:"
echo "optimized_cp: avg = $elapsed_optimized_cp_avg seconds, stddev = $elapsed_optimized_cp_stddev seconds"
echo "Linux cp: avg = $elapsed_linux_cp_avg seconds, stddev = $elapsed_linux_cp_stddev seconds"