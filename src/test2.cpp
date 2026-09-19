#include "include/crypto_matching_engine.h"
#include "include/mmap.h"
#include <iostream>
#include <string>
#include <system_error>

int main() {
    size_t count = ro_mmap.size() / sizeof(OrderData);
    for (size_t i = 0; i < count; ++i) {
        OrderData order {
            std::cout << 
            orders[i].id,
            orders[i].price,
            orders[i].qty,
            orders[i].quote_qty,
            orders[i].time,
            orders[i].side,
            orders[i].best_match
        };
        
        while (!queue.try_push(order)) {
        std::this_thread::yield();
        }
    }

}