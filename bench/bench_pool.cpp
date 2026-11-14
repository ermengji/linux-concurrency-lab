// SPDX-License-Identifier: MIT
// Throughput benchmark: thread pool under different worker counts.
//
// Each task is intentionally tiny (an integer add) so the measurement reflects
// scheduling and queue overhead rather than the work itself. That is exactly
// the number you want to know before choosing a pool size.
//
// Build:  cmake --build build --target bench_pool
// Run:    ./build/bench_pool

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <thread>

#include "thread_pool.hpp"

namespace {
using Clock = std::chrono::steady_clock;
}

int main(int argc, char** argv) {
    const std::size_t tasks =
        (argc > 1) ? static_cast<std::size_t>(std::atoll(argv[1])) : 200000;

    std::printf("ThreadPool throughput (tiny tasks, %zu tasks)\n", tasks);
    std::printf("hardware_concurrency: %u\n\n", std::thread::hardware_concurrency());
    std::printf("%8s %14s %14s\n", "workers", "tasks/s", "us/task");
    std::printf("------------------------------------------\n");

    for (std::size_t workers : {1u, 2u, 4u, 8u, 16u}) {
        double best_rate = 0.0;
        for (int round = 0; round < 3; ++round) {
            std::atomic<std::uint64_t> sum{0};
            const auto t0 = Clock::now();
            {
                lab::ThreadPool pool(workers);
                for (std::size_t i = 0; i < tasks; ++i) {
                    pool.submit([&sum, i] {
                        sum.fetch_add(i, std::memory_order_relaxed);
                    });
                }
                pool.shutdown(/*drain=*/true);
            }
            const auto t1 = Clock::now();
            const double ns =
                std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0)
                    .count();
            const double rate = static_cast<double>(tasks) / ns * 1e9;
            if (rate > best_rate) best_rate = rate;
            if (sum.load() == 0xdeadbeefULL) std::fprintf(stderr, "unreachable\n");
        }
        std::printf("%8zu %14.0f %14.3f\n", workers, best_rate,
                    1e6 / best_rate);
    }
    return 0;
}
