#include "app_storage.hpp"
#include "frame_cadence.hpp"
#include "frame_pacing.hpp"
#include "ps5_pacing_feedback.hpp"
#include "scanout_trace.hpp"
#if PROSPEROLIGHT_TEST_LAB
#include "test_lab_output_evidence.hpp"
#include "test_lab_lifecycle.hpp"
#endif
#include "pacing_decision_trace.hpp"
#include "lan_http_report.hpp"
#include <cstdio>
#include <new>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "native_agc_present.hpp"
#include "stream_overlay.hpp"
#include "stream_backend.hpp"
#include "video/ps5_presentation_stats.hpp"
#include "video/pyrowave_video_backend.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <deque>
#include <exception>
#include <mutex>
#include <pthread.h>

#ifndef PROSPEROLIGHT_FLIP_POLL_US
#define PROSPEROLIGHT_FLIP_POLL_US 500
#endif
static_assert(PROSPEROLIGHT_FLIP_POLL_US >= 100 && PROSPEROLIGHT_FLIP_POLL_US <= 2000,
              "Scanout polling must match the native presentation bounds");

extern "C" int wsi_ps5_release_videoout(void);
namespace prosperolight::pyrowave
{
namespace
{
unsigned selected_pacing = 0, selected_vrr_profile = 1;
struct Frame
{
    std::vector<uint8_t> bytes;
    std::vector<PyroWaveFraming::Segment> segments;
    uint64_t presentation_us{}, receive_us{}, enqueue_us{}, queued_us{};
    uint32_t rtp_timestamp{};
    uint16_t critical{};
    int number{};
};
struct Session
{
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<Frame> queue;
    PacingDecisionTrace decisions{"pyrowave", selected_pacing};
    bool running{}, started{};
    pthread_t worker{};
    unsigned width{}, height{}, fps{};
    bool chroma444{}, hdr{};
    std::unique_ptr<VulkanContext> context;
    std::unique_ptr<PyroWaveVideoBackend> backend;
    std::atomic<uint64_t> incoming{0}, decoded{0}, shown{0}, stale{0}, partial{0}, rejected{0},
        losses{0}, bytes{0};
    size_t high_water{};
};
std::unique_ptr<Session> session;
void (*error_callback)(int) = nullptr;
std::atomic<uint64_t> presented_count{0};
std::atomic<uint64_t> confirmed_unique_count{0};
std::atomic<bool> hdr_active{false}, hdr_known{false}, requested_hdr{false};
// moonlight-common-c queries this on its UDP receiver, not the renderer.
// A single atomic deadline snapshot prevents lock inversion with the queue.
moonlight::Ps5ReceiveDeadline receive_deadline;
uint64_t pyrowave_receive_deadline(uint32_t rtp)
{
    return receive_deadline.lookup(rtp, LiGetMicroseconds());
}
bool selected_vsync = true, selected_tv_safe = true;
std::mutex error_mutex;
char last_error[192]{};
void record_error(const char *reason)
{
    std::lock_guard<std::mutex> lock(error_mutex);
    snprintf(last_error, sizeof(last_error), "%s", reason);
}
uint64_t now_us()
{
    timespec t{};
    clock_gettime(CLOCK_MONOTONIC, &t);
    return uint64_t(t.tv_sec) * 1000000 + t.tv_nsec / 1000;
}

// Worker-owned bounded trace: allocate once, export only when the worker exits.
// VideoOut count observations are not hardware scanout timestamps.
struct OutputTrace
{
    struct Sample
    {
        int frame;
        uint64_t pts, ready, submit, observed, flips;
    };
    static constexpr size_t capacity = 32768;
    std::unique_ptr<Sample[]> samples;
    size_t count{}, omitted{}, next{};
    moonlight::FramePacing &pacer;
    moonlight::VrrRepeatPolicy &vrr;
    unsigned mode, fps;
    OutputTrace(moonlight::FramePacing &p, moonlight::VrrRepeatPolicy &v, unsigned m, unsigned f)
        : pacer(p), vrr(v), mode(m), fps(f)
    {
        if (prosperolight_logs_enabled())
            samples.reset(new (std::nothrow) Sample[capacity]);
    }
    void append(const Sample &sample)
    {
        if (!samples)
            return;
        samples[next] = sample;
        next = (next + 1) % capacity;
        if (count == capacity)
            ++omitted;
        else
            ++count;
    }
    ~OutputTrace()
    {
        if (ps5_vrr_output_active())
            log_line(
                "PyroWave VRR scheduler: period_us=%llu pictures=%llu repeats=%llu "
                "wait_us=%llu late_max_us=%llu submission_gap_max_us=%llu source_fps=%u "
                "repeat_factor=%u interval_us=%llu profile=%u vrr_reserve_us=%llu "
                "feedback_misses=%llu feedback_ambiguous=%llu feedback_epochs=%llu",
                (unsigned long long)vrr.period(), (unsigned long long)vrr.stats.pictures,
                (unsigned long long)vrr.stats.repeats, (unsigned long long)vrr.stats.wait_total_us,
                (unsigned long long)vrr.stats.late_max_us, (unsigned long long)vrr.stats.gap_max_us,
                vrr.source_rate(), vrr.repeat_factor(), (unsigned long long)vrr.interval(),
                selected_vrr_profile, (unsigned long long)vrr.playout_reserve_us(),
                (unsigned long long)vrr.feedback_misses(),
                (unsigned long long)vrr.feedback_ambiguous(),
                (unsigned long long)vrr.feedback_epoch_resets());
        else
            log_line("PyroWave pacing result: mode=%u period_us=%llu reserve_us=%llu "
                     "submissions=%llu "
                     "misses=%llu resets=%llu late_max_us=%llu spacing_error_max_us=%llu",
                     mode, (unsigned long long)pacer.stats.period_us,
                     (unsigned long long)pacer.stats.reserve_us,
                     (unsigned long long)pacer.stats.submissions,
                     (unsigned long long)pacer.stats.misses, (unsigned long long)pacer.stats.resets,
                     (unsigned long long)pacer.stats.late_max_us,
                     (unsigned long long)pacer.stats.spacing_error_max_us);
        if (!samples || !prosperolight_logs_enabled())
            return;
        char temporary[176], destination[176];
        snprintf(destination, sizeof(destination), "%s/pyrowave-output-mode%u.csv",
                 storage::paths().performance, mode);
        snprintf(temporary, sizeof(temporary), "%s.tmp", destination);
        FILE *file = fopen(temporary, "w");
        if (!file)
            return;
        bool ok = fprintf(file,
                          "# mode=%u,fps=%u,count=%zu,omitted=%zu\n"
                          "frame,pts_us,ready_us,submit_us,observed_us,flip_count\n",
                          mode, fps, count, omitted) > 0;
        for (size_t i = 0; ok && i < count; ++i)
        {
            const auto &v = samples[(count == capacity ? next + i : i) % capacity];
            ok = fprintf(file, "%d,%llu,%llu,%llu,%llu,%llu\n", v.frame, (unsigned long long)v.pts,
                         (unsigned long long)v.ready, (unsigned long long)v.submit,
                         (unsigned long long)v.observed, (unsigned long long)v.flips) > 0;
        }
        if (fclose(file) != 0)
            ok = false;
        if (!ok || rename(temporary, destination) != 0)
            remove(temporary);
    }
};

struct PacingWait
{
    Session *session;
    moonlight::FramePacing *pacer;
    const Frame *frame;
    unsigned mode;
    uint32_t refresh;
    uint64_t ready_us{}, submit_us{};
    moonlight::VrrRepeatPolicy *vrr{};
    uint64_t previous_request{};
    bool successor_queued{};
    uint64_t frame_queue_age_us{};
    uint64_t frame_queued_us{};
    uint64_t frame_dequeued_us{};
    moonlight::Ps5ReadinessEstimator *readiness{};
    bool repeat{};
    bool cancelled{}; // Stop requested after acquiring swapchain image.
    void (*observe_output)(void *, const PresentationStats &, uint64_t){};
    void *observation_context{};
    uint64_t minimum_deadline{};
};

bool admit_idle_repeat(void *context)
{
    auto &wait = *static_cast<PacingWait *>(context);
    auto &s = *wait.session;
    std::lock_guard<std::mutex> lock(s.mutex);
    const bool admitted = s.running && s.queue.empty();
    if (!admitted)
        s.decisions.record(0, 0, now_us(), wait.minimum_deadline, "repeat-superseded");
    return admitted;
}

void wait_prepared_frame(void *context)
{
    auto &wait = *static_cast<PacingWait *>(context);
    auto &s = *wait.session;
    const uint64_t started = now_us();
    wait.ready_us = started;
    // Paced modes observe GPU-prepared service. Unpaced does not wait for
    // this fence, so its callback is not readiness evidence.
    if (wait.mode != 0 && !wait.repeat && wait.readiness && wait.frame_dequeued_us)
        wait.readiness->observe(wait.frame_dequeued_us, started);
    if (wait.mode == 0)
    {
        wait.submit_us = started;
        return;
    }
    if (!wait.repeat && ps5_vrr_output_active())
    {
        // GPU prepared fence has completed; this is true presentation
        // readiness. Decoder ingress/packet arrival is not GPU readiness.
        wait.vrr->observe_readiness(wait.frame->presentation_us, started,
                                    uint32_t(wait.frame->number));
        // Capture freshly queued successors at the actual decision point,
        // rather than using the queue snapshot before GPU preparation.
        {
            std::lock_guard<std::mutex> lock(s.mutex);
            wait.successor_queued = !s.queue.empty();
        }
        if (wait.frame_queued_us && started > wait.frame_queued_us)
            wait.frame_queue_age_us = started - wait.frame_queued_us;
    }
    const uint64_t display_period = wait.refresh ? UINT64_C(100000000) / wait.refresh : 0;
    const uint64_t nominal = wait.pacer->stats.period_us;
    const uint64_t matched =
        display_period ? std::max<uint64_t>(1, (nominal + display_period / 2) / display_period) *
                             display_period
                       : nominal;
    const uint64_t mismatch = matched > nominal ? matched - nominal : nominal - matched;
    const bool fractional_fixed = selected_vsync && display_period && mismatch > nominal / 100;
    uint64_t flip_anchor = 0;
    if (ps5_vrr_output_active() || fractional_fixed)
    {
        // A ready source image replaces the next repeat slot. Retire the
        // previous scanout first; a GPU fence cannot establish this floor.
        for (;;)
        {
            const auto output = ps5_presentation_stats();
            if (!output.available)
                fail("Paced scanout counters unavailable");
            const uint64_t observed = now_us();
            if (wait.observe_output)
                wait.observe_output(wait.observation_context, output, observed);
            wait.vrr->scanned(output.flip_count, observed);
            if (!wait.previous_request ||
                moonlight::idle_repeat_ready(wait.previous_request, output.shown, output.available))
            {
                flip_anchor = now_us();
                break;
            }
            {
                std::unique_lock<std::mutex> lock(s.mutex);
                if (!s.running)
                    return;
                s.wake.wait_for(lock, std::chrono::microseconds(PROSPEROLIGHT_FLIP_POLL_US),
                                [&] { return !s.running; });
            }
            if (now_us() - started > 100000)
                fail("Paced previous scanout did not complete within 100ms");
        }
    }
    // VRR has ONE admission clock for fresh pictures and repeats. Applying
    // the fixed/source pacer as well can postpone a prepared picture after a
    // repeat and create an additional 30-50 ms transition gap.
    const uint64_t pacing_deadline =
        ps5_vrr_output_active()
            ? (wait.repeat ? wait.minimum_deadline
                           : wait.vrr->picture_target(now_us(), wait.successor_queued,
                                                      wait.frame_queue_age_us))
            : wait.pacer->target(
                  wait.frame->number, wait.frame->presentation_us, started,
                  selected_vsync ? wait.refresh : 0, fractional_fixed ? flip_anchor : 0, 0,
                  // Fixed HFR: learn a bounded renderer lead
                  // rather than always assuming exactly 1 ms.
                  wait.readiness
                      ? std::clamp<uint64_t>(wait.readiness->percentile_us() / 4, 250, 2000)
                      : 1000);
    const uint64_t deadline = std::max(pacing_deadline, wait.minimum_deadline);
    if (!wait.repeat && wait.frame && wait.readiness)
    {
        // Convert the PS5 CLOCK_MONOTONIC target to LiGetMicroseconds time
        // right here. The existing patched common-c callback will only
        // shorten an already incomplete *noncritical* PyroWave frame.
        const uint64_t sampled_mono = now_us();
        const uint64_t sampled_li = LiGetMicroseconds();
        receive_deadline.publish(wait.frame->rtp_timestamp, deadline, sampled_mono, sampled_li,
                                 wait.readiness->percentile_us(), true);
    }
    s.decisions.record(wait.frame->number, wait.frame->presentation_us, now_us(), deadline,
                       wait.repeat ? "repeat-target" : "target");
    std::unique_lock<std::mutex> lock(s.mutex);
    while (s.running)
    {
        const uint64_t current = now_us();
        if (current >= deadline)
            break;
        const uint64_t remaining = deadline - current;
        const uint64_t lead = wait.pacer->wake_lead_us();
        if (remaining > lead)
            s.wake.wait_for(lock,
                            std::chrono::microseconds(std::min<uint64_t>(remaining - lead, 1000)),
                            [&] { return !s.running; });
        else
        {
            lock.unlock();
            while (now_us() < deadline)
                __builtin_ia32_pause();
            lock.lock();
        }
    }
    if (!s.running)
    {
        // The Vulkan renderer already acquired a swapchain image. The caller
        // must still present/drain it before destroying its semaphores; do not
        // report that abandoned deadline as a successful unique frame.
        wait.cancelled = true;
        return;
    }
    const uint64_t submitted = now_us();
    s.decisions.record(wait.frame->number, wait.frame->presentation_us, submitted, deadline,
                       wait.repeat ? "repeat-submit" : "submit");
    wait.submit_us = submitted;
    if (ps5_vrr_output_active())
        wait.vrr->waited(started, deadline, submitted);
    else
        wait.pacer->submitted(deadline, submitted, submitted - started);
}

void *worker(void *)
{
    // Initialization and this worker use the single Granite index serially.
    // Without registration Granite logs and flushes stderr on each decode.
    Util::register_thread_index(0);
    auto &s = *session;
    moonlight::FramePacing pacer;
    moonlight::SourceTimestamp source_clock;
    const unsigned mode = selected_pacing;
    const uint32_t selected_refresh_x100 =
        static_cast<uint32_t>(s.backend->refresh_hz() * 100 + 0.5);
    pacer.reset(s.fps);
    const bool paced = mode != 0;
    moonlight::VrrRepeatPolicy vrr_repeats;
    vrr_repeats.reset(s.fps);
    vrr_repeats.configure(selected_vrr_profile, selected_refresh_x100);
    moonlight::VrrPreparationLead preparation_lead;
    moonlight::Ps5ReadinessEstimator readiness;
    unsigned cadence_rate = s.fps, cadence_copies = vrr_repeats.repeat_factor();
    OutputTrace trace(pacer, vrr_repeats, mode, s.fps);
    ScanoutTrace scanout_trace("pyrowave", mode);
    uint64_t repeat_argument = 0;
    log_line("PyroWave pacing: mode=%u requested_fps=%u selected_refresh_x100=%u "
             "source_clock=1 variable_output=%u flip_poll_us=%u",
             mode, s.fps, selected_refresh_x100, ps5_vrr_output_active() ? 1u : 0u,
             unsigned(PROSPEROLIGHT_FLIP_POLL_US));
    uint64_t last = now_us(), incoming = 0, decoded = 0, shown = 0, bytes = 0;
    double decode_ms = 0, render_ms = 0;
    uint64_t samples = 0, unique_presented = 0, prior_unique_presented = 0;
    native_agc_metrics_t initial_hud{};
    initial_hud.video_codec = 2;
    initial_hud.output_refresh_x100 = selected_refresh_x100;
    initial_hud.stream_width = s.width;
    initial_hud.stream_height = s.height;
    initial_hud.pacing_mode = mode;
    initial_hud.vrr_api_active = ps5_vrr_output_active();
    initial_hud.vsync_enabled = native_agc_vsync_active() || mode == 2;
    s.backend->update_hud(&initial_hud, native_agc_hud_enabled() != 0);

    auto counters = ps5_presentation_stats();
    uint64_t vblanks = counters.vblanks;
    bool refresh_reported = false;
    unsigned windows = 0;
    uint64_t stall_count = 0, last_stall_log = 0;
    uint64_t last_scanout_us = 0, last_picture_us = 0, repeated = 0;
    uint64_t last_observed_picture = 0;
    // Hold at most one unique frame for confirmed-flip feedback. If several
    // frames arrive before a poll, skip evidence instead of assigning a
    // wrong display time to earlier pictures.
    struct PendingFeedback
    {
        bool valid{};
        uint32_t number{};
        uint64_t pts{}, ready{}, requested_argument{}, requested_at_us{};
    } pending_feedback;
#if PROSPEROLIGHT_TEST_LAB
    prosperolight::test_lab::OutputEvidence test_output_evidence;
#endif
    const auto observe_feedback = [&](const auto &output)
    {
#if PROSPEROLIGHT_TEST_LAB
        if (output.available && test_output_evidence.observe(output.shown, output.flip_count))
            confirmed_unique_count.fetch_add(1, std::memory_order_relaxed);
#endif
        if (output.available && pending_feedback.valid &&
            output.shown == pending_feedback.requested_argument)
        {
            vrr_repeats.observe_output_feedback(
                pending_feedback.number, pending_feedback.pts, pending_feedback.ready, now_us(),
                ps5_vrr_output_active(), pending_feedback.requested_at_us);
            pending_feedback.valid = false;
        }
    };
    const auto record_output = [&](const PresentationStats &output, uint64_t observed)
    {
#if PROSPEROLIGHT_TEST_LAB
        if (output.available && test_output_evidence.observe(output.shown, output.flip_count))
            confirmed_unique_count.fetch_add(1, std::memory_order_relaxed);
#endif
        if (!output.available)
            return;
        scanout_trace.observe(output.flip_count, output.shown, observed,
                              output.shown == repeat_argument);
        // The same worker owns feedback during all previous-flip polls.
        if (pending_feedback.valid && output.shown == pending_feedback.requested_argument)
        {
            vrr_repeats.observe_output_feedback(
                pending_feedback.number, pending_feedback.pts, pending_feedback.ready, observed,
                ps5_vrr_output_active(), pending_feedback.requested_at_us);
            pending_feedback.valid = false;
        }
    };
    const auto record_poll = [](void *context, const PresentationStats &output, uint64_t observed)
    { (*static_cast<const decltype(record_output) *>(context))(output, observed); };
    bool repeating = false;
    moonlight::RepeatRetryPolicy repeat_retry;
    uint64_t skipped_repeats = 0;
    try
    {
        for (;;)
        {
            const uint64_t iteration_started = now_us();
            Frame frame;
            {
                std::unique_lock<std::mutex> lock(s.mutex);
                while (s.running && s.queue.empty())
                {
                    // A fixed-refresh television holds the last picture by itself.
                    if (!last_scanout_us || !ps5_vrr_output_active())
                    {
                        s.wake.wait(lock, [&] { return !s.running || !s.queue.empty(); });
                        continue;
                    }
                    // vkQueuePresent/GPU fences are not display completion.
                    // Observe the last WSI flip argument before scheduling an
                    // idle repeat; queued source frames must drain first.
                    const auto output = ps5_presentation_stats();
                    observe_feedback(output);
                    if (output.available)
                        scanout_trace.observe(output.flip_count, output.shown, now_us(),
                                              output.shown == repeat_argument);
                    if (output.available && output.shown != last_observed_picture)
                    {
                        last_observed_picture = output.shown;
                        last_scanout_us = now_us();
                        vrr_repeats.scanned(output.flip_count, last_scanout_us);
                    }
                    if (!moonlight::idle_repeat_ready(s.backend->requested(), output.shown,
                                                      output.available))
                    {
                        s.wake.wait_for(lock, std::chrono::microseconds(PROSPEROLIGHT_FLIP_POLL_US),
                                        [&] { return !s.running || !s.queue.empty(); });
                        continue;
                    }
                    const uint64_t idle_delay = moonlight::idle_scanout_delay_us(
                        s.fps, selected_refresh_x100, ps5_vrr_output_active(), repeating);
                    const uint64_t repeat_deadline = repeat_retry.deadline(
                        ps5_vrr_output_active() ? vrr_repeats.idle_deadline()
                                                : last_scanout_us + idle_delay);
                    const uint64_t current = now_us();
                    // Prepare retained planes before their display slot, just like a
                    // fresh picture. GPU preparation must not shift repeat flips.
                    const uint64_t lead =
                        std::max<uint64_t>(preparation_lead.lead_us(),
                                           std::min<uint64_t>(6000, readiness.percentile_us() / 2));
                    // Normal motion: do not acquire an optional duplicate
                    // early and wait while a fresh frame arrives. Sparse/LFC
                    // output retains its preparation lead for regular repeats.
                    const uint64_t repeat_lead = vrr_repeats.compensating() ? lead : 0;
                    const uint64_t prepare_deadline = repeat_deadline > repeat_lead
                                                          ? repeat_deadline - repeat_lead
                                                          : repeat_deadline;
                    if (current < prepare_deadline)
                    {
                        s.wake.wait_for(lock, std::chrono::microseconds(prepare_deadline - current),
                                        [&] { return !s.running || !s.queue.empty(); });
                        continue;
                    }
                    lock.unlock();
                    // Re-render retained decoded planes, never re-decode the
                    // compressed frame or acquire a decoder/network slot.
                    Frame retained{};
                    PacingWait repeat_wait{&s, &pacer, &retained, mode, selected_refresh_x100};
                    repeat_wait.observe_output = record_poll;
                    repeat_wait.observation_context =
                        const_cast<void *>(static_cast<const void *>(&record_output));
                    repeat_wait.repeat = true;
                    repeat_wait.minimum_deadline = repeat_deadline;
                    repeat_wait.vrr = &vrr_repeats;
                    repeat_wait.previous_request = s.backend->requested();
                    repeat_wait.readiness = &readiness;
                    s.backend->update_hud(nullptr, native_agc_hud_enabled() != 0);
                    const auto repeated_timing = s.backend->present(
                        wait_prepared_frame, &repeat_wait, true, true, admit_idle_repeat);
                    if (repeat_wait.cancelled)
                    {
                        lock.lock();
                        break; // Acquired image drained by backend; stop the worker.
                    }
                    if (repeated_timing.repeat_superseded)
                    {
                        lock.lock();
                        continue; // Fresh source wins; no WSI failure/backoff.
                    }
                    if (repeated_timing.repeat_skipped)
                    {
                        ++skipped_repeats;
                        repeat_retry.failed(now_us());
                        if (skipped_repeats == 1 || skipped_repeats % 240 == 0)
                            log_line("PyroWave repeat deferred: swapchain-not-ready "
                                     "skipped=%llu retry_level=%u",
                                     (unsigned long long)skipped_repeats, repeat_retry.failures());
                        s.decisions.record(0, 0, now_us(), repeat_deadline, "repeat-skipped");
                        lock.lock();
                        continue;
                    }
                    preparation_lead.observe(uint64_t(
                        std::max(0.0, repeated_timing.acquire_ms + repeated_timing.record_ms +
                                          repeated_timing.submit_ms +
                                          repeated_timing.prepared_wait_ms) *
                        1000));
                    repeat_retry.succeeded();
                    repeat_argument = s.backend->requested();
                    last_scanout_us = now_us();
                    s.decisions.record(0, 0, repeat_wait.submit_us, repeat_deadline, "repeat");
                    vrr_repeats.repeated(repeat_wait.submit_us ? repeat_wait.submit_us
                                                               : last_scanout_us);
                    repeating = true;
                    ++repeated;
                    if (repeated == 1 || repeated % 600 == 0)
                        log_line("PyroWave scanout repeat: count=%llu delay_us=%llu vrr_api=%d "
                                 "compensation=%d",
                                 (unsigned long long)repeated,
                                 (unsigned long long)(ps5_vrr_output_active()
                                                          ? vrr_repeats.interval()
                                                          : idle_delay),
                                 ps5_vrr_output_active(), vrr_repeats.compensating());
                    lock.lock();
                }
                if (!s.running)
                    break;
                if (paced)
                {
                    // Keep a small FIFO reserve; independent frames may be
                    // skipped before decode only when a successor exists.
                    while (s.queue.size() > 1 &&
                           now_us() > s.queue.front().queued_us +
                                          pacer.admission_limit_us(
                                              uint64_t(uint32_t(s.queue[1].rtp_timestamp -
                                                                s.queue.front().rtp_timestamp)) *
                                                  1000000 / 90000,
                                              ps5_vrr_output_active() && vrr_repeats.grid_active()
                                                  ? vrr_repeats.interval()
                                                  : 0))
                    {
                        s.decisions.record(s.queue.front().number, s.queue.front().rtp_timestamp,
                                           now_us(), 0, "drop-age");
                        s.queue.pop_front();
                        ++s.stale;
                    }
                    frame = std::move(s.queue.front());
                    s.queue.pop_front();
                }
                else
                {
                    frame = std::move(s.queue.back());
                    s.stale += s.queue.size() - 1;
                    s.queue.clear();
                }
            }

            const uint64_t dequeued = now_us();
            // Unpaced never waits for a stale image when an actual successor
            // arrived after the first latest-picture selection. Skip work
            // before PyroWave parsing/GPU submission, not after acquiring WSI.
            if (!paced)
            {
                std::lock_guard<std::mutex> lock(s.mutex);
                if (!s.queue.empty())
                {
                    ++s.stale;
                    s.decisions.record(frame.number, frame.rtp_timestamp, now_us(), 0,
                                       "drop-unpaced-superseded");
                    continue;
                }
            }
            // A successor is a real queued picture, not an inferred future tick.
            bool successor_queued = false;
            {
                std::lock_guard<std::mutex> lock(s.mutex);
                successor_queued = !s.queue.empty();
            }
            const uint64_t frame_queue_age_us =
                dequeued > frame.queued_us ? dequeued - frame.queued_us : 0;
            s.decisions.record(frame.number, frame.rtp_timestamp, dequeued, 0, "dequeue");
            frame.presentation_us = source_clock.update(frame.rtp_timestamp, frame.presentation_us);
            PyroWaveFraming::Frame parsed;
            std::string error;
            if (!PyroWaveFraming::parse(frame.bytes.data(), frame.bytes.size(), frame.segments,
                                        frame.critical, {int(s.width), int(s.height), s.chroma444},
                                        parsed, error) ||
                (parsed.partial && !parsed.coarseLevelIntact) ||
                !s.backend->ingest(frame.bytes.data(), parsed.spans, parsed.partial))
            {
                const auto rejected = ++s.rejected;
                if (rejected == 1 || rejected % 120 == 0)
                    log_line("PyroWave rejected frame %d: %s", frame.number, error.c_str());
                continue;
            }
            if (parsed.partial)
                ++s.partial;
            ++s.decoded;
            s.backend->update_hud(nullptr, native_agc_hud_enabled() != 0);
            const bool was_compensating = vrr_repeats.compensating();
            vrr_repeats.observe_picture(frame.presentation_us, uint32_t(frame.number));
            // Readiness is recorded after GPU preparation in
            // wait_prepared_frame(), not at network queue insertion.
            if (ps5_vrr_output_active() && was_compensating && !vrr_repeats.compensating())
            {
                pacer.resume(now_us());
                log_line("PyroWave VRR recovery: source_fps=%u phase_resume=1",
                         vrr_repeats.source_rate());
            }
            if (ps5_vrr_output_active() && (cadence_rate != vrr_repeats.source_rate() ||
                                            cadence_copies != vrr_repeats.repeat_factor()))
            {
                cadence_rate = vrr_repeats.source_rate();
                cadence_copies = vrr_repeats.repeat_factor();
                log_line("PyroWave VRR cadence: source_fps=%u repeat_factor=%u "
                         "target_refresh_x100=%u interval_us=%llu",
                         cadence_rate, cadence_copies, vrr_repeats.target_refresh_x100(),
                         (unsigned long long)vrr_repeats.interval());
            }
            PacingWait wait{&s, &pacer, &frame, mode, selected_refresh_x100};
            wait.observe_output = record_poll;
            wait.observation_context =
                const_cast<void *>(static_cast<const void *>(&record_output));
            wait.vrr = &vrr_repeats;
            wait.successor_queued = successor_queued;
            wait.frame_queue_age_us = frame_queue_age_us;
            wait.frame_queued_us = frame.queued_us;
            wait.frame_dequeued_us = dequeued;
            wait.readiness = &readiness;
            wait.previous_request = s.backend->requested();
            const uint64_t preparation_started = now_us();
            auto timing = s.backend->present(wait_prepared_frame, &wait, paced);
            if (wait.cancelled)
            {
                s.decisions.record(frame.number, frame.presentation_us, now_us(), 0,
                                   "shutdown-present-drained");
                break; // Acquired WSI image was still released by vkQueuePresent.
            }
            preparation_lead.observe(
                uint64_t(std::max(0.0, timing.acquire_ms + timing.record_ms + timing.submit_ms +
                                           timing.prepared_wait_ms) *
                         1000));
            const uint64_t finished = now_us();
            last_scanout_us = finished;
            repeating = repeating && last_picture_us &&
                        finished - last_picture_us > UINT64_C(1500000) / s.fps;
            last_picture_us = finished;
            repeat_retry.succeeded();
            ++unique_presented;
            const uint64_t actual_submit = wait.submit_us ? wait.submit_us : finished;
            stream_overlay::graph.record(native_agc_hud_enabled() != 0 && s.backend->hud_visible(),
                                         actual_submit);
            vrr_repeats.presented(actual_submit);
            // Consume previous completion before replacing its unique-frame evidence.
            observe_feedback(ps5_presentation_stats());
            if (pending_feedback.valid)
                vrr_repeats.invalidate_output_feedback();
#if PROSPEROLIGHT_TEST_LAB
            test_output_evidence.submitted(s.backend->requested(), uint32_t(frame.number));
#endif
            pending_feedback = {true,          uint32_t(frame.number), frame.presentation_us,
                                wait.ready_us, s.backend->requested(), actual_submit};
            if (finished - preparation_started > 50000 || preparation_started - dequeued > 50000)
            {
                ++stall_count;
                if (finished - last_stall_log >= 1000000)
                {
                    log_line("PyroWave stall: count=%llu frame=%d dequeue_us=%llu "
                             "ingest_us=%llu "
                             "acquire_ms=%.3f record_ms=%.3f submit_ms=%.3f "
                             "prepared_wait_ms=%.3f "
                             "pacing_ms=%.3f present_ms=%.3f completion_ms=%.3f gpu_ms=%.3f",
                             (unsigned long long)stall_count, frame.number,
                             (unsigned long long)(dequeued - iteration_started),
                             (unsigned long long)(preparation_started - dequeued),
                             timing.acquire_ms, timing.record_ms, timing.submit_ms,
                             timing.prepared_wait_ms, timing.pacing_ms, timing.present_ms,
                             timing.completion_ms, timing.total_ms);
                    last_stall_log = finished;
                }
            }
            decode_ms += timing.decode_ms;
            render_ms += timing.render_ms;
            ++samples;
            counters = ps5_presentation_stats();
            if (counters.available)
            {
                scanout_trace.observe(counters.flip_count, counters.shown, now_us(),
                                      counters.shown == repeat_argument);
                observe_feedback(counters);
            }
            if (!counters.available)
            {
                record_error("VideoOut presentation counters unavailable");
                log_line("PyroWave worker failed: VideoOut presentation counters "
                         "unavailable");
                if (error_callback)
                    error_callback(-1);
                return nullptr;
            }
            trace.append({frame.number, frame.presentation_us, wait.ready_us,
                          wait.submit_us ? wait.submit_us : now_us(), now_us(),
                          counters.flip_count});
            s.shown = counters.flip_count;
            presented_count = counters.flip_count;
            const uint64_t now = now_us(), elapsed = now - last;
            if (elapsed >= 1000000)
            {
                size_t depth, high;
                {
                    std::lock_guard<std::mutex> lock(s.mutex);
                    depth = s.queue.size();
                    high = s.high_water;
                }
                const double seconds = elapsed / 1e6;
                const double observed_refresh_hz =
                    counters.vblank_available ? (counters.vblanks - vblanks) / seconds : 0;
                if (!refresh_reported && ++windows >= 3)
                {
                    log_line("PyroWave VideoOut actual %.3f Hz; selected %.3f Hz",
                             observed_refresh_hz, s.backend->refresh_hz());
                    if (s.fps > 100 && observed_refresh_hz < 100 && !ps5_vrr_output_active())
                        log_line("PyroWave 120 Hz verification pending: short live window "
                                 "%.3f Hz; "
                                 "continuing stream",
                                 observed_refresh_hz);
                    refresh_reported = true;
                }
                if (s.hdr && windows >= 3 && ps5_hdr_output_active() != 1)
                {
                    record_error("HDR10 output unavailable: enable HDR in PS5 settings and use an "
                                 "HDR display");
                    log_line("PyroWave HDR10 stopped: physical VideoOut is not HDR");
                    if (error_callback)
                        error_callback(-1);
                    return nullptr;
                }
                if (s.shown.load() == shown && counters.failed && samples)
                {
                    record_error("VideoOut stopped presenting frames; reconnect or "
                                 "restart ProsperoLight");
                    if (error_callback)
                        error_callback(-1);
                    return nullptr;
                }
                log_line("PyroWave live: in=%.2f decoded=%.2f shown=%.2f vblank=%.2f "
                         "Mbps=%.2f GPU "
                         "decode=%.3f render=%.3f ms queue=%zu/%zu stale=%llu partial=%llu "
                         "rejected=%llu lost_packets=%llu flip_errors=%llu",
                         (s.incoming.load() - incoming) / seconds,
                         (s.decoded.load() - decoded) / seconds, (s.shown.load() - shown) / seconds,
                         observed_refresh_hz, (s.bytes.load() - bytes) * 8.0 / elapsed,
                         samples ? decode_ms / samples : 0, samples ? render_ms / samples : 0,
                         depth, high, (unsigned long long)s.stale.load(),
                         (unsigned long long)s.partial.load(),
                         (unsigned long long)s.rejected.load(), (unsigned long long)s.losses.load(),
                         (unsigned long long)counters.failed);
                native_agc_metrics_t hud{};
                hud.video_codec = 2;
                // Software VideoOut counters report Hz; the shared HUD expects Hz x100.
                hud.output_refresh_x100 = counters.vblank_available
                                              ? uint32_t(observed_refresh_hz * 100.0 + 0.5)
                                              : selected_refresh_x100;
                hud.incoming_fps_x100 = uint32_t((s.incoming.load() - incoming) * 100 / seconds);
                hud.decoded_fps_x100 = uint32_t((s.decoded.load() - decoded) * 100 / seconds);
                // Unique frames, unlike the separate shown/scanout repeat counter.
                hud.rendering_fps_x100 =
                    uint32_t((unique_presented - prior_unique_presented) * 100 / seconds);
                hud.stream_width = s.width;
                hud.stream_height = s.height;
                hud.gpu_render_average_us = uint64_t(samples ? render_ms * 1000 / samples : 0);
                hud.decode_average_us = uint64_t(samples ? decode_ms * 1000 / samples : 0);
                hud.decoder_load_permille = unsigned(hud.decode_average_us * s.fps / 1000);
                hud.pending_frames = unsigned(depth);
                hud.not_displayed = unsigned(s.stale.load());
                hud.lost_packets = s.losses.load();
                hud.partial_frames = unsigned(s.partial.load());
                hud.rejected_frames = unsigned(s.rejected.load());
                hud.network_drop_percent_x100 =
                    unsigned(s.rejected.load() * 10000 / std::max<uint64_t>(1, s.incoming.load()));
                hud.rtt_valid = LiGetEstimatedRttInfo(&hud.rtt_ms, &hud.rtt_variance_ms);
                hud.vsync_enabled = selected_vsync || mode == 2;
                hud.pacing_mode = mode;
                hud.vrr_api_active = ps5_vrr_output_active();
                hud.pacing_reserve_us =
                    hud.vrr_api_active ? vrr_repeats.playout_reserve_us() : pacer.stats.reserve_us;
                s.backend->update_hud(&hud, native_agc_hud_enabled() != 0);
                prior_unique_presented = unique_presented;
                last = now;
                incoming = s.incoming;
                decoded = s.decoded;
                shown = s.shown;
                bytes = s.bytes;
                vblanks = counters.vblanks;
                decode_ms = render_ms = 0;
                samples = 0;
            }
        }
    }
    catch (const std::exception &e)
    {
        record_error(e.what());
        log_line("PyroWave worker failed: %s", e.what());
        if (error_callback)
            error_callback(-1);
    }
    log_line("PyroWave scanout repeats total=%llu", (unsigned long long)repeated);
    return nullptr;
}
int setup(int format, int width, int height, int fps, void *, int flags)
{
    if (!(format & VIDEO_FORMAT_MASK_PYROWAVE) || (format & (format - 1)) || flags != 0)
        return -1;
    cleanup();
    session = std::make_unique<Session>();
    auto &s = *session;
    s.width = width;
    s.height = height;
    s.fps = fps;
    s.chroma444 = (format & VIDEO_FORMAT_MASK_YUV444) != 0;
    s.hdr = (format & VIDEO_FORMAT_MASK_10BIT) != 0;
    requested_hdr = s.hdr;
    try
    {
        // The caller stopped the AGC animation; no presentation owner remains.
        if (native_agc_present_shutdown() != 0)
            fail("AGC release before RADV failed");
        s.context = std::make_unique<VulkanContext>();
        s.context->init(true);
        s.backend = std::make_unique<PyroWaveVideoBackend>(*s.context);
        s.backend->initialize(width, height, fps, s.chroma444, s.hdr, selected_vsync,
                              selected_tv_safe, selected_pacing);
#if PROSPEROLIGHT_TEST_LAB
        if (prosperolight::test_lab::active())
            prosperolight::test_lab::context().negotiated(format, width, height, fps);
#endif
        log_line("PyroWave negotiated: %dx%d @ %d FPS, %s %s %u-bit limited range, "
                 "compression=0",
                 width, height, fps, s.hdr ? "HDR10" : "SDR", s.chroma444 ? "4:4:4" : "4:2:0",
                 s.hdr ? 10u : 8u);
        return 0;
    }
    catch (const std::exception &e)
    {
        record_error(e.what());
        log_line("PyroWave setup failed: %s", e.what());
        cleanup();
        return -1;
    }
}
void start()
{
    if (!session)
        return;
    receive_deadline.clear();
    session->running = true;
    int result = pthread_create(&session->worker, nullptr, worker, nullptr);
    session->started = result == 0;
    if (result && error_callback)
        error_callback(result);
}
void stop()
{
    if (!session)
        return;
    {
        std::lock_guard<std::mutex> lock(session->mutex);
        session->running = false;
        session->queue.clear();
    }
    receive_deadline.clear();
    session->wake.notify_all();
    if (session->started)
    {
        pthread_join(session->worker, nullptr);
        session->started = false;
    }
}
int submit(PDECODE_UNIT unit)
{
    if (!session || unit->fullLength <= 0 || unit->fullLength > 16 * 1024 * 1024)
        return DR_OK;
    if (session->hdr &&
        ((hdr_known.load() && !hdr_active.load()) || unit->colorspace != COLORSPACE_REC_2020))
    {
        record_error("HDR10 unavailable: enable HDR on the host capture display");
        log_line("PyroWave HDR10 rejected: enable HDR on the host capture display; "
                 "colorspace=%u",
                 unit->colorspace);
        if (error_callback)
            error_callback(-1);
        return DR_OK;
    }
    Frame frame;
    frame.bytes.resize(unit->fullLength);
    frame.presentation_us = unit->presentationTimeUs;
    frame.rtp_timestamp = unit->rtpTimestamp;
    frame.queued_us = now_us();
    frame.receive_us = unit->receiveTimeUs;
    frame.enqueue_us = unit->enqueueTimeUs;
    frame.critical = unit->pyrowaveCriticalPackets;
    frame.number = unit->frameNumber;
    size_t offset = 0;
    for (auto *buffer = unit->bufferList; buffer; buffer = buffer->next)
    {
        if (buffer->length <= 0 || size_t(buffer->length) > frame.bytes.size() - offset)
            return DR_OK;
        bool lost = buffer->bufferType == BUFFER_TYPE_LOST;
        frame.segments.push_back(
            {offset, size_t(buffer->length), lost, buffer->bufferType == BUFFER_TYPE_RECORD_START});
        if (lost)
        {
            memset(frame.bytes.data() + offset, 0, buffer->length);
            ++session->losses;
        }
        else
            memcpy(frame.bytes.data() + offset, buffer->data, buffer->length);
        offset += buffer->length;
    }
    if (offset != frame.bytes.size())
        return DR_OK;
    {
        std::lock_guard<std::mutex> lock(session->mutex);
        if (!session->running)
            return DR_OK;
        if (session->queue.size() == 2)
        {
            session->decisions.record(session->queue.front().number,
                                      session->queue.front().rtp_timestamp, now_us(), 0,
                                      "drop-capacity");
            session->queue.pop_front();
            ++session->stale;
        }
        session->decisions.record(frame.number, frame.rtp_timestamp, frame.queued_us, 0, "arrival");
        ++session->incoming;
        session->bytes += offset;
        session->queue.push_back(std::move(frame));
        session->high_water = std::max(session->high_water, session->queue.size());
    }
    session->wake.notify_one();
    return DR_OK;
}
DECODER_RENDERER_CALLBACKS video_callbacks{};
} // namespace
void cleanup()
{
    stop();
    if (session)
    {
        log_line(
            "PyroWave session summary: incoming=%llu decoded=%llu shown=%llu "
            "stale=%llu "
            "partial=%llu rejected=%llu lost_packets=%llu high_water=%zu",
            (unsigned long long)session->incoming.load(),
            (unsigned long long)session->decoded.load(), (unsigned long long)session->shown.load(),
            (unsigned long long)session->stale.load(), (unsigned long long)session->partial.load(),
            (unsigned long long)session->rejected.load(),
            (unsigned long long)session->losses.load(), session->high_water);
        requested_hdr = false;
        session->backend.reset();
        session->context.reset();
        session.reset();
        const int release_result = wsi_ps5_release_videoout();
        if (release_result)
            record_error("VideoOut release failed; restart ProsperoLight before "
                         "streaming again");
        log_line("PyroWave VideoOut handoff complete: rc=%08x", unsigned(release_result));
    }
}
void prepare_callbacks(void (*on_error)(int), bool vsync, bool tv_safe, unsigned pacing,
                       unsigned vrr_profile)
{
    record_error("");
    selected_pacing = pacing;
    selected_vrr_profile = vrr_profile;
    selected_vsync = vsync;
    selected_tv_safe = tv_safe;
    hdr_known = false;
    hdr_active = false;
    error_callback = on_error;
    presented_count = 0;
    confirmed_unique_count = 0;
    receive_deadline.clear();
    // The patched moonlight-common-c callback is registered before
    // LiStartConnection(). Unpaced explicitly disables deadline pressure.
    LiSetVideoReassemblyDeadlineCallback(selected_pacing == 0 ? nullptr
                                                              : pyrowave_receive_deadline);
    video_callbacks = {};
    video_callbacks.setup = setup;
    video_callbacks.start = start;
    video_callbacks.stop = stop;
    video_callbacks.cleanup = cleanup;
    video_callbacks.submitDecodeUnit = submit;
    video_callbacks.capabilities = CAPABILITY_DIRECT_SUBMIT;
}
void clear_error()
{
    record_error("");
}
void copy_error(char *destination, size_t capacity)
{
    std::lock_guard<std::mutex> lock(error_mutex);
    if (destination && capacity)
        snprintf(destination, capacity, "%s", last_error);
}
void set_hdr_mode(bool enabled, const SS_HDR_METADATA *metadata)
{
    hdr_active = enabled;
    hdr_known = true;
    if (metadata && requested_hdr.load())
        log_line("PyroWave HDR metadata: max_nits=%u min_1e4_nits=%u CLL=%u FALL=%u",
                 metadata->maxDisplayLuminance, metadata->minDisplayLuminance,
                 metadata->maxContentLightLevel, metadata->maxFrameAverageLightLevel);
}
DECODER_RENDERER_CALLBACKS *callbacks()
{
    return &video_callbacks;
}
uint64_t presented()
{
    return presented_count.load();
}
uint64_t completed_unique()
{
    return confirmed_unique_count.load(std::memory_order_relaxed);
}
} // namespace prosperolight::pyrowave
