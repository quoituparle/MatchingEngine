#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <queue>
#include <algorithm>
#include <cstdint>

enum class Side {
    Buy,
    Sell
};

struct Order {
    uint64_t id;
    uint64_t price;
    uint64_t qty;
    Side side;
};

class MatchingEngine {
private:
    // Bids (Buy orders) sorted highest price to lowest
    std::map<uint64_t, std::queue<Order>, std::greater<uint64_t>> bids;
    // Asks (Sell orders) sorted lowest price to highest
    std::map<uint64_t, std::queue<Order>> asks;

public:
    void submit(Order order) {
        if (order.side == Side::Buy) {
            match_buy(order);
        } else {
            match_sell(order);
        }
    }

    void cancel_order(const std::string& order_id) {
        std::cout << "Cancelling order: " << order_id << "\n";
        // To truly remove from std::queue in O(1), a different data structure 
        // (like an intrusive list or a map of ID to order) would be needed.
    }

    void print_book() const {
        std::cout << "--- Order Book ---\n";
        std::cout << "Asks:\n";
        // Iterate backwards through asks to show highest asks first, down to lowest ask
        for (auto it = asks.rbegin(); it != asks.rend(); ++it) {
            uint64_t total_qty = 0;
            std::queue<Order> temp = it->second;
            while(!temp.empty()) {
                total_qty += temp.front().qty;
                temp.pop();
            }
            std::cout << it->first << " : " << total_qty << "\n";
        }
        std::cout << "Bids:\n";
        for (const auto& [price, q] : bids) {
            uint64_t total_qty = 0;
            std::queue<Order> temp = q;
            while(!temp.empty()) {
                total_qty += temp.front().qty;
                temp.pop();
            }
            std::cout << price << " : " << total_qty << "\n";
        }
        std::cout << "------------------\n";
    }

private:
    void match_buy(Order& order) {
        while (order.qty > 0 && !asks.empty() && asks.begin()->first <= order.price) {
            auto& ask_queue = asks.begin()->second;
            Order& resting_ask = ask_queue.front();

            uint64_t trade_qty = std::min(resting_ask.qty, order.qty);
            std::cout << "Trade Executed: " << trade_qty << " @ " << asks.begin()->first 
                      << " (Buy Order " << order.id << " hits Sell Order " << resting_ask.id << ")\n";

            order.qty -= trade_qty;
            resting_ask.qty -= trade_qty;

            if (resting_ask.qty == 0) {
                ask_queue.pop();
            }

            if (ask_queue.empty()) {
                asks.erase(asks.begin());
            }
        }

        if (order.qty > 0) {
            bids[order.price].push(order);
            std::cout << "Order added to book: Buy " << order.qty << " @ " << order.price << " (ID: " << order.id << ")\n";
        }
    }

    void match_sell(Order& order) {
        while (order.qty > 0 && !bids.empty() && bids.begin()->first >= order.price) {
            auto& bid_queue = bids.begin()->second;
            Order& resting_bid = bid_queue.front();

            uint64_t trade_qty = std::min(resting_bid.qty, order.qty);
            std::cout << "Trade Executed: " << trade_qty << " @ " << bids.begin()->first 
                      << " (Sell Order " << order.id << " hits Buy Order " << resting_bid.id << ")\n";

            order.qty -= trade_qty;
            resting_bid.qty -= trade_qty;

            if (resting_bid.qty == 0) {
                bid_queue.pop();
            }

            if (bid_queue.empty()) {
                bids.erase(bids.begin());
            }
        }

        if (order.qty > 0) {
            asks[order.price].push(order);
            std::cout << "Order added to book: Sell " << order.qty << " @ " << order.price << " (ID: " << order.id << ")\n";
        }
    }
};

int main() {
    MatchingEngine engine;

    std::cout << "--- Initializing Engine and Submitting Orders ---\n";
    engine.submit({1, 100, 10, Side::Buy});
    engine.submit({2, 99, 5, Side::Buy});
    engine.submit({3, 101, 15, Side::Sell});
    engine.submit({4, 102, 10, Side::Sell});

    engine.print_book();

    std::cout << "\n--- Submitting Crossing Order ---\n";
    // This buy order should cross with the sell order at 101, taking 15 qty, leaving 5 qty to rest at 101
    engine.submit({5, 101, 20, Side::Buy}); 

    engine.print_book();
    
    std::cout << "\n--- Testing Cancel ---\n";
    engine.cancel_order("order_123");

    return 0;
}
