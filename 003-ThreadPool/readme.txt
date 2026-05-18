编译指令
# g++ test_thread_pool.cc thread_pool.cc -o test_pool -std=c++11 -pthread -O2
# ./test_pool 

结果：
--- Starting ThreadPool Validations ---
[PASS] Basic execution logic matched.
[PASS] Concurrency stress test (100k tasks) matched.
--- All Validations Completed Successfully ---