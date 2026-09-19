#include <iostream>
#include <cassert>
#include <atomic>
#include <algorithm>
#include <new>
#include <utility>

template <typename T>
class MemoryPool {

public:
    MemoryPool(size_t capacity) : capacity_(capacity) {
        for (int i = 0; i < capacity_; ++i) {
            Block* node = reinterpret_cast<Block*>(storage_ + i * aligned_size);
            node->next = head;
            head = node;
        }
    }

    template <typename... Args>
    T* allocate(Args&&... args) {
        if (!head) return nullptr;
        Block* block = head;
        head = head->next;
        T* ptr = reinterpret_cast<T*>(block);
        new (ptr) T(std::forward<Args>(args)...);
        return ptr;
    }

    void deallocate(T* ptr) {
        ptr->~T();
        Block* block = reinterpret_cast<Block*>(ptr);
        block->next = head;
        head = block;
    }

private:
    size_t capacity_;
    struct Block {
        Block* next;
    };

    Block* head = nullptr;

    char padding[8];

    static constexpr size_t block_size = std::max(sizeof(T), sizeof(Block));
    static constexpr size_t block_alignment = std::max(alignof(T), alignof(Block));
    static constexpr size_t aligned_size = ((block_size + block_alignment - 1) / block_alignment) * block_alignment;
    alignas(block_alignment) char storage_[capacity_ * aligned_size];
};