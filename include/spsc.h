#pragma once

#include <memory>
#include <thread>
#include <cstddef>
#include <atomic>
#include <limits>
#include <new>
#include <type_traits>
#include <utility>

template<typename T, size_t Capacity, typename Allocator = std::allocator<T>>
class SPSC {
    static_assert(Capacity >= 1, "Capacity must be at least 1");
    static_assert(Capacity < std::numeric_limits<size_t>::max(), "Capacity is too large");
    static_assert(std::is_nothrow_destructible_v<T>, "T must be nothrow destructible");
    static constexpr size_t capacity_ = Capacity + 1;

    using alloc_traits = std::allocator_traits<Allocator>;
public:
    explicit SPSC(const Allocator allocator = Allocator())
    : allocator_(allocator) {
        slots_ = alloc_traits::allocate(allocator_, capacity_);
    }

    SPSC(const SPSC&) = delete;
    SPSC& operator=(const SPSC&) = delete;

    // The producer and consumer must have stopped before destruction.
    ~SPSC() noexcept {
        while (try_pop()) {}
        alloc_traits::deallocate(allocator_, slots_, capacity_);
    }

    // Producer only.
    template<typename... Args>
    bool try_emplace(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args&&...>) {
        auto writeId = writeId_.load(std::memory_order_relaxed);
        auto next = writeId + 1;
        if (next == capacity_) {
            next = 0;
        }
        if (next == readCache_) {
            readCache_ = readId_.load(std::memory_order_acquire);
            if (next == readCache_) return false;
        }
        ::new (static_cast<void*>(slots_ + writeId)) T(std::forward<Args>(args)...);
        writeId_.store(next, std::memory_order_release);
        return true;
    }

    bool try_push(const T& item) noexcept(std::is_nothrow_copy_constructible_v<T>) {
        return try_emplace(item);
    }

    bool try_push(T&& item) noexcept(std::is_nothrow_move_constructible_v<T>) {
        return try_emplace(std::move(item));
    }

    template<typename P>
    void push(P&& item) noexcept(std::is_nothrow_constructible_v<T, P&&>) {
        while (!try_emplace(std::forward<P>(item))) {
            std::this_thread::yield();
        }
    }

    // Consumer only. The returned pointer remains valid until try_pop().
    T* front() noexcept {
        auto readId = readId_.load(std::memory_order_relaxed);
        if (readId == writeCache_) {
            writeCache_ = writeId_.load(std::memory_order_acquire);
            if (readId == writeCache_) return nullptr;
        }
        return slots_ + readId;
    }

    bool try_pop() noexcept {
        auto readId = readId_.load(std::memory_order_relaxed);
        if (readId == writeCache_) {
            writeCache_ = writeId_.load(std::memory_order_acquire);
            if (readId == writeCache_) return false;
        }
        slots_[readId].~T();
        auto next = readId + 1;
        if (next == capacity_) {
            next = 0;
        }
        readId_.store(next, std::memory_order_release);
        return true;
    }

private:
    T* slots_;
    Allocator allocator_;
    alignas(64) std::atomic<size_t> writeId_{0};
    size_t readCache_{0}; // Producer's cached consumer position.
    alignas(64) std::atomic<size_t> readId_{0};
    size_t writeCache_{0}; // Consumer's cached producer position.
};
