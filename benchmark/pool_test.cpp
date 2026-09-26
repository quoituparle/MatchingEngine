#include <benchmark/benchmark.h>
#include <cstddef>
#include <vector>
#include <memory>

#include "../include/pool.h"


struct TestObject {
    int x;
    double y;

    TestObject(int a, double b)
        : x(a), y(b) {}
};


constexpr size_t N = 100000;


static void BM_Pool_Allocate_Deallocate(benchmark::State& state)
{
    Pool<TestObject, N> pool;

    for (auto _ : state) {

        std::vector<TestObject*> ptrs;
        ptrs.reserve(N);

        for(size_t i = 0; i < N; i++)
        {
            auto* p = pool.allocate(123, 3.14);
            benchmark::DoNotOptimize(p);
            ptrs.push_back(p);
        }
        benchmark::DoNotOptimize(ptrs.data());


        for(auto p : ptrs)
        {
            benchmark::DoNotOptimize(p);
        }
    }
}

BENCHMARK(BM_Pool_Allocate_Deallocate)
    ->Repetitions(10)->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();