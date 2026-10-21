/* ps5-native-app-boilerplate - opt-in test session lifecycle.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once
#include "test_lab_profile.hpp"
#include <atomic>
#include <pthread.h>
#include <vector>
namespace prosperolight::test_lab
{
// Publish a synchronous pre-stream refusal and stop the bounded lab lifecycle.
void reject_launch(const char *reason);
struct Observation
{
    bool connected = false, output_valid = false;
    uint64_t unique_frames = 0;
};
struct Timeline
{
    enum State
    {
        connecting,
        waiting_frame,
        stream_ready,
        warmup,
        measuring,
        cooldown,
        stopping
    };
    State state = connecting;
    uint64_t origin = 0, phase_started = 0;
    uint64_t last_frames = 0, measurement_start_frames = 0, measurement_end_frames = 0;
    uint64_t ready_us = 0, measure_start_us = 0, measure_end_us = 0;
    uint64_t progress_last_us = 0, progress_last_frames = 0, progress_longest_us = 0;
    uint64_t progress_stalls = 0, progress_samples = 0, progress_poll_max_us = 0;
    uint64_t progress_previous_poll_us = 0, progress_gap_bins[64]{};
    bool progress_stalled = false;
    bool ready_seen = false, measurement_requested = false, measured = false;
    std::string reason;
    TestProfile profile;
    explicit Timeline(const TestProfile &p, uint64_t now)
        : origin(now), phase_started(now), measurement_requested(!p.coordinated.value_or(false)),
          profile(p)
    {
    }
    const char *name() const
    {
        switch (state)
        {
        case connecting:
            return "connecting";
        case waiting_frame:
            return "waiting_frame";
        case stream_ready:
            return "stream_ready";
        case warmup:
            return "warmup";
        case measuring:
            return "measuring";
        case cooldown:
            return "cooldown";
        case stopping:
            return "stopping";
        }
        return "failed";
    }
    void stop(const char *why)
    {
        if (state != stopping)
        {
            state = stopping;
            reason = why;
        }
    }
    bool command(const std::string &action, uint64_t now)
    {
        if (action == "clock_probe")
            return state != stopping;
        if (action == "cancel")
        {
            stop("CANCELLED");
            return true;
        }
        if (action == "stop")
        {
            stop("STOP_REQUESTED");
            return true;
        }
        if (action == "start_measure" && (state == stream_ready || state == warmup))
        {
            if (!measurement_requested)
            {
                measurement_requested = true;
                state = warmup;
                phase_started = now;
            }
            return true;
        }
        return false;
    }
    void tick(uint64_t now, Observation o)
    {
        if (state == stopping)
            return;
        last_frames = o.unique_frames;
        auto elapsed = [&](uint64_t start, unsigned seconds)
        { return now >= start && now - start >= uint64_t(seconds) * 1000000; };
        if (elapsed(origin, profile.hard_seconds))
        {
            stop("HARD_TIMEOUT");
            return;
        }
        if (ready_seen && !o.output_valid)
        {
            stop("OUTPUT_LOST");
            return;
        }
        switch (state)
        {
        case connecting:
            if (o.connected)
            {
                state = waiting_frame;
                phase_started = now;
            }
            else if (elapsed(origin, profile.startup_seconds))
                stop("STARTUP_TIMEOUT");
            break;
        case waiting_frame:
            if (o.output_valid && o.unique_frames)
            {
                ready_seen = true;
                ready_us = now;
                state = stream_ready;
                phase_started = now;
            }
            else if (elapsed(phase_started, profile.ready_seconds))
                stop("READY_TIMEOUT");
            break;
        case stream_ready:
            state = warmup;
            phase_started = now;
            break;
        case warmup:
            if (measurement_requested && elapsed(phase_started, profile.warmup_seconds))
            {
                state = measuring;
                phase_started = now;
                measure_start_us = now;
                measurement_start_frames = o.unique_frames;
                progress_last_us = progress_previous_poll_us = now;
                progress_last_frames = o.unique_frames;
            }
            break;
        case measuring:
        {
            const uint64_t gap = now >= progress_last_us ? now - progress_last_us : 0;
            if (gap > progress_longest_us)
                progress_longest_us = gap;
            const uint64_t poll =
                now >= progress_previous_poll_us ? now - progress_previous_poll_us : 0;
            if (poll > progress_poll_max_us)
                progress_poll_max_us = poll;
            progress_previous_poll_us = now;
            ++progress_samples;
            if (gap >= 1000000 && !progress_stalled)
            {
                ++progress_stalls;
                progress_stalled = true;
            }
            if (o.unique_frames > progress_last_frames ||
                elapsed(phase_started, profile.measure_seconds))
            {
                unsigned bin = 0;
                for (uint64_t value = gap; value > 1 && bin < 63; value >>= 1)
                    ++bin;
                ++progress_gap_bins[bin];
                progress_last_us = now;
                progress_last_frames = o.unique_frames;
                progress_stalled = false;
            }
            if (elapsed(phase_started, profile.measure_seconds))
            {
                if (o.unique_frames <= measurement_start_frames)
                    stop("NO_MEASURE_FRAMES");
                else
                {
                    measured = true;
                    measure_end_us = now;
                    measurement_end_frames = o.unique_frames;
                    state = cooldown;
                    phase_started = now;
                }
            }
            break;
        }
        case cooldown:
            if (elapsed(phase_started, profile.cooldown_seconds))
                stop("COMPLETE");
            break;
        case stopping:
            break;
        }
    }
};
class Context
{
  public:
    bool begin(const TestProfile &profile, std::string *error = nullptr);
    void attach(Observation (*sample)(void *), void (*stop)(void *), void *data,
                const moonlight_stream_options_t &options);
    bool stop_requested() const
    {
        return stopping_.load();
    }
    void fail(const char *reason);
    void negotiated(int format, int width, int height, int fps);
    void backend_output(unsigned width, unsigned height, unsigned refresh_x100, unsigned pacing,
                        bool vsync, bool hdr);
    void host_cleanup_confirmed()
    {
        host_cleanup_confirmed_.store(true);
    }
    void host_exit_cleanup_result(bool confirmed)
    {
        host_exit_cleanup_.store(confirmed ? 1 : 2);
    }
    void join();
    bool finish(const moonlight_stream_metrics_t *metrics, int result);

  private:
    static void *worker(void *self);
    void run();
    pthread_t thread_{};
    pthread_mutex_t mutex_ = PTHREAD_MUTEX_INITIALIZER;
    bool started_ = false;
    std::atomic<bool> running_{false}, stopping_{false};
    std::atomic<bool> host_cleanup_confirmed_{false};
    std::atomic<unsigned> host_exit_cleanup_{0};
    Timeline timeline_{TestProfile{}, 0};
    Observation (*sample_)(void *) = nullptr;
    void (*stop_)(void *) = nullptr;
    void *data_ = nullptr;
    moonlight_stream_options_t options_{};
    int format_ = 0, width_ = 0, height_ = 0, fps_ = 0;
    unsigned output_width_ = 0, output_height_ = 0, output_refresh_ = 0, output_pacing_ = 0;
    bool output_vsync_ = false, output_hdr_ = false;
    unsigned sequence_ = 2;
    uint64_t wall_started_ = 0;
    uint64_t unique_frames_ = 0;
    uint64_t baseline_sizes_[8]{}, baseline_times_[8]{};
    bool baseline_valid_[8]{};
    std::vector<ControlCommand> commands_;
    std::string last_command_, last_action_, last_result_;
};
Context &context();
} // namespace prosperolight::test_lab
