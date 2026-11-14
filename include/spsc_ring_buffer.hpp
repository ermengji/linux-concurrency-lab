// SPDX-License-Identifier: MIT
//
// SpscRingBuffer: a single-producer / single-consumer lock-free ring buffer.
//
// Design notes:
//   * Capacity is rounded up to a power of two so the modulo becomes a mask.
//   * Producer only writes head_, consumer only writes tail_, so no CAS loop is
//     needed - a plain store with release semantics is enough.
//   * Each side caches the other side's index to avoid hammering the shared
//     cache line on every operation.
//   * head_ and tail_ are placed on separate cache lines (alignas(64)) to avoid
//     false sharing.
//   * Indices are unsigned and allowed to wrap around; the distance is always
//     computed as (a - b) which stays correct under wrap-around.

#pragma once

#include <atomic>
#include <cstddef>
#include <vector>

namespace lab {

namespace detail {
inline constexpr std::size_t round_up_pow2(std::size_t v) {
    if (v < 2) return 2;
    --v;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v |= v >> 32;
    return v + 1;
}
}  // namespace detail

// T must be default constructible and trivially copyable-friendly (we store by
// value). For heavy objects, store a pointer/slot index instead.
template <typename T>
class SpscRingBuffer {
public:
    explicit SpscRingBuffer(std::size_t capacity)
        : capacity_(detail::round_up_pow2(capacity)),
          mask_(capacity_ - 1),
          buffer_(capacity_) {}

    SpscRingBuffer(const SpscRingBuffer&) = delete;
    SpscRingBuffer& operator=(const SpscRingBuffer&) = delete;

    // Producer side. Returns false when full (drop the item, caller decides).
    bool try_push(const T& value) {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        // Only fetch the consumer index when we might be full.
        if (head - tail_cache_ == capacity_) {
            tail_cache_ = tail_.load(std::memory_order_acquire);
            if (head - tail_cache_ == capacity_) return false;
        }
        buffer_[head & mask_] = value;
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    // Consumer side. Returns false when empty.
    bool try_pop(T& out) {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_cache_) {
            head_cache_ = head_.load(std::memory_order_acquire);
            if (tail == head_cache_) return false;
        }
        out = buffer_[tail & mask_];
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    std::size_t capacity() const { return capacity_; }

    // Approximate size; only exact when called from the consumer side.
    std::size_t size() const {
        return head_.load(std::memory_order_acquire) -
               tail_.load(std::memory_order_acquire);
    }

    bool empty() const { return size() == 0; }

private:
    const std::size_t capacity_;
    const std::size_t mask_;
    std::vector<T> buffer_;

    // Producer-owned index, padded to its own cache line.
    alignas(64) std::atomic<std::size_t> head_{0};
    // Consumer-owned index, padded to its own cache line.
    alignas(64) std::atomic<std::size_t> tail_{0};
    // Private caches of the other side's index.
    alignas(64) std::size_t tail_cache_{0};
    alignas(64) std::size_t head_cache_{0};
};

}  // namespace lab
