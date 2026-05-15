#include "002test.h"
#include "002MPMCRingBuffer.h" // 引入待测试的无锁队列模板头文件
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>

// 内部链接属性，限制作用域在本翻译单元内（等价于 static，但现代 C++ 推荐使用匿名命名空间）
namespace {
    constexpr size_t QUEUE_CAPACITY = 8192;
    constexpr size_t PRODUCER_COUNT = 4;
    constexpr size_t CONSUMER_COUNT = 4;
    constexpr size_t ITEMS_PER_PRODUCER = 2000000; 
}

bool run_mpmc_concurrency_test() {
    LockFreeMPMCQueue<uint64_t> queue(QUEUE_CAPACITY);

    std::atomic<uint64_t> total_produced_sum{0};
    std::atomic<uint64_t> total_consumed_sum{0};
    std::atomic<size_t> total_consumed_count{0};
    std::atomic<bool> start_flag{false};

    auto producer_task = [&](size_t thread_id) {
        while (!start_flag.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        uint64_t local_sum = 0;
        uint64_t base_val = (thread_id + 1) * 100000000ULL; 

        for (size_t i = 1; i <= ITEMS_PER_PRODUCER; ++i) {
            uint64_t val = base_val + i;
            while (!queue.push(val)) {
                std::this_thread::yield();
            }
            local_sum += val;
        }
        total_produced_sum.fetch_add(local_sum, std::memory_order_relaxed);
    };

    auto consumer_task = [&]() {
        while (!start_flag.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        uint64_t local_sum = 0;
        const size_t TARGET_CONSUME_COUNT = PRODUCER_COUNT * ITEMS_PER_PRODUCER;

        while (true) {
            if (total_consumed_count.load(std::memory_order_relaxed) >= TARGET_CONSUME_COUNT) {
                break;
            }

            uint64_t val = 0;
            if (queue.pop(val)) {
                local_sum += val;
                total_consumed_count.fetch_add(1, std::memory_order_relaxed);
            } else {
                std::this_thread::yield();
            }
        }
        total_consumed_sum.fetch_add(local_sum, std::memory_order_relaxed);
    };

    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;

    auto start_time = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < PRODUCER_COUNT; ++i) {
        producers.emplace_back(producer_task, i);
    }
    for (size_t i = 0; i < CONSUMER_COUNT; ++i) {
        consumers.emplace_back(consumer_task);
    }

    start_flag.store(true, std::memory_order_release);

    for (auto& t : producers) t.join();
    for (auto& t : consumers) t.join();

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end_time - start_time;

    bool is_consistent = (total_produced_sum.load() == total_consumed_sum.load()) && 
                         (total_consumed_count.load() == PRODUCER_COUNT * ITEMS_PER_PRODUCER);

    if (is_consistent) {
        double qps = (PRODUCER_COUNT * ITEMS_PER_PRODUCER) / diff.count();
        std::cout << "[PASS] MPMC Concurrency Test Checksum Matched.\n";
        std::cout << "       Throughput: " << (qps / 1000000.0) << " Million Ops/sec.\n";
    } else {
        std::cerr << "[FAIL] Data Corruption Detected!\n";
        std::cerr << "       Produced: " << total_produced_sum.load() << "\n";
        std::cerr << "       Consumed: " << total_consumed_sum.load() << "\n";
    }

    return is_consistent;
}