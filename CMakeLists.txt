cmake_minimum_required(VERSION 3.16)
project(linux_concurrency_lab CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

# For aarch64 cross builds: -DCMAKE_TOOLCHAIN_FILE=toolchains/aarch64-linux-gnu.cmake
find_package(Threads REQUIRED)

add_library(lab_headers INTERFACE)
target_include_directories(lab_headers INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(lab_headers INTERFACE Threads::Threads)

add_executable(bench_ring bench/bench_ring.cpp)
target_link_libraries(bench_ring PRIVATE lab_headers)

add_executable(bench_pool bench/bench_pool.cpp)
target_link_libraries(bench_pool PRIVATE lab_headers)

add_executable(test_ring_buffer tests/test_ring_buffer.cpp)
target_link_libraries(test_ring_buffer PRIVATE lab_headers)

enable_testing()
add_test(NAME ring_buffer COMMAND test_ring_buffer)
