#ifndef TIMER_MANAGER_H
#define TIMER_MANAGER_H 

#include <chrono>        // C++11 时间库，用于精准的时间点与时间段计算
#include <functional>    // 用于封装通用的回调函数 std::function
#include <set>           // 底层为红黑树，用于按超时时间自动排序任务
#include <unordered_map> // 底层为哈希表，用于通过ID快速定位任务进行删除
#include <mutex>         // 互斥锁，保障多线程环境下的结构安全
#include <atomic>        // 原子变量，用于生成线程安全的无锁全局唯一自增ID
#include <vector>        // 动态数组，用于暂存已超时的回调函数以剥离锁

class TimerManager{
public:
    using Clock  = std::chrono::steady_clock;
    using TimerPoint = Clock::time_point;
    using TimerCallBack = std::function<void()>;
private:
    
    
    std::unordered_map<size_t, TimerPoint> lookup_; // 辅助容器：映射 ID -> ExpireTime，用于 O(1) 定位
    std::atomic<size_t> next_id_;                  // 原子计数器：生成下一个定时器ID，无锁并发安全
    std::mutex mutex_;// 互斥锁：保护 timers_ 和 lookup_ 在多线程下的安全    
// 内部数据结构：定时器任务节点
    struct TimerNode {
        TimerPoint expire_time;   // 一级排序键：任务应当触发的绝对时间点
        size_t id;             // 二级排序键：全局唯一ID，解决同一时刻的排序冲突
        TimerCallBack cb;        // 任务实体：达到触发条件时执行的代码逻辑

        // 必须重载 '<' 运算符，以便 std::set 能够构建正确的红黑树并维持严格弱序
        bool operator<(const TimerNode& other) const {
            if (expire_time == other.expire_time) {
                // 如果超时时间完全一致，比较唯一ID，先添加的排在前面
                return id < other.id;
            }
            // 默认按超时时间升序排列，最先超时的在最左下方的叶子或根(begin处)
            return expire_time < other.expire_time;
        }
    };
    std::set<TimerNode> timers_;                     // 核心容器：保存所有待执行的任务，自动排序
public:
    

    TimerManager();
    ~TimerManager() = default;

    TimerManager(const TimerManager&)=delete;
    TimerManager& operator=(const TimerManager&)=delete;

    size_t m_AddTimer(size_t delay_ms, TimerCallBack cb);

    bool m_CancelTimer(size_t m_timer_id);

    int m_GetNextTimeout();

    void m_Tick();

};

#endif // TIMER_MANAGER_H
