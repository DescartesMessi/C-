#include "thread_pool.h"

ThreadPool::ThreadPool(size_t threads):m_threads(threads){
    for(size_t i=0;i<m_threads;++i){
        m_workers.emplace_back(
            [this]{
                for(;;){
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(this->m_queue_mutex);
                        this->m_condition.wait(lock,[this]{return this->m_stop || !this->m_tasks.empty();});
                        if(this->m_stop && this->m_tasks.empty()){
                            return;
                        }
                        task = std::move(this->m_tasks.front());
                        this->m_tasks.pop();
                    }
                    task();
                }
            }
        );
    }
}

ThreadPool::~ThreadPool(){
    {
        std::unique_lock<std::mutex> lock(m_queue_mutex);
        m_stop = true;
    }
    m_condition.notify_all();
    for(std::thread &worker : m_workers){
        worker.join();
    }
}
