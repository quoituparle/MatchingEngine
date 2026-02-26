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
private:
    std::map<uint64_t, std::queue<Order>, std::greater<uint64_t>> bids;
    std::map<uint64_t, std::queue<Order>> asks;
public:
    void submit(Order order) {
        if (order.side == Side::Buy) {
            while (order.qty > 0 && !asks.empty() && asks.begin()->first <= order.price) {
                auto& queue = asks.begin()->second;
                Order& resting = queue.front();
                uint64_t excuted = std::min(resting.qty, order.qty);
                std::cout << "Match: " << excuted << " @ " << asks.begin()->first << "\n";

                order.qty -= excuted;
                resting.qty -= excuted;

                if (resting.qty == 0) queue.pop();
                if (queue.empty()) asks.erase(asks.begin());
            }
            if (order.qty > 0) bids[order.price].push(order);
        } else {
            while (order.qty > 0 && !bids.empty() && bids.begin()->first >= order.price) {
                auto& queue = bids.begin()->second;
                Order& resting = queue.front();
                uint64_t excuted = std::min(resting.qty, order.qty);
                std::cout << "Match: " << excuted << " @ " << bids.begin()->first << "\n";

                order.qty -= excuted;
                resting.qty -= excuted;

                if (resting.qty == 0) queue.pop();
                if (queue.empty()) bids.erase(bids.begin());
            }
            if (order.qty > 0) asks[order.price].push(order);
        }
    }
};

int main(){
    MatchingEngine engine;
    std::cout << "Limit Buy" << '\n';
    engine.submit({1, 100, 10, Side::Buy});
    engine.submit({2, 98,  5, Side::Buy});

    std::cout << "Complet deal with id1" << "\n";
    engine.submit({3, 99, 10, Side::Sell});

    std::cout << "Pending Order" << "\n";
    engine.submit({4, 90, 100, Side::Buy});
    engine.submit({5, 110, 100, Side::Sell});

    return 0;
    
}