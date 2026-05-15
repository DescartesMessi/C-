#include <atomic>
#include <cstddef>
#include <utility>
#include <stdexcept>
#include <type_traits>

// 缓存行大小，通常 x86-64 架构下为 64 字节
constexpr size_t CACHE_LINE_SIZE = 64;

template <typename T>
class LockFreeMPMCQueue {
private:
    struct Cell {
        std::atomic<size_t> sequence;
        T data;
    };

    // 内存对齐，避免伪共享 (False Sharing)
    // padding 确保 buffer_、mask_、head_idx_、tail_idx_ 分别位于不同的 Cache Line
    alignas(CACHE_LINE_SIZE) Cell*  buffer_;
    alignas(CACHE_LINE_SIZE) size_t mask_;
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> head_idx_;
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> tail_idx_;

public:
    explicit LockFreeMPMCQueue(size_t capacity) {
        // 容量必须是 2 的幂次，以使用位与运算替代取模运算
        if (capacity < 2 || (capacity & (capacity - 1)) != 0) {
            throw std::invalid_argument("Capacity must be a power of 2");
        }
        
        mask_ = capacity - 1;
        buffer_ = new Cell[capacity];
        
        for (size_t i = 0; i < capacity; ++i) {
            buffer_[i].sequence.store(i, std::memory_order_relaxed);
        }
        
        head_idx_.store(0, std::memory_order_relaxed);
        tail_idx_.store(0, std::memory_order_relaxed);
    }

    ~LockFreeMPMCQueue() {
        delete[] buffer_;
    }

    // 禁用拷贝与移动，队列持有的资源在并发语义下不应被转移
    LockFreeMPMCQueue(const LockFreeMPMCQueue&) = delete;
    LockFreeMPMCQueue& operator=(const LockFreeMPMCQueue&) = delete;

    bool push(const T& data) {
        Cell* cell;
        size_t pos = tail_idx_.load(std::memory_order_relaxed);
        
        for (;;) {
            cell = &buffer_[pos & mask_];
            size_t seq = cell->sequence.load(std::memory_order_acquire);
            intptr_t dif = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);
            
            if (dif == 0) {
                // 槽位就绪，且尚未被其他生产者抢占。尝试 CAS 推进全局 tail 索引
                if (tail_idx_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    break;
                }
            } else if (dif < 0) {
                // 当前 tail 追上了 head，队列已满
                return false; 
            } else {
                // dif > 0 说明被其他生产者抢占，重新加载最新的 tail
                pos = tail_idx_.load(std::memory_order_relaxed);
            }
        }
        
        cell->data = data;
        // 写入完毕，修改 sequence 唤醒消费者，Release 语义保证 data 写入不被重排到后面
        cell->sequence.store(pos + 1, std::memory_order_release);
        return true;
    }

    bool pop(T& data) {
        Cell* cell;
        size_t pos = head_idx_.load(std::memory_order_relaxed);
        
        for (;;) {
            cell = &buffer_[pos & mask_];
            size_t seq = cell->sequence.load(std::memory_order_acquire);
            intptr_t dif = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);
            
            if (dif == 0) {
                // 数据已写入完毕，且尚未被其他消费者抢占。尝试 CAS 推进全局 head 索引
                if (head_idx_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    break;
                }
            } else if (dif < 0) {
                // 当前 head 追上了 tail，队列为空
                return false; 
            } else {
                // dif > 0 说明被其他消费者抢占，重新加载最新的 head
                pos = head_idx_.load(std::memory_order_relaxed);
            }
        }
        
        data = std::move(cell->data);
        // 读取完毕，将 sequence 拨到下一圈，释放槽位给生产者
        cell->sequence.store(pos + mask_ + 1, std::memory_order_release);
        return true;
    }
};