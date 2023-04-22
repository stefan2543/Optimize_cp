# Optimize_cp
run create_src_dir.py to create src dir test set
<br>
g++ -std=c++17  thread_queue.cpp -o queue -lrt -lpthread -luring
<br>
to copy directory run ./optimized_cp -s src -d dst 
<br>
time ./optimized_cp -s src -d dst
<br>
time cp -r src dst
<br>
to run tests run
<br>
./run_tests.sh
