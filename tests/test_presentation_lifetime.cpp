/*
 * ps5-native-app-boilerplate / ProsperoLight - Real presentation fence guard, mock VideoOut.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

// Include the implementation to exercise private ownership state without adding
// a production testing API. Unused hardware paths are discarded by the linker.
#define PROSPEROLIGHT_PERFORMANCE_DETAIL 1
#include "../src/native_agc_present.cpp"
#include <cassert>

static unsigned queries, sleeps, event_waits, complete_after;
static int event_result, event_count;
extern "C"
{
    int lan_http_report_text(const char *)
    {
        return 0;
    }
    int sceVideoOutGetFlipStatus(int32_t, void *status)
    {
        ++queries;
        static_cast<uint64_t *>(status)[3] = sleeps + event_waits >= complete_after ? 0x4444 : 0;
        return 0;
    }
    int sceKernelUsleep(uint32_t)
    {
        ++sleeps;
        return 0;
    }
    int sceKernelWaitEqueue(void *, void *, int capacity, int *count, unsigned int *timeout)
    {
        assert(capacity == 1 && timeout && *timeout == FLIP_EVENT_WAIT_US);
        ++event_waits;
        *count = event_count;
        return event_result;
    }
}

static void arm(const void *source)
{
    presenter.pending_source = source;
    presenter.pending_marker = 0x4444;
    queries = sleeps = event_waits = 0;
    native_agc_reset_performance();
}

int main()
{
    int source_a = 0, source_b = 0, queue = 0;
    native_agc_reset_performance();
    presenter.video = 1;
    presenter.requested_fps = 120;
    arm(&source_a);
    complete_after = 3;
    assert(native_agc_wait_source_idle(&source_b) == 0);
    assert(queries == 0); // The decoder may write the other slot immediately.
    assert(native_agc_wait_source_idle(&source_a) == 0);
    assert(sleeps == 3 && presenter.pending_source == nullptr && presenter.pending_marker == 0);
    assert(native_agc_performance().flip_queries == 4);
    assert(native_agc_performance().flip_sleeps == 3);
    assert(event_waits == 0 && !native_agc_flip_events_active());
    arm(&source_a);
    complete_after = 10000;
    assert(native_agc_finish_frame() == -5);
    assert(native_agc_performance().flip_timeouts == 1);
    assert(presenter.pending_source == &source_a && presenter.pending_marker == 0x4444);
    complete_after = 0;
    assert(native_agc_finish_frame() == 0);
    assert(presenter.pending_source == nullptr && presenter.pending_marker == 0);
    assert(native_agc_wait_source_idle(nullptr) == -1);

    // A flip event wakes the waiter; status still decides.
    presenter.flip_queue = &queue;
    assert(native_agc_flip_events_active());
    arm(&source_a);
    event_result = 0;
    event_count = 1;
    complete_after = 1;
    assert(native_agc_finish_frame() == 0);
    assert(event_waits == 1 && sleeps == 0 && queries == 2);
    assert(native_agc_performance().flip_event_wakeups == 1);
    // A stale event is followed by one sleep, then the queue again: never a spin.
    arm(&source_a);
    complete_after = 3;
    assert(native_agc_finish_frame() == 0);
    assert(event_waits == 2 && sleeps == 1);
    // A queue that only ever times out degrades to polling.
    arm(&source_a);
    event_result = (int)KERNEL_ERROR_ETIMEDOUT;
    event_count = 0;
    complete_after = 6;
    assert(native_agc_finish_frame() == 0);
    assert(event_waits == 3 && sleeps == 3);
    assert(native_agc_performance().flip_event_wakeups == 0);
    assert(native_agc_performance().flip_event_errors == 0);
    // A failing queue is counted and still bounded by the same budget.
    arm(&source_a);
    event_result = -1;
    complete_after = 100000;
    assert(native_agc_finish_frame() == -5);
    assert(native_agc_performance().flip_timeouts == 1);
    assert(native_agc_performance().flip_event_errors == event_waits && event_waits == sleeps);
    assert(presenter.pending_source == &source_a);
    complete_after = 0;
    assert(native_agc_finish_frame() == 0);
    presenter.flip_queue = nullptr;

    // V-Sync off is a request; a rejected immediate flip retires it for good.
    assert(native_agc_vsync_active());
    native_agc_set_vsync(0);
    assert(!native_agc_vsync_active() && effective_flip_mode() == VIDEO_OUT_FLIP_MODE_HSYNC);
    std::atomic_store(&hsync_rejected, 1);
    assert(native_agc_vsync_active() && effective_flip_mode() == VIDEO_OUT_FLIP_MODE_VSYNC);
    std::atomic_store(&hsync_rejected, 0);
    // An immediate flip that is never reported is given up once, not waited for
    // again, and ends immediate flips; a V-Sync flip keeps its timeout.
    arm(&source_a);
    presenter.pending_flip_mode = VIDEO_OUT_FLIP_MODE_HSYNC;
    complete_after = 100000;
    assert(native_agc_finish_frame() == 0);
    assert(presenter.pending_source == nullptr && presenter.pending_marker == 0);
    assert(native_agc_performance().vsync_fallbacks == 1 && native_agc_vsync_active());
    assert(native_agc_performance().flip_timeouts == 1);
    std::atomic_store(&hsync_rejected, 0);
    arm(&source_a);
    presenter.pending_flip_mode = VIDEO_OUT_FLIP_MODE_VSYNC;
    assert(native_agc_finish_frame() == -5 && presenter.pending_source == &source_a);
    complete_after = 0;
    assert(native_agc_finish_frame() == 0);
    native_agc_set_vsync(1);
    assert(native_agc_vsync_active());

    native_agc_reset_performance();
    assert(native_agc_performance().flip_queries == 0);
    assert(native_agc_performance().flip_sleeps == 0);
    assert(native_agc_performance().flip_timeouts == 0);
    puts("Presentation source ownership / timeout / flip events / V-Sync request PASS "
         "(mock VideoOut)");
}
