#include <vector>
#include <iostream>
#include <thread>
#include <algorithm>
#include <cstdint>
#include <malloc.h>
#include <atomic>
#include <map>
#include <queue>

enum struct Side{ Buy, Sell };

struct Order{
    uint64_t id;
    uint64_t price;
    uint64_t qty;
    Side side;
};

class MatchingEngine {
    std::map<uint64_t, std::queue<Order>, std::greater<uint64_t>> bits;
    std::map<uint64_t, std::queue<Order>> asks;
public:
    void submit(Order order) {
        if (order.side == Side::Buy) {
            // Use while loops to eliminate every share.
            while (order.qty > 0 && !asks.empty() && asks.begin()->first <= order.price) {
                auto& queue = asks.begin()->second;
                Order& resting = queue.front();

                uint64_t trade_qty = std::min(resting.qty, order.qty);
                std::cout << "Match: " << trade_qty << " @ " << asks.begin()->first << "\n";
                order.qty -= trade_qty;
                resting.qty -= trade_qty;

                if (resting.qty == 0) {
                    queue.pop();
                }

                if (queue.empty()) asks.erase(asks.begin());
                
            }
            if (order.qty > 0) bits[order.price].push(order);
        } else {
            while (order.qty > 0 && !bits.empty() && bits.begin()->first >= order.price) {
                auto& queue = bits.begin()->second;
                Order& resting = queue.front();

                uint64_t trade_qty = std::min(resting.qty, order.qty);
                std::cout << "Match: " << trade_qty << " @ " << bits.begin()->first << "\n";

                order.qty -= trade_qty;
                resting.qty -= trade_qty;

                if (resting.qty == 0) queue.pop();
                if (queue.empty()) bits.erase(bits.begin());
            }
            if (order.qty > 0) asks[order.price].push(order);
        }
    }
};