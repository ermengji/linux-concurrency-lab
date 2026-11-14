// SPDX-License-Identifier: MIT
// Throughput benchmark: one producer thread, one consumer thread.
//
// The producer spins on try_push (a full buffer means the consumer is the
// bottleneck), the consumer spins on try_pop. This measures the best case for
// the hand-off, which is what you want when comparing implementations.
//
// Build:  cmake --build build --target bench_ring
// Run:    ./build/bench_ring

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <thread>

#include "spsc_ring_buffer.hpp"

namespace {

using Clock = std::chrono::steady_clock;

struct Result {
    double ns_per_op;
    double mops;  // million ops per second
};

Result run_once(std::size_t capacity, std::size_t items) {
    lab::SpscRingBuffer<std::uint64_t> rb(capacity);
    std::atomic<bool> producer_done{false};
    std::uint64_t checksum = 0;

    const auto t0 = Clock::now();

    std::thread consumer([&] {
        std::uint64_t value = 0;
        for (;;) {
            if (rb.try_pop(value)) {
                checksum += value;
            } else if (producer_done.load(std::memory_order_acquire)) {
                if (!rb.try_pop(value)) break;
                checksum += value;
            }
        }
    });

    for (std::uint64_t i = 0; i < items; ++i) {
        while (!rb.try_push(i)) {
            // spin: buffer full, consumer has not caught up yet
        }
    }
    producer_done.store(true, std::memory_order_release);
    consumer.join();

    const auto t1 = Clock::now();
    const double ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    // Consume the checksum so the compiler cannot drop the whole loop.
    if (checksum == 0xdeadbeefULL) std::fprintf(stderr, "unreachable\n");

    return {ns / static_cast<double>(items),
            static_cast<double>(items) / ns * 1000.0};
}

}  // namespace

int main(int argc, char** argv) {
    const std::size_t items =
        (argc > 1) ? static_cast<std::size_t>(std::atoll(argv[1])) : 20000000;

    std::printf("SPSC ring buffer throughput\n");
    std::printf("items per run : %zu\n", items);
    std::printf("threads       : producer + consumer\n\n");
    std::printf("%10s %14s %14s\n", "capacity", "ns/op", "Mops/s");
    std::printf("------------------------------------------------\n");

    for (std::size_t capacity : {64u, 1024u, 65536u}) {
        Result best{1e18, 0.0};
        for (int round = 0; round < 5; ++round) {
            const Result r = run_once(capacity, items);
            if (r.ns_per_op < best.ns_per_op) best = r;
        }
        std::printf("%10zu %14.2f %14.2f\n", capacity, best.ns_per_op, best.mops);
    }
    return 0;
}
