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

enum struct Side{ Buy, Sell };
enum struct Type{ Market, Limit, PostOnly};

struct Order{
    uint64_t id;
    uint64_t price;
    uint64_t qty;
    uint64_t time;
    Side side;
    Type type;
};

struct Block{
    Block* next;
};

// for Lockless ABA
struct alignas(16) Tagged{ 
    Block* ptr;
    uintptr_t version;
};


class MemoryPool{
private:
    std::atomic<Tagged> freeHead;
    char* memoryChunk;
public:
    MemoryPool(size_t objectCount) {
        size_t blockSize = sizeof(Order);
        size_t totalBytes = objectCount * blockSize;

        void* rawPtr = _aligned_malloc(totalBytes, 64);
        if (!rawPtr) throw std::bad_alloc();

        memoryChunk = static_cast<char*>(rawPtr);
        Block* head = reinterpret_cast<Block*>(memoryChunk);
        Block* current = head;

        for (size_t i = 0; i < objectCount; ++i) {
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

    char* allocate() { // CAS process
        Tagged oldHead = freeHead.load();
        while (true) {
            Tagged newHead;
            newHead.ptr = oldHead.ptr->next;
            newHead.version = oldHead.version + 1;

            if (freeHead.compare_exchange_weak(oldHead, newHead)) {
                return oldHead;
            }
        }
    };

    void deallocate(void* p) {
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

class MatchingEngine {
private:
    std::map<uint64_t, std::list<Order>, std::greater<uint64_t>> bids; // highest buy
    std::map<uint64_t, std::list<Order>, std::less<uint64_t>> asks; // lowest sell

    struct OrderLocation{
        Side side;
        uint64_t price;
        std::list<Order>::iterator iterator;
    };

    std::unordered_map<uint64_t, OrderLocation> orderIndex; // for O(1) search and cancel shares.

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
                auto& resting = queue.front();
                uint64_t excuted = std::min(resting.qty, qty);

                qty-=excuted;
                resting.qty-=excuted;

                if (resting.qty == 0) {
                    orderIndex.erase(resting.id);
                    queue.pop_front();
                }
                if (queue.empty()) asks.erase(asks.begin());
            };
            if (qty > 0 && type == Type::Limit) {
                bids[price].push_back({orderId, price, qty, TimeStamp(), side, type});
                orderIndex[orderId] = {side, price, std::prev(bids[price].end())};
            };
        } else {
            while (qty > 0 && !bids.empty() && bids.begin()->first >= price) {
                auto& queue = bids.begin()->second;
                auto& resting = queue.front();
                uint64_t excuted = std::min(resting.qty, qty);

                qty-=excuted;
                resting.qty-=excuted;

                if (resting.qty == 0) {
                    orderIndex.erase(resting.id);
                    queue.pop_front();
                }
                if (queue.empty()) bids.erase(bids.begin());
            };
            if (qty > 0 && type == Type::Limit) {
                asks[price].push_back({orderId, price, qty, TimeStamp(), side, type});
                orderIndex[orderId] = {side, price, std::prev(asks[price].end())};
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
            if (asks.empty() && asks.begin()->first <= price) return;
            bids[price].push_back({Id, price, qty, TimeStamp(), side, type});
            orderIndex[Id] = {side, price, std::prev(bids[price].end())};
        } else {
            if (bids.empty() && bids.begin()->first >= price) return;
            asks[price].push_back({Id, price, qty, TimeStamp(), side, type});
            orderIndex[Id] = {side, price, std::prev(asks[price].end())};
        }
    };



public:
    void Submit(uint64_t price, uint64_t qty, Side side, Type type) {
        if (type == Type::Limit) LimitSubmit(price, qty, side, type);
        if (type == Type::Market) MarketSubmit(qty, side);
        if (type == Type::PostOnly) PostOnly(price, qty, side);
    };

    void CancelOrder(uint64_t Id) {
        auto it = orderIndex.find(Id);
        if (it == orderIndex.end()) return;
        auto& loc = it->second;
        if (loc.side == Side::Buy) {
            bids[loc.price].erase(loc.iterator);
            if (bids[loc.price].empty()) bids.erase(loc.price);
        } else {
            asks[loc.price].erase(loc.iterator);
            if (bids[loc.price].empty()) bids.erase(loc.price);
        };
    };

    void PrintBooks() {
        for (auto i = asks.rbegin(); i != asks.rend(); ++i) {
            for (const auto & order : i->second) {
                std::cout << "ID: " << order.id << " Price: " << order.price << " $ " <<" Qty: " << order.qty << '\n';
            }
        }
        std::cout << "Asks: " << '\n';
        std::cout << "     ----------------" << '\n';
        std::cout << "Bits: " << '\n';

        for (auto i = bids.begin(); i != bids.end(); ++i) {
            for (const auto& order : i->second) {
                std::cout << "ID: " << order.id << " Price: " << order.price << " $ " <<" Qty: " << order.qty << '\n';
            }
        }
    };
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