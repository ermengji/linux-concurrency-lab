// SPDX-License-Identifier: MIT
//
// Fixed-size thread pool with future-based result retrieval.
//
// Design notes:
//   * Fixed size on purpose: an unbounded pool hides backpressure problems
//     instead of exposing them. The caller decides how many workers it needs.
//   * Tasks are type-erased via std::packaged_task wrapped in shared_ptr,
//     because std::function requires copyable callables and packaged_task is
//     move-only.
//   * Worker threads get a name via pthread_setname_np so that `top -H` and
//     `gdb` show something readable. Small thing, saves real debugging time.
//   * Shutdown drains queued work by default; a hard shutdown is available for
//     the case where the process is going down anyway.

#pragma once

#include <pthread.h>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdio>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace lab {

class ThreadPool {
public:
    // threads == 0 -> hardware_concurrency() (at least 1).
    explicit ThreadPool(std::size_t threads = 0)
        : worker_count_(threads == 0 ? default_worker_count() : threads) {
        workers_.reserve(worker_count_);
        for (std::size_t i = 0; i < worker_count_; ++i) {
            workers_.emplace_back([this, i] { worker_loop(i); });
        }
    }

    ~ThreadPool() { shutdown(/*drain=*/true); }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    template <class F, class... Args>
    auto submit(F&& f, Args&&... args)
        -> std::future<typename std::invoke_result<F, Args...>::type> {
        using R = typename std::invoke_result<F, Args...>::type;
        auto task = std::make_shared<std::packaged_task<R()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        std::future<R> fut = task->get_future();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_) throw std::runtime_error("ThreadPool: submit after shutdown");
            tasks_.emplace([task] { (*task)(); });
            pending_.fetch_add(1, std::memory_order_relaxed);
        }
        cv_.notify_one();
        return fut;
    }

    // drain == true: finish queued tasks, then join.
    // drain == false: drop queued tasks (their futures get broken_promise).
    void shutdown(bool drain = true) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_ && workers_.empty()) return;
            if (drain) {
                stopping_ = true;  // workers exit only when queue is empty
            } else {
                std::queue<std::function<void()>> empty;
                tasks_.swap(empty);
                stopping_ = true;
            }
        }
        cv_.notify_all();
        for (auto& t : workers_) {
            if (t.joinable()) t.join();
        }
        workers_.clear();
    }

    std::size_t worker_count() const { return worker_count_; }
    std::size_t pending() const { return pending_.load(std::memory_order_relaxed); }

private:
    static std::size_t default_worker_count() {
        const unsigned hc = std::thread::hardware_concurrency();
        return hc == 0 ? 1 : static_cast<std::size_t>(hc);
    }

    void worker_loop(std::size_t index) {
        // Readable thread name: helps `top -H -p <pid>` and gdb.
        char name[16];
        std::snprintf(name, sizeof(name), "pool-%zu", index);
        pthread_setname_np(pthread_self(), name);

        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] {
                    return stopping_ || !tasks_.empty();
                });
                if (tasks_.empty()) {
                    if (stopping_) return;  // drained
                    continue;               // spurious wakeup
                }
                job = std::move(tasks_.front());
                tasks_.pop();
            }
            // Run outside the lock so slow tasks do not block submit().
            try {
                job();
            } catch (...) {
                // A throwing task must not kill the worker. The exception is
                // already captured by its future.
            }
            pending_.fetch_sub(1, std::memory_order_relaxed);
        }
    }

    std::vector<std::thread> workers_;
    const std::size_t worker_count_ = 0;
    std::queue<std::function<void()>> tasks_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool stopping_ = false;
    std::atomic<std::size_t> pending_{0};
};

}  // namespace lab
