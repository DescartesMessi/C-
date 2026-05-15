#include <iostream>
#include <thread>
#include <vector>
#include <cassert>
#include <chrono>
#include <atomic>

// 假设之前的头文件名为 SPSC_RingBuffer.hpp
#include "001.SPSCRingBuffer.h"
#include "001test.h"
// ==========================================
// 测试 1：基础逻辑与边界测试 (单线程环境)
// 目的：验证判空、判满、越界环绕、以及有效容量 (Capacity - 1)
// ==========================================
void test_basic_logic() {
    // 实例化容量为 4，实际可用槽位为 3
    SPSC_RingBuffer<int, 4> rb;
    
    assert(rb.isEmpty() == true);

    int val = 0;
    // 1. 空载出队测试
    assert(rb.pop(val) == false); 

    // 2. 正常入队测试
    assert(rb.push(1) == true);
    assert(rb.push(2) == true);
    assert(rb.push(3) == true);
    
    // 3. 满载入队测试：队列应已满，拒绝写入
    assert(rb.push(4) == false); 
    assert(rb.isEmpty() == false);

    // 4. FIFO 顺序验证
    assert(rb.pop(val) == true && val == 1); 
    assert(rb.pop(val) == true && val == 2);
    
    // 5. 环绕测试 (Wrap-around)
    assert(rb.push(5) == true); 
    assert(rb.push(6) == true);
    assert(rb.push(7) == false); // 再次满载

    assert(rb.pop(val) == true && val == 3);
    assert(rb.pop(val) == true && val == 5);
    assert(rb.pop(val) == true && val == 6);
    
    // 6. 耗尽后的空载验证
    assert(rb.pop(val) == false); 
    
    std::cout << "[PASS] test_basic_logic: 边界与环绕逻辑正确。\n";
}

// ==========================================
// 测试 2：SPSC 并发正确性测试 (多线程环境)
// 目的：验证 Release-Acquire 内存屏障是否有效防止了数据竞争与乱序
// ==========================================
void test_concurrency() {
    constexpr size_t TEST_SIZE = 10000000; // 1000万次读写
    SPSC_RingBuffer<size_t, 1024> rb;
    
    std::thread producer([&]() {
        for (size_t i = 0; i < TEST_SIZE; ++i) {
            // 失败则自旋让出时间片 (Yield strategy)
            while (!rb.push(i)) {
                std::this_thread::yield(); 
            }
        }
    });

    std::thread consumer([&]() {
        size_t expected = 0;
        size_t val = 0;
        while (expected < TEST_SIZE) {
            if (rb.pop(val)) {
                // 严格校验数据一致性：如果内存序失效，这里会读到乱码或旧数据
                if (val != expected) {
                    std::cerr << "FATAL ERROR: Data corruption. Expected " 
                              << expected << ", got " << val << "\n";
                    std::abort();
                }
                expected++;
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();
    std::cout << "[PASS] test_concurrency: 1000万次并发读写一致性校验通过。\n";
}

// ==========================================
// 测试 3：高吞吐量性能基准测试
// 目的：评估无锁队列结合 Cache-line 对齐后的极限吞吐能力
// ==========================================
void test_benchmark() {
    constexpr size_t TEST_SIZE = 50000000; // 5000万次操作
    SPSC_RingBuffer<size_t, 8192> rb;

    auto start_time = std::chrono::high_resolution_clock::now();

    std::thread producer([&]() {
        for (size_t i = 0; i < TEST_SIZE; ++i) {
            // 极限自旋，不 yield，测算纯粹的 CAS/Atomic 开销
            while (!rb.push(i)); 
        }
    });

    std::thread consumer([&]() {
        size_t val = 0;
        for (size_t i = 0; i < TEST_SIZE; ++i) {
            while (!rb.pop(val));
        }
    });

    producer.join();
    consumer.join();

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end_time - start_time;

    double ops = TEST_SIZE / diff.count();
    std::cout << "[PASS] test_benchmark: 耗时 " << diff.count() << " 秒.\n";
    std::cout << "       Throughput: " << (ops / 1000000.0) << " Million Ops/sec.\n";
}

// int main() {
//     std::cout << "--- Starting SPSC_RingBuffer Validations ---\n";
//     test_basic_logic();
//     test_concurrency();
//     test_benchmark();
//     std::cout << "--- All Validations Completed ---\n";
//     return 0;
// }
/*--- Starting SPSC_RingBuffer Validations ---
[PASS] test_basic_logic: 边界与环绕逻辑正确。
[PASS] test_concurrency: 1000万次并发读写一致性校验通过。
[PASS] test_benchmark: 耗时 7.38772 秒.
       Throughput: 6.76798 Million Ops/sec.
--- All Validations Completed ---
[1] + Done                       "/usr/bin/gdb" --interpreter=mi --tty=${DbgTerm} 0<"/tmp/Microsoft-MIEngine-In-3p12u3rv.cxe" 1>"/tmp/Microsoft-MIEngine-Out-gvderfl1.0gr"*/
