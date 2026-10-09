#include <flat_map>
#include <map>
#include <vector>
#include <queue>
#include <thread>
#include <algorithm>
#include <string>
#include <cstddef>

#include "pool.h"
#include "spsc.h"

// {"result":null,"id":1}
// {"e":"trade","E":1791578295735,"s":"BTCUSDT","t":6751793095,"p":"82411.88000000","q":"0.00010000","T":1791578295734,"m":false,"M":true}
// {"e":"trade","E":1791578295860,"s":"BTCUSDT","t":6751793096,"p":"82411.87000000","q":"0.00008000","T":1791578295859,"m":true,"M":true}
// {"e":"trade","E":1791578295866,"s":"BTCUSDT","t":6751793098,"p":"82411.87000000","q":"0.00013000","T":1791578295865,"m":true,"M":true}
// {
//     "e": "trade",           // Event type
//     "E": 1672515782136,     // Event time
//     "s": "BNBBTC",          // Symbol
//     "t": 12345,             // Trade ID
//     "p": "0.001",           // Price
//     "q": "100",             // Quantity
//     "T": 1672515782136,     // Trade time
//     "m": true,              // Is the buyer the market maker?
//     "M": true               // Ignore
// }

// simulation : @trade, real market data : @depth + REST snapshot

struct Order {
    uint64_t e_time;
    uint64_t t_time;
    uint64_t t_id;

    uint64_t price;
    uint64_t quantity;

    Order* next = nullptr;
    Order* prev = nullptr;
    
    bool is_maker;
    bool is_buyer;  // 58

    char padding[6];
};

static_assert(sizeof(Order) == 64);

class Queue {
private:
    Order* head = nullptr;
    Order* tail = nullptr;

public:
    void push_back(Order* order) noexcept {
        order->next = nullptr;
        order->prev = tail;

        if (tail) {
            tail->next = order;
        } else {
            head = order;
        }

        tail = order;
    }

    void remove(Order* order) noexcept {
        if (order->next) {
            order->next->prev = order->prev;
        } else {
            tail = order->prev;
        }

        if (order->prev) {
            order->prev->next = order->next;
        } else {
            head = order->next;
        }

        order->next = nullptr;
        order->prev = nullptr;
    }

    bool empty() const noexcept {
        return head == nullptr;
    }

    Order* begin() const noexcept { return head; }
    Order* end() const noexcept { return nullptr; }

    Order* front() const noexcept { return head; }
    Order* back() const noexcept { return tail; }
};
class MatchingEngine {
private:

    
public:

};