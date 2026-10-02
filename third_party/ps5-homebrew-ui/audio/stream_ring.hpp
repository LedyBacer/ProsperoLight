// ps5-homebrew-ui - Lock-free ring of stereo float frames (one producer, one consumer).
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <vector>

namespace hui::audio
{

// The game thread writes decoded music; the audio thread reads it. Neither
// side blocks: a short read means the producer fell behind (silence fills in).
class StreamRing
{
  public:
    explicit StreamRing(std::size_t frames = 1u << 16) : samples_(frames * 2), capacity_(frames)
    {
    }

    std::size_t capacity() const
    {
        return capacity_;
    }
    // Frames buffered and not yet read.
    std::size_t available() const
    {
        return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire);
    }
    std::size_t space() const
    {
        return capacity_ - available();
    }

    // Producer: copies up to frames stereo frames; returns how many fit.
    std::size_t write(const float *stereo, std::size_t frames)
    {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        const std::size_t count = std::min(frames, capacity_ - (head - tail));
        for (std::size_t i = 0; i < count; ++i)
        {
            const std::size_t at = ((head + i) % capacity_) * 2;
            samples_[at] = stereo[i * 2];
            samples_[at + 1] = stereo[i * 2 + 1];
        }
        head_.store(head + count, std::memory_order_release);
        return count;
    }

    // Consumer: copies up to frames stereo frames; returns how many were there.
    std::size_t read(float *stereo, std::size_t frames)
    {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t head = head_.load(std::memory_order_acquire);
        const std::size_t count = std::min(frames, head - tail);
        for (std::size_t i = 0; i < count; ++i)
        {
            const std::size_t at = ((tail + i) % capacity_) * 2;
            stereo[i * 2] = samples_[at];
            stereo[i * 2 + 1] = samples_[at + 1];
        }
        tail_.store(tail + count, std::memory_order_release);
        return count;
    }

    // Consumer side only: drops everything buffered.
    void drain()
    {
        tail_.store(head_.load(std::memory_order_acquire), std::memory_order_release);
    }

  private:
    std::vector<float> samples_;
    std::size_t capacity_;
    std::atomic<std::size_t> head_{0}; // frames written, ever
    std::atomic<std::size_t> tail_{0}; // frames read, ever
};

} // namespace hui::audio
