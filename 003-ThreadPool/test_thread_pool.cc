#include "thread_pool.h"
#include <iostream>
#include <vector>
#include <chrono>
#include <cassert>
#include <atomic>
#include <cstdlib>

// 测试1：基础功能与异步返回值
bool test_basic_execution() {
    ThreadPool pool(4);
    
    auto result1 = pool.enqueue([]() { return 42; });
    auto result2 = pool.enqueue([](int a, int b) { return a + b; }, 10, 20);
    
    return (result1.get() == 42) && (result2.get() == 30);
}

// 测试2：高并发竞态与原子性验证
bool test_concurrency() {
    ThreadPool pool(8);
    std::atomic<int> counter{0};
    constexpr int TASK_COUNT = 100000;
    std::vector<std::future<void>> futures;

    // 高频并发提交任务
    for (int i = 0; i < TASK_COUNT; ++i) {
        futures.emplace_back(pool.enqueue([&counter]() {
            // 模拟极短暂的耗时工作，刻意引发竞态交错
            std::this_thread::sleep_for(std::chrono::microseconds(1));
            counter.fetch_add(1, std::memory_order_relaxed);
        }));
    }

    // 阻塞等待所有任务完成
    for (auto& f : futures) {
        f.get();
    }

    // 校验执行次数是否严格对齐
    return counter.load() == TASK_COUNT;
}

int main() {
    // 关闭 IO 同步，提升日志输出性能
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(NULL);

    std::cout << "--- Starting ThreadPool Validations ---\n";

    try {
        if (!test_basic_execution()) {
            std::cerr << "[FAIL] Basic execution test failed.\n";
            return EXIT_FAILURE;
        }
        std::cout << "[PASS] Basic execution logic matched.\n";

        if (!test_concurrency()) {
            std::cerr << "[FAIL] Concurrency stress test failed (Data mismatch).\n";
            return EXIT_FAILURE;
        }
        std::cout << "[PASS] Concurrency stress test (100k tasks) matched.\n";

    } catch (const std::exception& e) {
        std::cerr << "[FATAL] Uncaught exception: " << e.what() << "\n";
        return EXIT_FAILURE;
    }

    std::cout << "--- All Validations Completed Successfully ---\n";
    return EXIT_SUCCESS;
}