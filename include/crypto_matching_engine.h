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
#include <system_error>
#include "SPSCQueue.h"
#include "MemoryPool.h"
#include "mmap.h"

enum struct Side : uint8_t { Buy, Sell };

#pragma pack(push, 1)
struct OrderData {
    uint64_t id;
    uint64_t price;
    uint64_t qty;
    uint64_t quote_qty;
    uint64_t time;
    Side side;
    bool best_match;
};
#pragma pack(pop)

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

        order->next = nullptr;
        order->prev = nullptr;
    }

    bool empty() const {
        return head == nullptr;
    }
};

class CryptoMatchingEngine {
private:
    MemoryPool<Order, 8000000> pool;
    std::map<uint64_t, Queue, std::greater<uint64_t>> bids;
    std::map<uint64_t, Queue, std::less<uint64_t>> asks;

    void Submit(OrderData order) {
        if (order.side == Side::Buy) {
            while(order.qty > 0 && !asks.empty() && asks.begin()->first <= order.price) {
                auto& queue = asks.begin()->second;
                Order* resting = queue.head;
                
                if (!resting) {
                    asks.erase(asks.begin());
                    break;
                }

                uint64_t excuted = (std::min)(resting->qty, order.qty);
                
                if (excuted == 0) {
                    break;
                }

                order.qty -= excuted;
                resting->qty -= excuted;

                if (resting->qty == 0) {
                    queue.remove(resting);
                    pool.deallocate(resting);
                }
                
                if (queue.empty()) {
                    asks.erase(asks.begin());
                }
            }
            
            if (order.qty > 0) {
                Order* new_order = pool.allocate(Order{order.id, order.price, order.qty, order.quote_qty, order.time, order.side, order.best_match, nullptr, nullptr});
                bids[order.price].intrusive_push_back(new_order);
                if (!new_order) {
                    throw std::bad_alloc();
                };
            }
        } else {
            while(order.qty > 0 && !bids.empty() && bids.begin()->first >= order.price) {
                auto& queue = bids.begin()->second;
                Order* resting = queue.head;
                
                if (!resting) {
                    bids.erase(bids.begin());
                    break;
                }

                uint64_t excuted = (std::min)(resting->qty, order.qty);
                if (excuted == 0) {
                    break;
                }

                order.qty -= excuted;
                resting->qty -= excuted;

                if (resting->qty == 0) {
                    queue.remove(resting);
                    pool.deallocate(resting);
                }
                if (queue.empty()) {
                    bids.erase(bids.begin());
                }
            }
            
            if (order.qty > 0) {
                Order* new_order = pool.allocate(Order{order.id, order.price, order.qty, order.quote_qty, order.time, order.side, order.best_match, nullptr, nullptr});
                asks[order.price].intrusive_push_back(new_order);
                if (!new_order) {
                    throw std::bad_alloc();
                };
            }
        }
    }

public:
    CryptoMatchingEngine() {}

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

                    Submit(cmd);
                }
            }
        });
    };

    void Stop() {
        if (!running) return;
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
    std::atomic<bool> running = false;
    std::thread worker_thread;
};

// using mio libaries
int handle_error(const std::error_code& error)
{
    const auto& errmsg = error.message();
    std::printf("error mapping file: %s, exiting...\n", errmsg.c_str());
    return error.value();
}

const OrderData* mmap(mio::mmap_source& ro_mmap, const std::string& path, std::error_code& error) {
        ro_mmap.map(path, error);
        if (error) { return nullptr; }

        const auto* data = ro_mmap.data();
        if (!data) { return nullptr; };
        return reinterpret_cast<const OrderData*>(data);
        
}
