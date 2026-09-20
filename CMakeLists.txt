cmake_minimum_required(VERSION 3.16)

project(MatchingEngine
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

find_package(Threads REQUIRED)

add_library(engine_common INTERFACE)

target_include_directories(engine_common INTERFACE
    "${PROJECT_SOURCE_DIR}/include"
)

target_link_libraries(engine_common INTERFACE
    Threads::Threads
)

function(enable_project_warnings target)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )
    endif()
endfunction()

add_executable(matching_engine
    src/matching_engine.cpp
)

target_link_libraries(matching_engine PRIVATE engine_common)
enable_project_warnings(matching_engine)

add_executable(crypto_engine
    src/launch.cpp
)

target_link_libraries(crypto_engine PRIVATE engine_common)
enable_project_warnings(crypto_engine)

add_executable(file_converter
    data/file_converter.cpp
)

target_link_libraries(file_converter PRIVATE engine_common)
enable_project_warnings(file_converter)

option(BUILD_BENCHMARKS "Build benchmarks" OFF)

if(BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()