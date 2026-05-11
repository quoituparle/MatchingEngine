#include <vector>
#include <iostream>
#include <thread>
#include <algorithm>
#include <cstdint>
#include <malloc.h>
#include <atomic>
#include <map>
#include <unordered_map>
#include <queue>
#include <chrono>
#include <SPSCQueue.h>

enum struct Side{ Buy, Sell };
enum struct Type{ Market, Limit, PostOnly};

struct Block{
    Block* next;
};

// for Lockless ABA
struct alignas(16) Tagged{ 
    Block* ptr;
    uintptr_t version;
};

template<typename T>
class MemoryPool{
private:
    std::atomic<Tagged> freeHead;
    char* memoryChunk;
public:
    MemoryPool(size_t objectCount) {
        size_t blockSize = sizeof(T);
        size_t totalBytes = objectCount * blockSize;

        void* rawPtr = _aligned_malloc(totalBytes, 64);
        if (!rawPtr) throw std::bad_alloc();

        memoryChunk = static_cast<char*>(rawPtr);
        Block* head = reinterpret_cast<Block*>(memoryChunk);
        Block* current = head;

        for (size_t i = 0; i < objectCount - 1; ++i) {
            char* nextAddress = reinterpret_cast<char*>(current) + blockSize;
            current->next = reinterpret_cast<Block*>(nextAddress);
            current = current->next;
        }
        current->next = nullptr;

        Tagged initHead; // For Lock-free initializing
        initHead.ptr = head;
        initHead.version = 0;

        freeHead.store(initHead);
    };

    ~MemoryPool(){
        _aligned_free(memoryChunk);
    };

    T* allocate() { // CAS process
        Tagged oldHead = freeHead.load();
        while (true) {
            Tagged newHead;
            newHead.ptr = oldHead.ptr->next;
            newHead.version = oldHead.version + 1;

            if (freeHead.compare_exchange_weak(oldHead, newHead)) {
                return reinterpret_cast<T*>(oldHead.ptr);
            }
        }
    };

    void deallocate(T* p) {
        if (!p) return;
        Block* BlockToRecycle = reinterpret_cast<Block*>(p);
        Tagged oldHead = freeHead.load();

        while (true) {
            BlockToRecycle->next = oldHead.ptr;

            Tagged newHead;
            newHead.ptr = BlockToRecycle;
            newHead.version = oldHead.version + 1;

            if (freeHead.compare_exchange_weak(oldHead, newHead)) {
                break;
            }
        }
    };
};

struct Order{
    uint64_t id;
    uint64_t price;
    uint64_t qty;
    uint64_t time;
    Side side;
    Type type;
    Order* next;
    Order* prev;
};

struct Queue{
    Order* head = nullptr;
    Order* tail = nullptr;

    // FIFO
    // oN(Tail) -> ... -> o3 -> o2 -> o1(Head)
    void intrusive_push_back(Order* order) { // Use intrusive list instead of original double list;
        order->next = nullptr;
        if (!tail) {
            head = tail = order;
            order->prev = nullptr;
        } else {
            tail->next = order;
            order->prev = tail;
            tail = order;
        }
    };

    void remove(Order* order) { // to replace pop()
        if (order->prev) {
            order->prev->next = order->next;
        } else head = order->next;
        if (order->next) {
            order->next->prev = order->prev;
        } else tail = order->prev;
    }

    bool empty() const {
        return head == nullptr;
    }
};

class MatchingEngine {
private:
    MemoryPool<Order> pool;
    std::map<uint64_t, Queue, std::greater<uint64_t>> bids; // highest buy
    std::map<uint64_t, Queue, std::less<uint64_t>> asks; // lowest sell

    std::unordered_map<uint64_t, Order*> orderIndex; // for O(1) search and cancel shares.

    uint64_t TimeStamp(){
        return static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    };

    uint64_t nextId = 1;

    uint64_t MakeId() {
        return nextId++;
    };

    void LimitSubmit(uint64_t price, uint64_t qty, Side side, Type type = Type::Limit) {
        uint64_t orderId = MakeId();
        if (side == Side::Buy) {
            while (qty > 0 && !asks.empty() && asks.begin()->first <= price) {
                auto& queue = asks.begin()->second;
                Order* resting = queue.head;
                uint64_t excuted = std::min(resting->qty, qty);

                qty-=excuted;
                resting->qty-=excuted;

                if (resting->qty == 0) {
                    orderIndex.erase(resting->id);
                    queue.remove(resting);
                    pool.deallocate(resting);
                }
                if (queue.empty()) bids.erase(bids.begin());
            };
            if (qty > 0 && type == Type::Limit) {
                Order* order = pool.allocate();
                new (order) Order{orderId, price, qty, TimeStamp(), side, type, nullptr, nullptr};
                bids[price].intrusive_push_back(order);
                orderIndex[order->id] = order;
            };
        } else {
            while (qty > 0 && !bids.empty() && bids.begin()->first >= price) {
                auto& queue = bids.begin()->second;
                Order* resting = queue.head;
                uint64_t excuted = std::min(resting->qty, qty);

                qty-=excuted;
                resting->qty-=excuted;

                if (resting->qty == 0) {
                    orderIndex.erase(resting->id);
                    queue.remove(resting);
                }
                if (queue.empty()) asks.erase(bids.begin());
            };
            if (qty > 0 && type == Type::Limit) {
                Order* order = pool.allocate();
                new (order) Order{orderId, price, qty, TimeStamp(), side, type, nullptr, nullptr};
                asks[price].intrusive_push_back(order);
                orderIndex[order->id] = order;
            }
        };
    };

    void MarketSubmit(uint64_t qty, Side side) {
        if (side == Side::Buy) {
            if (asks.empty()) return;
            LimitSubmit(UINT64_MAX, qty, side, Type::Market);
        } else {
            if (bids.empty()) return;
            uint64_t price = bids.begin()->first;
            LimitSubmit(0, qty, side, Type::Market);
        }
    };

    void PostOnly(uint64_t price, uint64_t qty, Side side) {
        Type type = Type::PostOnly;
        uint64_t Id = MakeId();
        if (side == Side::Buy) {
            if (!asks.empty() && asks.begin()->first <= price) return;

            Order* order = pool.allocate();
            new (order) Order{Id, price, qty, TimeStamp(), side, type, nullptr, nullptr};
            bids[price].intrusive_push_back(order);
            orderIndex[order->id] = order;
        } else {
            if (!bids.empty() && bids.begin()->first >= price) return;

            Order* order = pool.allocate();
            new (order) Order{Id, price, qty, TimeStamp(), side, type, nullptr, nullptr};
            asks[price].intrusive_push_back(order);
            orderIndex[order->id] = order;
        }
    };



public:
    MatchingEngine(size_t poolSize = 100000) : pool(poolSize) {}

    void Submit(uint64_t price, uint64_t qty, Side side, Type type) {
        if (type == Type::Limit) LimitSubmit(price, qty, side, type);
        if (type == Type::Market) MarketSubmit(qty, side);
        if (type == Type::PostOnly) PostOnly(price, qty, side);
    };

    void CancelOrder(uint64_t id) {
        auto it = orderIndex.find(id);
        if (it == orderIndex.end()) return;
        
        Order* target = it->second;
        orderIndex.erase(it);

        if (target->side == Side::Buy) {
            bids[target->price].remove(target);
            if (bids[target->price].empty()) bids.erase(target->price);
        } else {
            asks[target->price].remove(target);
            if (asks[target->price].empty()) asks.erase(target->price);
        }
        
        pool.deallocate(target);
    }

    void PrintBooks() {
        for (auto i = asks.rbegin(); i != asks.rend(); ++i) {
            Order* current = i->second.head;
            while (current) {
                std::cout << "ID: " << current->id << " Price: " << current->price 
                          << " $ Qty: " << current->qty << '\n';
                current = current->next;
            }
        }
        std::cout << "Asks: \n     ----------------\nBits: \n";

        for (auto i = bids.begin(); i != bids.end(); ++i) {
            Order* current = i->second.head;
            while (current) {
                std::cout << "ID: " << current->id << " Price: " << current->price 
                          << " $ Qty: " << current->qty << '\n';
                current = current->next;
            }
        }
    }
};



int main(){
    MatchingEngine engine;
    for (int price = 90; price < 100; ++price) {
        engine.Submit(price, 10, Side::Buy, Type::Limit);
    };

    for (int price = 100; price < 111; ++price) {
        engine.Submit(price, 10, Side::Sell, Type::Limit);
    };

    engine.CancelOrder(6);
    engine.Submit(103, 5, Side::Buy, Type::PostOnly);

    engine.PrintBooks();
    return 0;
    
}