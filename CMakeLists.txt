cmake_minimum_required(VERSION 3.20)

project(MatchingEngine
    VERSION 0.2.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_library(engine_common INTERFACE)

add_executable(test test.cpp)
target_link_libraries(test PRIVATE engine_common)

add_subdirectory(benchmark)
