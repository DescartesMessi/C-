#ifndef LOCK_FREE_MPMC_QUEUE_HPP
#define LOCK_FREE_MPMC_QUEUE_HPP

#include <atomic>
#include <cstddef>
#include <stdexcept>
#include <utility>

// 默认 Cache Line 大小，用于消除伪共享
#if defined(__cpp_lib_hardware_interference_size)
    constexpr size_t CACHE_LINE = std::hardware_destructive_interference_size;
#else
    constexpr size_t CACHE_LINE = 64;
#endif

template <typename T>
class LockFreeMPMCQueue {
private:
    struct Cell {
        std::atomic<size_t> sequence;
        T data;
    };

    // 内存隔离：防止 buffer_ 指针、mask_ 和读写索引在多核间发生伪共享
    alignas(CACHE_LINE) Cell*  buffer_;
    alignas(CACHE_LINE) size_t mask_;
    alignas(CACHE_LINE) std::atomic<size_t> head_idx_;
    alignas(CACHE_LINE) std::atomic<size_t> tail_idx_;

public:
    explicit LockFreeMPMCQueue(size_t capacity) {
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
                if (tail_idx_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    break;
                }
            } else if (dif < 0) {
                return false; 
            } else {
                pos = tail_idx_.load(std::memory_order_relaxed);
            }
        }
        
        cell->data = data;
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
                if (head_idx_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    break;
                }
            } else if (dif < 0) {
                return false; 
            } else {
                pos = head_idx_.load(std::memory_order_relaxed);
            }
        }
        
        data = std::move(cell->data);
        cell->sequence.store(pos + mask_ + 1, std::memory_order_release);
        return true;
    }
};

#endif // LOCK_FREE_MPMC_QUEUE_HPP