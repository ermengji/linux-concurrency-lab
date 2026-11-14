// SPDX-License-Identifier: MIT
// Minimal self-checking tests. No framework on purpose: the point is that
// anyone can read them and verify the invariants without installing anything.
//
// Build:  cmake --build build --target test_ring_buffer
// Run:    ./build/test_ring_buffer

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <thread>
#include <vector>

#include "latest_frame_buffer.hpp"
#include "spsc_ring_buffer.hpp"
#include "thread_pool.hpp"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("  [FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                               \
        }                                                               \
    } while (0)

void test_fifo_order() {
    std::printf("test_fifo_order\n");
    lab::SpscRingBuffer<int> rb(8);
    CHECK(rb.capacity() == 8);
    for (int i = 0; i < 8; ++i) CHECK(rb.try_push(i));

    int v = -1;
    for (int i = 0; i < 8; ++i) {
        CHECK(rb.try_pop(v));
        CHECK(v == i);
    }
    CHECK(!rb.try_pop(v));  // empty now
}

void test_full_boundary() {
    std::printf("test_full_boundary\n");
    lab::SpscRingBuffer<int> rb(4);
    // Capacity 4 must hold exactly 4 items, no more.
    for (int i = 0; i < 4; ++i) CHECK(rb.try_push(i));
    CHECK(!rb.try_push(99));
    CHECK(rb.size() == 4);

    // Pop one, then push must succeed again.
    int v = 0;
    CHECK(rb.try_pop(v));
    CHECK(v == 0);
    CHECK(rb.try_push(99));
}

void test_capacity_rounding() {
    std::printf("test_capacity_rounding\n");
    lab::SpscRingBuffer<int> rb(100);
    CHECK(rb.capacity() == 128);  // rounded up to power of two
    lab::SpscRingBuffer<int> rb2(1);
    CHECK(rb2.capacity() == 2);
}

void test_cross_thread_integrity() {
    std::printf("test_cross_thread_integrity\n");
    constexpr std::uint64_t kItems = 1000000;
    lab::SpscRingBuffer<std::uint64_t> rb(1024);

    std::atomic<bool> done{false};
    std::uint64_t expected = 0;
    bool ok = true;

    std::thread consumer([&] {
        std::uint64_t v = 0;
        std::uint64_t received = 0;
        while (received < kItems) {
            if (rb.try_pop(v)) {
                if (v != received) ok = false;  // order or loss
                ++received;
            } else if (done.load(std::memory_order_acquire) && rb.empty()) {
                break;
            }
        }
        expected = received;
    });

    for (std::uint64_t i = 0; i < kItems; ++i) {
        while (!rb.try_push(i)) {
        }
    }
    done.store(true, std::memory_order_release);
    consumer.join();

    CHECK(ok);
    CHECK(expected == kItems);
}

void test_latest_frame_buffer() {
    std::printf("test_latest_frame_buffer\n");
    lab::LatestFrameBuffer<std::uint64_t> lfb;
    std::uint64_t frame = 0;
    std::uint64_t seq = 0;

    CHECK(!lfb.try_acquire(0, frame, seq));  // nothing published yet

    lfb.publish(111);
    CHECK(lfb.try_acquire(0, frame, seq));
    CHECK(frame == 111);
    const std::uint64_t first_seq = seq;

    // Consumer is slow: three publishes happen before it looks again.
    lfb.publish(222);
    lfb.publish(333);
    lfb.publish(444);

    // It must get the newest frame, not 222.
    CHECK(lfb.try_acquire(first_seq, frame, seq));
    CHECK(frame == 444);

    // Same sequence must not be handed out twice.
    CHECK(!lfb.try_acquire(seq, frame, seq));

    // And the skipped frames are accounted for, not silently lost.
    CHECK(lfb.dropped() >= 2);
    std::printf("  dropped frames: %llu\n",
                static_cast<unsigned long long>(lfb.dropped()));
}

void test_pool_runs_all_tasks() {
    std::printf("test_pool_runs_all_tasks\n");
    lab::ThreadPool pool(4);
    std::atomic<int> counter{0};
    for (int i = 0; i < 1000; ++i) {
        pool.submit([&counter] { counter.fetch_add(1, std::memory_order_relaxed); });
    }
    pool.shutdown(true);
    CHECK(counter.load() == 1000);
}

void test_pool_propagates_exception() {
    std::printf("test_pool_propagates_exception\n");
    lab::ThreadPool pool(2);
    auto fut = pool.submit([]() -> int {
        throw std::runtime_error("boom");
        return 0;  // unreachable, silences -Wreturn-type
    });
    bool caught = false;
    try {
        fut.get();
    } catch (const std::runtime_error&) {
        caught = true;
    }
    CHECK(caught);
    // The worker must still be alive after a task threw.
    auto fut2 = pool.submit([] { return 42; });
    CHECK(fut2.get() == 42);
}

}  // namespace

int main() {
    test_fifo_order();
    test_full_boundary();
    test_capacity_rounding();
    test_cross_thread_integrity();
    test_latest_frame_buffer();
    test_pool_runs_all_tasks();
    test_pool_propagates_exception();

    if (g_failures == 0) {
        std::printf("\nALL TESTS PASSED\n");
        return 0;
    }
    std::printf("\n%d CHECK(S) FAILED\n", g_failures);
    return 1;
}
