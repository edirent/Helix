#pragma once

#include <atomic>
#include <cstddef>
#include <optional>
#include <vector>

namespace helix::utils {

/**
 * Minimal lock-free single-producer single-consumer ring buffer.
 */
template <typename T>
class RingBuffer {
  public:
    explicit RingBuffer(std::size_t capacity = 1024)
        : capacity_(capacity), data_(capacity), head_(0), tail_(0) {}

    bool push(const T &value) {
        auto head = head_.load(std::memory_order_relaxed);
        auto next = increment(head);
        if (next == tail_.load(std::memory_order_acquire)) {
            return false;  // full
        }
        data_[head] = value;
        head_.store(next, std::memory_order_release);
        return true;
    }

    std::optional<T> pop() {
        auto tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) {
            return std::nullopt;  // empty
        }
        auto value = data_[tail];
        tail_.store(increment(tail), std::memory_order_release);
        return value;
    }

    bool empty() const { return head_.load() == tail_.load(); }
    std::size_t capacity() const { return capacity_; }

  private:
    std::size_t increment(std::size_t idx) const { return (idx + 1) % capacity_; }

    const std::size_t capacity_;
    std::vector<T> data_;
    std::atomic<std::size_t> head_;
    std::atomic<std::size_t> tail_;
};

}  // namespace helix::utils
