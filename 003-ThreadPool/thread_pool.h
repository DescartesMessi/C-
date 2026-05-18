#ifndef _THREAD_POOL_H_
#define _THREAD_POOL_H_

#include<vector>
#include<thread>
#include<queue>
#include<mutex>
#include<condition_variable>
#include<functional>
#include<future>
#include<stdexcept>
#include<memory>


class ThreadPool{
private:
    size_t m_threads{0};
    std::vector<std::thread> m_workers;
    std::queue<std::function<void()>> m_tasks;
    std::mutex m_queue_mutex;
    std::condition_variable m_condition;
    bool m_stop{false};

public:
    explicit ThreadPool(size_t m_threads);
    ~ThreadPool();

    ThreadPool(const ThreadPool&)=delete;
    ThreadPool& operator=(const ThreadPool&)=delete;

    template <class F ,class ...Args>
    auto enqueue(F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type>;

};

template <class F ,class ...Args>
auto ThreadPool::enqueue(F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type>{
    using return_type = typename std::result_of<F(Args...)>::type;
    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );

    std::future<return_type> res = task->get_future();
    {
        std::unique_lock<std::mutex> lock(m_queue_mutex);
        if(m_stop){
            throw std::runtime_error("enqueue on stopped ThreadPool");
        }
        m_tasks.emplace([task](){(*task)();});
    }
    m_condition.notify_one();
    return res;     
}







#endif // _THREAD_POOL_H_