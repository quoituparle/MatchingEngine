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


public:
    void LimitSubmit(uint64_t price, uint64_t qty, Side side, Type type = Type::Limit) {
        if (side == Side::Buy) {
            while (qty > 0 && !asks.empty() && asks.begin()->first <= price) {
                auto& queue = asks.begin()->second;
                auto& resting = queue.front();
                uint64_t excuted = std::min(resting.qty, qty);

                qty-=excuted;
                resting.qty-=excuted;

                if (resting.qty == 0) queue.pop_front();
                if (queue.empty()) asks.erase(asks.begin());
            };
            if (qty > 0 && type == Type::Limit) bids[price].push_back({MakeId(), price, qty, TimeStamp(), side, type});
        } else {
            while (qty > 0 && !bids.empty() && bids.begin()->first >= price) {
                auto& queue = bids.begin()->second;
                auto& resting = queue.front();
                uint64_t excuted = std::min(resting.qty, qty);

                qty-=excuted;
                resting.qty-=excuted;

                if (resting.qty == 0) queue.pop_front();
                if (queue.empty()) bids.erase(bids.begin());
            };
            if (qty > 0 && type == Type::Limit) asks[price].push_back({MakeId(), price, qty, TimeStamp(), side, type});
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

    void CancelOrder(uint64_t Id) {
        auto it = orderIndex.find(Id);
        if (it == orderIndex.end()) return;
        auto& loc = it->second;
        if (loc.side == Side::Buy) {
            bids[loc.price].erase(loc.iterator);
        } else {
            asks[loc.price].erase(loc.iterator);
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
    std::cout << "Limit Buy" << '\n';
    engine.LimitSubmit(100, 2, Side::Buy);
    engine.LimitSubmit(105, 5, Side::Sell);
    engine.LimitSubmit(95, 5, Side::Sell);

    engine.LimitSubmit(94, 2, Side::Sell);
    engine.LimitSubmit(95, 10, Side::Sell);
    engine.LimitSubmit(96, 10, Side::Sell);

    std::cout <<"Market Order" << '\n';
    engine.MarketSubmit(20, Side::Buy);

    engine.PrintBooks();

    return 0;
    
}