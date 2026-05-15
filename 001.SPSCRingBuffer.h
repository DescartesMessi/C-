#ifndef SPSC_RINGBUFFER_HPP
#define SPSC_RINGBUFFER_HPP

#include <vector>
#include <atomic>
#include <cassert>
#include <stdexcept>

// 模板定义，必须在头文件中完整实现
template <typename T, size_t Capacity>
class SPSC_RingBuffer {
    // 修正：增加括号以保证按位与运算的优先级
    static_assert(Capacity > 1 && ((Capacity & (Capacity - 1)) == 0),   "Capacity must be a power of 2");

private:
    static constexpr size_t m_Mask = Capacity - 1;
    
    // 底层容器，使用 vector 代替 C 数组，利用 RAII 管理内存
    std::vector<T> m_buffer;
    
    // alignas(64) 隔离缓存行，消除多核 CPU 间的伪共享 (False Sharing)
    alignas(64) std::atomic<size_t> m_head {0};
    alignas(64) std::atomic<size_t> m_tail {0};

public:
    SPSC_RingBuffer() : m_head(0), m_tail(0) {
        // 使用 resize 会调用 T 的默认构造函数进行初始化
        m_buffer.resize(Capacity);
    }

    // 禁用拷贝语义，避免指针/原子变量状态混乱
    SPSC_RingBuffer(const SPSC_RingBuffer&) = delete;
    SPSC_RingBuffer& operator=(const SPSC_RingBuffer&) = delete;

    /**
     * @brief 生产者入队操作
     */
    bool push(const T& data) {
        // 获取当前写指针，使用 relaxed 因为写指针只由当前线程（生产者）修改
        size_t curr_tail = m_tail.load(std::memory_order_relaxed);
        size_t next_tail = (curr_tail + 1) & m_Mask; // 普通局部 size_t 即可

        // 检查队列是否已满：当下一个写位置追上读位置时即为满
        // 使用 acquire 与消费者更新 m_head 时的 release 语义形成同步
        if (next_tail == m_head.load(std::memory_order_acquire)) {
            return false;
        }

        // 写入数据
        m_buffer[curr_tail] = data;

        // 更新写指针：使用 release 确保 data 的写入动作在修改 tail 之前生效，
        // 且对消费者线程可见。
        m_tail.store(next_tail, std::memory_order_release);
        return true;
    }

    /**
     * @brief 消费者出队操作
     */
    bool pop(T& val) {
        // 获取当前读指针，使用 relaxed 因为读指针只由当前线程（消费者）修改
        size_t curr_head = m_head.load(std::memory_order_relaxed);

        // 检查队列是否为空：读写指针相等即为空
        // 使用 acquire 确保能看见生产者在写入 data 后更新的 tail
        if (curr_head == m_tail.load(std::memory_order_acquire)) {
            return false;
        }

        // 读出数据 (使用 std::move 优化对象转移开销)
        val = std::move(m_buffer[curr_head]);

        // 更新读指针：使用 release 确保数据被读取完成的动作对生产者可见
        m_head.store((curr_head + 1) & m_Mask, std::memory_order_release);
        return true;
    }

    bool isEmpty() const {
        // 判空操作：由于可能跨线程调用，最好保持 acquire 语义，或者统一 relaxed(会有微小的一致性延迟)
        return m_head.load(std::memory_order_acquire) == m_tail.load(std::memory_order_acquire);
    }
};

#endif // SPSC_RINGBUFFER_HPP