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
#include "SPSCQueue.h"
#include "MemoryPool.h"
#include "mmap.h"

enum struct Side : uint8_t { Buy, Sell };

struct OrderData {
    uint64_t id;
    uint64_t price;
    uint64_t qty;
    uint64_t quote_qty;
    uint64_t time;
    Side side;
    bool best_match;
}

struct Order {
    uint64_t id;
    uint64_t price;
    uint64_t qty;
    uint64_t quote_qty;
    uint64_t time;
    Side side;
    bool best_match;
    Order* next;
    Order* prev;
}

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

class CryptoMatchingEngine {
private:
    MemoryPool<Order> pool;
    std::map<uint64_t, Queue, std::greater<uint64_t>> bids;
    std::map<uint64_t, Queue, std::less<uint64_t>> asks;

    void Submit(OrderData) {
        if (OrderData::side == Side::Buy) {
            while(OrderData::qty > 0 && !asks.empty() && asks.begin()->first <= OrderData::price) {
                auto& queue = asks.begin()->second;
                Order* resting = queue.head;
                uint64_t excuted = std::min(resting->qty, OrderData::qty);

                OrderData::qty-=excuted;
                resting->qty-=excuted;

                if (resting->qty == 0) {
                    queue.remove(resting);
                    pool.deallocate(resting);
                }
                if (queue.empty()) asks.erase(asks.begin());
            };
            Order* order = pool.allocate();
            new (order) Order{OrderData::id, OrderData::price, OrderData::qty, OrderData::quote_qty, OrderData::time, OrderData::side, OrderData::best_match, nullptr, nullptr};
        } else {
            while(OrderData::qty > 0 && !bids.empty() && bids.begin()->first >= OrderData::price) {
                auto& queue = bids.begin()->second;
                Order* resting = queue.head;
                uint64_t excuted = std::min(resting->qty, OrderData::qty);

                OrderData::qty-=excuted;
                resting->qty-=excuted;

                if (resting->qty == 0) {
                    queue.remove(resting);
                    pool.deallocate(resting);
                }
                if (queue.empty()) bids.erase(bids.begin());
            };
            Order* order = pool.allocate();
            new (order) Order{OrderData::id, OrderData::price, OrderData::qty, OrderData::quote_qty, OrderData::time, OrderData::side, OrderData::best_match, nullptr, nullptr};
        };
    };

public:
    CryptoMatchingEngine(size_t poolSize = 100000) : pool(poolSize) {}

    ~CryptoMatchingEngine(){}

    void Start(rigtorp::SPSCQueue<OrderData>& queue) {
        if (running) return;
        running = true;

        worker_thread = std::thread([&](){
            while(running || queue.front() != nullptr) {
                auto* cmd_ptr = queue.front();
                if (cmd_ptr) {
                    OrderData cmd = *cmd_ptr;
                    queue.pop();

                    Submit(OrderData);
                }
            }
        });
    };

    void Stop() {
        running = false;
        if (worker_thread.joinable()) {
            worker_thread.join();
        };
    };

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
    };
private:
    std::atomic<bool> running(false);
    std::thread worker_thread;
};

// using mio libaries
int handle_error(const std::error_code& error)
{
    const auto& errmsg = error.message();
    std::printf("error mapping file: %s, exiting...\n", errmsg.c_str());
    return error.value();
}

void mmap(const std::string& path, const std::error_code& error) {
        mio::mmap_source ro_mmap;
        ro_mmap.map(path, error);
        if (error) { return handle_error(error); }

        const auto* data = ro_mmap.data();
        static_assert(data, "Cannot find ro data");
        auto* orders = reinterpret_cast<OrderData*>(data);
}
