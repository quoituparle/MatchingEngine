#include <vector>
#include <iostream>
#include <thread>
#include <algorithm>
#include <cstdint>
#include <malloc.h>
#include <atomic>
#include <map>
#include <queue>
#include <chrono>

enum struct Side{ Buy, Sell };
enum struct Type{ Market, Limit};

struct Order{
    uint64_t id;
    uint64_t price;
    uint64_t qty;
    uint64_t time;
    Side side;
    Type type;
};

class MatchingEngine {
private:
    std::map<uint64_t, std::queue<Order>, std::greater<uint64_t>> bids;
    std::map<uint64_t, std::queue<Order>> asks;

    uint64_t TimeStamp(){
        return static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    };

    uint64_t MakeId(){
        const uint64_t time = TimeStamp();
        return time;
    };

public:
    void LimitSubmit(uint64_t price, uint64_t qty, Side side) {
        if (side == Side::Buy) {
            while (qty > 0 && !asks.empty() && asks.begin()->first <= price) {
                auto& queue = asks.begin()->second;
                auto& resting = queue.front();
                uint64_t excuted = std::min(resting.qty, qty);

                qty-=excuted;
                resting.qty-=excuted;

                if (resting.qty == 0) queue.pop();
                if (queue.empty()) asks.erase(asks.begin());
            };
            if (qty > 0) bids.emplace({MakeId(), price, qty, TimeStamp(), side, Type::Limit});
        } else {
            while (qty > 0 && !bids.empty() && bids.begin()->first >= price) {
                auto& queue = bids.begin()->second;
                auto& resting = queue.front();
                uint64_t excuted = queue.front();

                qty-=excuted;
                resting.qty-=excuted;

                if (resting.qty == 0) queue.pop();
                if (queue.empty()) bids.erase(bids.begin());
            };
            if (qty > 0) asks[price].push({MakeId(), price, qty, TimeStamp(), side, Type::Limit});
        };
    };

    void MarketSubmit(uint64_t qty, Side side) {
        if (side == Side::Buy) {
            uint64_t price = asks.begin()->first;
            LimitSubmit(price, qty, side);
        } else {
            uint64_t price = bids.begin()->first;
            LimitSubmit(price, qty, side);
        }
    }
};

uint64_t TimeStamp() {
    return static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count();
    );
};

int main(){
    MatchingEngine engine;
    std::cout << "Limit Buy" << '\n';
    engine.submit({1, 100, 10, Side::Buy, TimeStamp()});
    engine.submit({2, 98,  5, Side::Buy, TimeStamp()});

    std::cout << "Complet deal with id1" << "\n";
    engine.submit({3, 99, 10, Side::Sell, TimeStamp()});

    std::cout << "Pending Order" << "\n";
    engine.submit({4, 90, 100, Side::Buy, TimeStamp()});
    engine.submit({5, 110, 100, Side::Sell, TimeStamp()});

    return 0;
    
}