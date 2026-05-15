#ifndef TEST_MPMC_QUEUE_HPP
#define TEST_MPMC_QUEUE_HPP

/**
 * @brief 运行多生产者多消费者(MPMC)无锁队列的并发一致性与吞吐量测试
 * @return true 测试通过（校验和匹配），false 测试失败（数据损坏或丢失）
 */
bool run_mpmc_concurrency_test();

#endif // TEST_MPMC_QUEUE_HPP