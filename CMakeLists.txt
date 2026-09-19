cmake_minimum_required(VERSION 3.20)

project(MatchingEngine
    VERSION 1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# =====================
# Core library
# =====================

add_library(matching_engine
    matching_engine.cpp
)

target_include_directories(matching_engine
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_compile_options(matching_engine
    PRIVATE
        -Wall
        -Wextra
        -O3
)


# =====================
# Test executable
# =====================

add_executable(test_engine
    test.cpp
)

target_link_libraries(test_engine
    PRIVATE
        matching_engine
)