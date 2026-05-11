#include <iostream>
#include <atomic>
#include <map>
#include <unordered_map>
#include <chrono>
#include <cstdint>
#include <malloc.h>
#include <algorithm>

enum struct Side { Buy, Sell };
enum struct Type { Market, Limit, PostOnly };

struct Block {
    Block* next;
};

struct alignas(16) Tagged {
    Block* ptr;
    uintptr_t version;
};

template<typename T>
class MemoryPool {
private:
    std::atomic<Tagged> freeHead;
    char* memoryChunk;
public:
    MemoryPool(size_t objectCount) {
        size_t blockSize = sizeof(T);
        void* rawPtr = _aligned_malloc(objectCount * blockSize, 64);
        if (!rawPtr) throw std::bad_alloc();

        memoryChunk = static_cast<char*>(rawPtr);
        Block* current = reinterpret_cast<Block*>(memoryChunk);

        for (size_t i = 0; i < objectCount - 1; ++i) {
            current->next = reinterpret_cast<Block*>(reinterpret_cast<char*>(current) + blockSize);
            current = current->next;
        }
        current->next = nullptr;

        Tagged initHead{reinterpret_cast<Block*>(memoryChunk), 0};
        freeHead.store(initHead);
    }

    ~MemoryPool() {
        _aligned_free(memoryChunk);
    }

    T* allocate() {
        Tagged oldHead = freeHead.load();
        while (true) {
            if (!oldHead.ptr) return nullptr;
            Tagged newHead{oldHead.ptr->next, oldHead.version + 1};
            if (freeHead.compare_exchange_weak(oldHead, newHead)) {
                return reinterpret_cast<T*>(oldHead.ptr);
            }
        }
    }

    void deallocate(T* p) {
        if (!p) return;
        Block* blockToRecycle = reinterpret_cast<Block*>(p);
        Tagged oldHead = freeHead.load();
        while (true) {
            blockToRecycle->next = oldHead.ptr;
            Tagged newHead{blockToRecycle, oldHead.version + 1};
            if (freeHead.compare_exchange_weak(oldHead, newHead)) {
                break;
            }
        }
    }
};

struct Order {
    uint64_t id;
    uint64_t price;
    uint64_t qty;
    uint64_t time;
    Side side;
    Type type;
    Order* next = nullptr;
    Order* prev = nullptr;
};

struct OrderQueue {
    Order* head = nullptr;
    Order* tail = nullptr;

    void push_back(Order* order) {
        order->next = nullptr;
        if (!tail) {
            head = tail = order;
            order->prev = nullptr;
        } else {
            tail->next = order;
            order->prev = tail;
            tail = order;
        }
    }

    void remove(Order* order) {
        if (order->prev) order->prev->next = order->next;
        else head = order->next;
        if (order->next) order->next->prev = order->prev;
        else tail = order->prev;
    }

    bool empty() const {
        return head == nullptr;
    }
};

class MatchingEngine {
private:
    MemoryPool<Order> pool;
    std::map<uint64_t, OrderQueue, std::greater<uint64_t>> bids;
    std::map<uint64_t, OrderQueue, std::less<uint64_t>> asks;
    std::unordered_map<uint64_t, Order*> orderIndex;
    uint64_t nextId = 1;

    uint64_t TimeStamp() {
        return static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    }

    uint64_t MakeId() {
        return nextId++;
    }

    void LimitSubmit(uint64_t price, uint64_t qty, Side side, Type type = Type::Limit) {
        if (side == Side::Buy) {
            while (qty > 0 && !asks.empty() && asks.begin()->first <= price) {
                auto& queue = asks.begin()->second;
                Order* resting = queue.head;
                uint64_t executed = std::min(resting->qty, qty);

                qty -= executed;
                resting->qty -= executed;

                if (resting->qty == 0) {
                    orderIndex.erase(resting->id);
                    queue.remove(resting);
                    pool.deallocate(resting);
                }
                if (queue.empty()) asks.erase(asks.begin());
            }
            if (qty > 0 && type == Type::Limit) {
                Order* newOrder = pool.allocate();
                if (!newOrder) return;
                newOrder->id = MakeId();
                newOrder->price = price;
                newOrder->qty = qty;
                newOrder->time = TimeStamp();
                newOrder->side = side;
                newOrder->type = type;
                bids[price].push_back(newOrder);
                orderIndex[newOrder->id] = newOrder;
            }
        } else {
            while (qty > 0 && !bids.empty() && bids.begin()->first >= price) {
                auto& queue = bids.begin()->second;
                Order* resting = queue.head;
                uint64_t executed = std::min(resting->qty, qty);

                qty -= executed;
                resting->qty -= executed;

                if (resting->qty == 0) {
                    orderIndex.erase(resting->id);
                    queue.remove(resting);
                    pool.deallocate(resting);
                }
                if (queue.empty()) bids.erase(bids.begin());
            }
            if (qty > 0 && type == Type::Limit) {
                Order* newOrder = pool.allocate();
                if (!newOrder) return;
                newOrder->id = MakeId();
                newOrder->price = price;
                newOrder->qty = qty;
                newOrder->time = TimeStamp();
                newOrder->side = side;
                newOrder->type = type;
                asks[price].push_back(newOrder);
                orderIndex[newOrder->id] = newOrder;
            }
        }
    }

    void MarketSubmit(uint64_t qty, Side side) {
        if (side == Side::Buy) {
            if (asks.empty()) return;
            LimitSubmit(UINT64_MAX, qty, side, Type::Market);
        } else {
            if (bids.empty()) return;
            LimitSubmit(0, qty, side, Type::Market);
        }
    }

    void PostOnly(uint64_t price, uint64_t qty, Side side) {
        if (side == Side::Buy) {
            if (!asks.empty() && asks.begin()->first <= price) return;
            Order* newOrder = pool.allocate();
            if (!newOrder) return;
            newOrder->id = MakeId();
            newOrder->price = price;
            newOrder->qty = qty;
            newOrder->time = TimeStamp();
            newOrder->side = side;
            newOrder->type = Type::PostOnly;
            bids[price].push_back(newOrder);
            orderIndex[newOrder->id] = newOrder;
        } else {
            if (!bids.empty() && bids.begin()->first >= price) return;
            Order* newOrder = pool.allocate();
            if (!newOrder) return;
            newOrder->id = MakeId();
            newOrder->price = price;
            newOrder->qty = qty;
            newOrder->time = TimeStamp();
            newOrder->side = side;
            newOrder->type = Type::PostOnly;
            asks[price].push_back(newOrder);
            orderIndex[newOrder->id] = newOrder;
        }
    }

public:
    MatchingEngine(size_t poolSize = 100000) : pool(poolSize) {}

    void Submit(uint64_t price, uint64_t qty, Side side, Type type) {
        if (type == Type::Limit) LimitSubmit(price, qty, side, type);
        if (type == Type::Market) MarketSubmit(qty, side);
        if (type == Type::PostOnly) PostOnly(price, qty, side);
    }

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

int main() {
    MatchingEngine engine;
    
    for (int price = 90; price < 100; ++price) {
        engine.Submit(price, 10, Side::Buy, Type::Limit);
    }

    for (int price = 100; price < 111; ++price) {
        engine.Submit(price, 10, Side::Sell, Type::Limit);
    }

    engine.CancelOrder(6);
    engine.Submit(103, 5, Side::Buy, Type::PostOnly);

    engine.PrintBooks();
    
    return 0;
}