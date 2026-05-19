#include "timer_manager.h"

// 构造函数：原子变量 next_id_ 从 1 开始计数
TimerManager::TimerManager() : next_id_(1) {}

size_t TimerManager::m_AddTimer(size_t delay_ms, TimerCallBack cb) {
    // 1. 计算出任务触发的绝对时间点：当前时间 + 延时毫秒
    TimerPoint expire = Clock::now() + std::chrono::milliseconds(delay_ms);
    // 2. 获取唯一ID，fetch_add 保证多线程并发下ID不重复，memory_order_relaxed 追求极致性能
    size_t id = next_id_.fetch_add(1, std::memory_order_relaxed);

    // 3. 开启 RAII 锁，保护对 set 和 map 的修改操作
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 4. 将任务节点构造并插入到红黑树中，O(log N)
    timers_.insert({expire, id, std::move(cb)});
    // 5. 在哈希表中记录该 ID 对应的超时时间，为 O(log N) 的高效取消做准备
    lookup_[id] = expire; 
    
    // 6. 返回生成的ID，外部保存以便后续手动取消
    return id;
}

bool TimerManager::m_CancelTimer(size_t timer_id) {
    // 1. 开启 RAII 锁
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 2. 先通过哈希表 O(1) 查找该任务是否存在
    auto it = lookup_.find(timer_id);
    if (it == lookup_.end()) {
        // 如果不在哈希表中，说明任务已经被执行或者ID非法，返回失败
        return false; 
    }

    // 3. 查出超时时间，构造一个 Dummy（傀儡）节点。
    // 注：std::set 的 erase 只认排序键（expire_time 和 id），不需要真实的回调函数
    TimerNode dummy{it->second, timer_id, nullptr};
    
    // 4. 利用傀儡节点从红黑树中精准剔除原任务，O(log N)
    timers_.erase(dummy);
    
    // 5. 从哈希表中清理该记录
    lookup_.erase(it);
    
    // 6. 返回取消成功
    return true;
}

int TimerManager::m_GetNextTimeout() {
    // 1. 开启 RAII 锁读取树的头部
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (timers_.empty()) {
        // 2. 没有任何定时任务，返回 -1。在 epoll_wait 中 -1 代表无限期阻塞，直到有 IO 事件唤醒
        return -1; 
    }

    // 3. 获取当前时刻
    auto now = Clock::now();
    // 4. std::set 的 begin() 是 O(1) 操作，获取最快超时的那个任务的时间点
    auto expire = timers_.begin()->expire_time;
    
    if (expire <= now) {
        // 5. 如果已经有任务超时，返回 0，指示 epoll_wait 立即返回，不要阻塞
        return 0; 
    }

    // 6. 计算差值，转换为毫秒并强制转换为 int，供系统调用使用
    auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(expire - now).count();
    return static_cast<int>(diff);
}

void TimerManager::m_Tick() {
    // 1. 本地动态数组，用于把所有超时的回调函数“搬运”出锁的保护区
    std::vector<TimerCallBack> expired_callbacks;
    
    {
        // 2. 开启 RAII 锁，作用域被限定在这一对大括号内 (重点优化)
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = Clock::now();

        // 3. 循环检查树的最左侧节点
        while (!timers_.empty()) {
            auto it = timers_.begin();
            if (it->expire_time > now) {
                // 4. 由于是红黑树排序，如果最小的时间还没超时，后面的肯定也没超时，直接跳出循环
                break; 
            }

            // 5. 任务已超时，将其回调函数通过移动语义（零拷贝）压入本地数组
            expired_callbacks.emplace_back(std::move(it->cb));
            
            // 6. 同步清理哈希表中的记录
            lookup_.erase(it->id);
            
            // 7. 从红黑树中删除该任务节点
            timers_.erase(it);
        }
    } // 8. 大括号结束，RAII 自动释放互斥锁！

    // 9. [多线程极限优化]：在锁的作用域之外执行回调函数！
    // 如果带着锁执行回调，一旦回调内部包含非常耗时的逻辑，其他线程的 Add/Cancel 将全部阻塞；
    // 更致命的是，如果回调逻辑内部又调用了 AddTimer()，会导致死锁 (Deadlock)。剥离执行完美解决此问题。
    for (const auto& cb : expired_callbacks) {
        if (cb) {
            cb(); // 执行用户定义的业务逻辑
        }
    }
}