add_subdirectory(third_party/benchmark)

add_executable(pool_test pool_test.cpp)
target_link_libraries(pool_test PRIVATE engine_common benchmark::benchmark_main)

add_executable(spsc_test spsc_test.cpp)
target_link_libraries(spsc_test PRIVATE engine_common benchmark::benchmark_main)
