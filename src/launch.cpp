#include "../include/crypto_matching_engine.h"
#include "../include/mmap.h"
#include <iostream>
#include <string>
#include <system_error>
#include <chrono>
#include <memory>

const std::string path = "data/output.bin"; // change to your own files.

int main() {
    std::error_code error;
    mio::mmap_source ro_mmap;
    auto orders = mmap(ro_mmap, path, error);
    if (error) { return handle_error(error); }

    rigtorp::SPSCQueue<OrderData> queue(131072); // When you have a large file don't forget to expand queue volumn

    auto engine_storage = std::make_unique<CryptoMatchingEngine>();
    CryptoMatchingEngine& engine = *engine_storage;
    engine.Start(queue);

    size_t count = ro_mmap.size() / sizeof(OrderData);
    auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < count; ++i) {
        OrderData order {
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
    while (queue.front() != nullptr) {
        std::this_thread::yield();
    }
    engine.Stop();
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "Project over, time use is " << duration.count() << "ms" << std::endl;

    return 0;
};