// SPDX-License-Identifier: MIT
//
// LatestFrameBuffer: a slot that always holds the newest frame.
//
// Motivation (video pipelines): when inference is slower than capture, queueing
// every frame only adds latency. What the consumer actually wants is "give me
// the newest frame I have not seen yet, and tell me how many I missed".
//
// Design trade-off, stated explicitly because it matters:
//   A lock-free design is possible here, but the payload (a frame) is much
//   larger than a pointer, so a seqlock would copy the whole frame on every
//   retry. Since the publish rate is bounded by the camera (tens of Hz), the
//   critical section is short and contention is negligible. We therefore use a
//   mutex + condition variable and spend the complexity budget elsewhere.
//   This is a deliberate choice, not an oversight.
//
// The sequence number doubles as a "have I already processed this frame" token,
// which is how the consumer avoids running inference twice on the same frame.

#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace lab {

template <typename Frame>
class LatestFrameBuffer {
public:
    LatestFrameBuffer() = default;

    LatestFrameBuffer(const LatestFrameBuffer&) = delete;
    LatestFrameBuffer& operator=(const LatestFrameBuffer&) = delete;

    // Producer side: overwrite the slot. Never blocks for long.
    // Returns the number of frames that were skipped because the consumer was
    // not fast enough.
    std::uint64_t publish(const Frame& frame) {
        std::uint64_t skipped = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (has_value_) skipped = seq_ - last_consumed_seq_;
            slot_ = frame;
            has_value_ = true;
            ++seq_;
        }
        cv_.notify_one();
        dropped_ += skipped;
        return skipped;
    }

    // Consumer side: block until a frame newer than last_seq is available.
    // Returns false on timeout. On success, out_seq receives the new sequence
    // number, which the caller passes back in on the next call.
    bool acquire(std::uint64_t last_seq, Frame& out, std::uint64_t& out_seq,
                 std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        const bool ready = cv_.wait_for(lock, timeout, [&] {
            return has_value_ && seq_ > last_seq;
        });
        if (!ready) return false;
        out = slot_;
        out_seq = seq_;
        last_consumed_seq_ = seq_;
        return true;
    }

    // Non-blocking variant for poll-style consumers.
    bool try_acquire(std::uint64_t last_seq, Frame& out, std::uint64_t& out_seq) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!has_value_ || seq_ <= last_seq) return false;
        out = slot_;
        out_seq = seq_;
        last_consumed_seq_ = seq_;
        return true;
    }

    std::uint64_t dropped() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return dropped_ + (has_value_ ? seq_ - last_consumed_seq_ - 1 : 0);
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    Frame slot_{};
    bool has_value_ = false;
    std::uint64_t seq_ = 0;             // number of publishes so far
    std::uint64_t last_consumed_seq_ = 0;
    std::uint64_t dropped_ = 0;
};

}  // namespace lab
