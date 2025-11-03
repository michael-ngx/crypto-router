#pragma once
#include <atomic>
#include <cstddef>
#include <type_traits>
#include <utility>
#include <new>

// Single-Producer / Single-Consumer ring buffer.
// - CapacityPow2 must be a power-of-two (e.g., 1024).
// - SPSC: exactly one producer thread calls try_push,
//         exactly one consumer thread calls try_pop.
//
// This implementation stores T in-place and uses head/tail indices
// with wrap-around via bitmasking (fast, branch-light).
template <typename T, std::size_t CapacityPow2>
class SpscRing {
    static_assert((CapacityPow2 & (CapacityPow2 - 1)) == 0, "Capacity must be power of two");

public:
    SpscRing() : head_(0), tail_(0) {
        // value-initialize storage to keep things simple for trivially default-constructible T
        for (std::size_t i = 0; i < CapacityPow2; ++i) {
            new (&buf_[i]) T();
        }
    }
    ~SpscRing() {
        for (std::size_t i = 0; i < CapacityPow2; ++i) {
            reinterpret_cast<T*>(&buf_[i])->~T();
        }
    }

    // Non-copyable
    SpscRing(const SpscRing&) = delete;
    SpscRing& operator=(const SpscRing&) = delete;

    // Producer: attempt to push; returns false if full (caller decides policy)
    bool try_push(T&& v) {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t next = (head + 1) & mask_;
        if (next == tail_.load(std::memory_order_acquire)) {
            // full
            return false;
        }
        *ptr(head) = std::move(v);
        head_.store(next, std::memory_order_release);
        return true;
    }

    // Consumer: attempt to pop; returns false if empty
    bool try_pop(T& out) {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) {
            // empty
            return false;
        }
        out = std::move(*ptr(tail));
        tail_.store((tail + 1) & mask_, std::memory_order_release);
        return true;
    }

    bool empty() const {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }
    bool full() const {
        const std::size_t next = (head_.load(std::memory_order_acquire) + 1) & mask_;
        return next == tail_.load(std::memory_order_acquire);
    }
    std::size_t capacity() const { return CapacityPow2 - 1; } // one slot unused to disambiguate full/empty

private:
    using Storage = typename std::aligned_storage<sizeof(T), alignof(T)>::type;

    T* ptr(std::size_t i) { return reinterpret_cast<T*>(&buf_[i]); }
    const T* ptr(std::size_t i) const { return reinterpret_cast<const T*>(&buf_[i]); }

    static constexpr std::size_t mask_ = CapacityPow2 - 1;
    Storage buf_[CapacityPow2];
    std::atomic<std::size_t> head_; // producer writes
    std::atomic<std::size_t> tail_; // consumer writes
};