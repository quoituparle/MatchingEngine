/**
 * Order Book Matching Engine
 *
 * Supports:
 *  - Limit orders (buy/sell)
 *  - Market orders
 *  - Order cancellation
 *  - Price-time priority (FIFO within same price level)
 *  - Multiple instruments
 */

#include <iostream>
#include <map>
#include <unordered_map>
#include <queue>
#include <list>
#include <string>
#include <chrono>
#include <optional>
#include <functional>
#include <sstream>
#include <iomanip>

// ─────────────────────────────────────────────
// Types & Enums
// ─────────────────────────────────────────────

using Price    = int64_t;   // price in ticks (e.g. cents)
using Quantity = int64_t;
using OrderId  = uint64_t;
using Timestamp = uint64_t;

enum class Side   { Buy, Sell };
enum class OrdType { Limit, Market };
enum class OrdStatus { New, PartiallyFilled, Filled, Cancelled };

// ─────────────────────────────────────────────
// Order
// ─────────────────────────────────────────────

struct Order {
    OrderId   id;
    std::string symbol;
    Side      side;
    OrdType   type;
    Price     price;        // ignored for market orders
    Quantity  qty;
    Quantity  leavesQty;    // remaining
    Timestamp timestamp;
    OrdStatus status;

    Order(OrderId id_, const std::string& sym, Side s, OrdType t,
          Price p, Quantity q, Timestamp ts)
        : id(id_), symbol(sym), side(s), type(t),
          price(p), qty(q), leavesQty(q), timestamp(ts),
          status(OrdStatus::New) {}
};

// ─────────────────────────────────────────────
// Trade (fill report)
// ─────────────────────────────────────────────

struct Trade {
    std::string symbol;
    OrderId  makerOrderId;
    OrderId  takerOrderId;
    Price    price;
    Quantity qty;
    Timestamp timestamp;
};

// ─────────────────────────────────────────────
// Order Book for a single symbol
// ─────────────────────────────────────────────

class OrderBook {
public:
    using OrderList   = std::list<Order*>;
    using PriceLevel  = OrderList;

    // Bids: highest price first
    std::map<Price, PriceLevel, std::greater<Price>> bids;
    // Asks: lowest price first
    std::map<Price, PriceLevel, std::less<Price>>    asks;

    // Index for O(1) cancel
    std::unordered_map<OrderId, std::pair<Price, OrderList::iterator>> orderIndex;

    std::string symbol;
    std::vector<Trade> trades;  // filled trades emitted by this book

    explicit OrderBook(const std::string& sym) : symbol(sym) {}

    ~OrderBook() {
        for (auto& [p, lvl] : bids) for (auto* o : lvl) delete o;
        for (auto& [p, lvl] : asks) for (auto* o : lvl) delete o;
    }

    // Insert a resting order into the book
    void insertOrder(Order* order) {
        if (order->side == Side::Buy) {
            auto& lvl = bids[order->price];
            lvl.push_back(order);
            orderIndex[order->id] = {order->price, std::prev(lvl.end())};
        } else {
            auto& lvl = asks[order->price];
            lvl.push_back(order);
            orderIndex[order->id] = {order->price, std::prev(lvl.end())};
        }
    }

    // Remove an order from book (cancel or fully filled)
    void removeFromBook(Order* order) {
        auto it = orderIndex.find(order->id);
        if (it == orderIndex.end()) return;
        auto [price, listIt] = it->second;
        if (order->side == Side::Buy) {
            auto& lvl = bids[price];
            lvl.erase(listIt);
            if (lvl.empty()) bids.erase(price);
        } else {
            auto& lvl = asks[price];
            lvl.erase(listIt);
            if (lvl.empty()) asks.erase(price);
        }
        orderIndex.erase(it);
    }

    void emitTrade(Order* maker, Order* taker, Quantity qty, Timestamp ts) {
        Trade t;
        t.symbol       = symbol;
        t.makerOrderId = maker->id;
        t.takerOrderId = taker->id;
        t.price        = maker->price;
        t.qty          = qty;
        t.timestamp    = ts;
        trades.push_back(t);

        std::cout << "[TRADE] " << symbol
                  << " px=" << maker->price
                  << " qty=" << qty
                  << " maker=" << maker->id
                  << " taker=" << taker->id << "\n";
    }

    // Match incoming order against the opposite side
    void matchSide(Order* taker, Timestamp ts, auto& oppSide) {
        while (taker->leavesQty > 0 && !oppSide.empty()) {
            auto levelIt = oppSide.begin();
            Price bestPrice = levelIt->first;

            // Price check for limit orders
            if (taker->type == OrdType::Limit) {
                if (taker->side == Side::Buy  && taker->price < bestPrice) break;
                if (taker->side == Side::Sell && taker->price > bestPrice) break;
            }

            PriceLevel& lvl = levelIt->second;
            while (taker->leavesQty > 0 && !lvl.empty()) {
                Order* maker = lvl.front();
                Quantity fillQty = std::min(taker->leavesQty, maker->leavesQty);

                taker->leavesQty -= fillQty;
                maker->leavesQty -= fillQty;

                emitTrade(maker, taker, fillQty, ts);

                if (maker->leavesQty == 0) {
                    maker->status = OrdStatus::Filled;
                    orderIndex.erase(maker->id);
                    lvl.pop_front();
                    delete maker;
                } else {
                    maker->status = OrdStatus::PartiallyFilled;
                }
            }
            if (lvl.empty()) oppSide.erase(levelIt);
        }
    }

    void match(Order* taker, Timestamp ts) {
        if (taker->side == Side::Buy)
            matchSide(taker, ts, asks);
        else
            matchSide(taker, ts, bids);

        if (taker->leavesQty == 0)
            taker->status = OrdStatus::Filled;
        else if (taker->leavesQty < taker->qty)
            taker->status = OrdStatus::PartiallyFilled;
    }

    // Submit an order
    void submitOrder(Order* order, Timestamp ts) {
        match(order, ts);

        // Rest unfilled limit orders; discard unfilled market orders
        if (order->leavesQty > 0) {
            if (order->type == OrdType::Limit) {
                insertOrder(order);
                std::cout << "[REST]  " << symbol
                          << " orderId=" << order->id
                          << " side=" << (order->side == Side::Buy ? "BUY" : "SELL")
                          << " px=" << order->price
                          << " qty=" << order->leavesQty << "\n";
            } else {
                // Market order with no liquidity — cancel remainder
                order->status = (order->qty == order->leavesQty)
                                ? OrdStatus::Cancelled
                                : OrdStatus::PartiallyFilled;
                std::cout << "[CANCEL] Market order " << order->id
                          << " unfilled qty=" << order->leavesQty << "\n";
                delete order;
            }
        } else {
            delete order;
        }
    }

    bool cancelOrder(OrderId id) {
        auto it = orderIndex.find(id);
        if (it == orderIndex.end()) return false;
        auto listIt = it->second.second;
        Order* o = *listIt;
        o->status = OrdStatus::Cancelled;
        removeFromBook(o);
        std::cout << "[CANCEL] orderId=" << o->id
                  << " sym=" << o->symbol << "\n";
        delete o;
        return true;
    }

    void printBook(int depth = 5) const {
        std::cout << "\n── Order Book: " << symbol << " ──\n";
        std::cout << std::setw(12) << "ASK QTY"
                  << std::setw(10) << "PRICE"
                  << "\n";

        // Collect asks (lowest first → print in reverse for display)
        std::vector<std::pair<Price, Quantity>> askLevels;
        for (auto& [p, lvl] : asks) {
            Quantity tot = 0;
            for (auto* o : lvl) tot += o->leavesQty;
            askLevels.push_back({p, tot});
            if ((int)askLevels.size() >= depth) break;
        }
        for (auto it = askLevels.rbegin(); it != askLevels.rend(); ++it)
            std::cout << std::setw(12) << it->second
                      << std::setw(10) << it->first << "\n";

        std::cout << "──────────────────────\n";

        int cnt = 0;
        for (auto& [p, lvl] : bids) {
            Quantity tot = 0;
            for (auto* o : lvl) tot += o->leavesQty;
            std::cout << std::setw(12) << ""
                      << std::setw(10) << p
                      << std::setw(10) << tot << "\n";
            if (++cnt >= depth) break;
        }
        std::cout << std::setw(12) << ""
                  << std::setw(10) << "PRICE"
                  << std::setw(10) << "BID QTY" << "\n\n";
    }
};

// ─────────────────────────────────────────────
// Matching Engine (multi-instrument)
// ─────────────────────────────────────────────

class MatchingEngine {
    std::unordered_map<std::string, OrderBook*> books;
    OrderId nextOrderId = 1;

    Timestamp now() {
        return static_cast<Timestamp>(
            std::chrono::steady_clock::now().time_since_epoch().count());
    }

    OrderBook& getBook(const std::string& symbol) {
        auto it = books.find(symbol);
        if (it == books.end()) {
            books[symbol] = new OrderBook(symbol);
        }
        return *books[symbol];
    }

public:
    ~MatchingEngine() { for (auto& [s, b] : books) delete b; }

    OrderId submitLimit(const std::string& sym, Side side,
                        Price price, Quantity qty) {
        OrderId id = nextOrderId++;
        auto* order = new Order(id, sym, side, OrdType::Limit, price, qty, now());
        std::cout << "[NEW]   " << sym
                  << " orderId=" << id
                  << " side=" << (side == Side::Buy ? "BUY" : "SELL")
                  << " LIMIT px=" << price
                  << " qty=" << qty << "\n";
        getBook(sym).submitOrder(order, now());
        return id;
    }

    OrderId submitMarket(const std::string& sym, Side side, Quantity qty) {
        OrderId id = nextOrderId++;
        auto* order = new Order(id, sym, side, OrdType::Market, 0, qty, now());
        std::cout << "[NEW]   " << sym
                  << " orderId=" << id
                  << " side=" << (side == Side::Buy ? "BUY" : "SELL")
                  << " MARKET qty=" << qty << "\n";
        getBook(sym).submitOrder(order, now());
        return id;
    }

    bool cancel(const std::string& sym, OrderId id) {
        return getBook(sym).cancelOrder(id);
    }

    void printBook(const std::string& sym, int depth = 5) {
        getBook(sym).printBook(depth);
    }
};

// ─────────────────────────────────────────────
// Demo / Tests
// ─────────────────────────────────────────────

int main() {
    MatchingEngine engine;
    const std::string SYM = "AAPL";

    std::cout << "=== Seeding order book ===\n";
    // Resting asks
    engine.submitLimit(SYM, Side::Sell, 15020, 100);
    engine.submitLimit(SYM, Side::Sell, 15010, 200);
    engine.submitLimit(SYM, Side::Sell, 15000, 150);

    // Resting bids
    engine.submitLimit(SYM, Side::Buy, 14990, 100);
    engine.submitLimit(SYM, Side::Buy, 14980, 300);
    engine.submitLimit(SYM, Side::Buy, 14970, 200);

    engine.printBook(SYM);

    std::cout << "=== Aggressive limit buy (crosses spread) ===\n";
    engine.submitLimit(SYM, Side::Buy, 15010, 250);

    engine.printBook(SYM);

    std::cout << "=== Market sell ===\n";
    engine.submitMarket(SYM, Side::Sell, 120);

    engine.printBook(SYM);

    std::cout << "=== Cancel a resting order ===\n";
    auto cancelId = engine.submitLimit(SYM, Side::Buy, 14960, 500);
    engine.cancel(SYM, cancelId);

    engine.printBook(SYM);

    return 0;
}