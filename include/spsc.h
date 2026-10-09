#include <memory>
#include <thread>
#include <cstddef>
#include <atomic>
#include <new>
#include <utility>

template<typename T, size_t Capacity, typename Allocator = std::allocator<T>>
class SPSC {
    static_assert(Capacity >= 1, "Capacity must bigger than 1");
    static constexpr size_t capacity_ = Capacity + 1;

    using alloc_traits = std::allocator_traits<Allocator>;
public:
    explicit SPSC(const Allocator allocator = Allocator()) // default construct an allocator;
    : allocator_(allocator) { 
        slots_ = alloc_traits::allocate(allocator_, capacity_);
    }

    ~SPSC noexcept() {
        alloc_traits::deallocate(allocator_, slots_, capacity_);
    }

    template<typename... Args>
    bool try_emplace noexcept(Args&&... args) {
        auto writeId = writeId_.load(std::memory_order_relaxed);
        auto next = writeId + 1;
        if (next == capacity_) {
            next = 0;
        }
        if (next == readCache_) {
            readCache_ = readId_.load(std::memory_order_acquire);
            if (next == readCache_) return false;
        }
        new (&slots_[writeId]) T(std::forward<Args>(args)...);
        writeId_.store(next, std::memory_order_release);
        return true;
    }

    bool try_push noexcept(T& item) {
        return try_emplace(item);
    }

    bool try_pop noexcept() {
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
    alignas(64) std::atomic<size_t> writeId_{0}; // false sharing
    size_t writeCache_{0}; // coherenc traffic
    alignas(64) std::atomic<size_t> readId_{0};
    size_t readCache_{0};
};
