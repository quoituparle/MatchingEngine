set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/benchmark)

add_executable(pool_test pool_test.cpp)
target_link_libraries(pool_test PRIVATE engine_common benchmark::benchmark_main)

add_executable(spsc_test spsc_test.cpp)
target_link_libraries(spsc_test PRIVATE engine_common benchmark::benchmark_main)
