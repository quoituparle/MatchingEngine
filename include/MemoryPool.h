#include <vector>
#include <iostream>
#include <thread>
#include <algorithm>
#include <cstdint>
#include <malloc.h>
#include <atomic>

template <typename T>
class MemoryPool {
private:
    struct Node {
        Node* next;
    };
    Node* ptr;
    Node* head;
    char* memoryChunk;
public:
    MemoryPool(size_t objectCount) {
        size_t blockSize = sizeof(T);
        size_t totalBytes = objectCount * blockSize;

        void* rawPtr = _aligned_malloc(totalBytes, 64);
        memoryChunk = static_cast<char*>(rawPtr);
        head = reinterpret_cast<Node*>(memoryChunk);
        Node* current = head;

        for (size_t i = 0; i < objectCount - 1; ++i) {
            char* nextAddresse = reinterpret_cast<char*>(current) + blockSize;
            current->next = reinterpret_cast<Node*>(nextAddresse);
            current = current->next;
        }
        current->next = nullptr;
    };

    ~MemoryPool() {
        _aligned_free(memoryChunk);
    };

    T* allocate() {
        if (!head) {
            return nullptr;
        }

        Node* blockToGive = head;
        head = head->next;
        return blockToGive;
    }

    void deallocate(void* p) {
        Node* node = reinterpret_cast<Node*>(p);

        node->next = head;
        head = node;
    }
};