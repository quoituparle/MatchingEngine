#include <benchmark/benchmark.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <thread>

#include "../include/spsc.h"

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>

inline void cpu_relax() {
    _mm_pause();
}
#else
inline void cpu_relax() {
    std::this_thread::yield();
}
#endif


template <std::size_t Capacity>
static void BM_SPSC_Throughput(benchmark::State& state) {
    constexpr std::size_t kOperations = 1'000'000;

    SPSC<int, Capacity> queue;

    for (auto _ : state) {
        std::atomic<bool> start{false};

        std::chrono::steady_clock::time_point end_time;

        std::thread producer([&] {
            while (!start.load(std::memory_order_acquire)) {
                cpu_relax();
            }

            for (std::size_t i = 0; i < kOperations; ++i) {
                while (!queue.try_emplace(static_cast<int>(i))) {
                    cpu_relax();
                }
            }
        });

        std::thread consumer([&] {
            while (!start.load(std::memory_order_acquire)) {
                cpu_relax();
            }

            std::size_t popped = 0;

            while (popped < kOperations) {
                if (queue.try_pop()) {
                    ++popped;
                } else {
                    cpu_relax();
                }
            }

            end_time = std::chrono::steady_clock::now();
        });

        const auto start_time = std::chrono::steady_clock::now();

        start.store(true, std::memory_order_release);

        producer.join();
        consumer.join();

        const std::chrono::duration<double> elapsed =
            end_time - start_time;

        state.SetIterationTime(elapsed.count());
    }

    state.SetItemsProcessed(
        static_cast<int64_t>(state.iterations()) *
        static_cast<int64_t>(kOperations)
    );
}


BENCHMARK_TEMPLATE(BM_SPSC_Throughput, 64)
    ->UseManualTime()
    ->Unit(benchmark::kMillisecond);

BENCHMARK_TEMPLATE(BM_SPSC_Throughput, 256)
    ->UseManualTime()
    ->Unit(benchmark::kMillisecond);

BENCHMARK_TEMPLATE(BM_SPSC_Throughput, 1024)
    ->UseManualTime()
    ->Unit(benchmark::kMillisecond);

BENCHMARK_TEMPLATE(BM_SPSC_Throughput, 4096)
    ->UseManualTime()
    ->Unit(benchmark::kMillisecond);


BENCHMARK_MAIN();