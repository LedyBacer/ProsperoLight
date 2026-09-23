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

static unsigned queries, sleeps, complete_after;
extern "C"
{
    int sceVideoOutGetFlipStatus(int32_t, void *status)
    {
        ++queries;
        static_cast<uint64_t *>(status)[3] = sleeps >= complete_after ? 0x4444 : 0;
        return 0;
    }
    int sceKernelUsleep(uint32_t)
    {
        ++sleeps;
        return 0;
    }
    int sceVideoOutWaitVblank(int32_t)
    {
        ++sleeps;
        return 0;
    }
}

int main()
{
    int source_a = 0, source_b = 0;
    native_agc_reset_performance();
    presenter.video = 1;
    presenter.requested_fps = 120;
    presenter.pending_source = &source_a;
    presenter.pending_marker = 0x4444;
    complete_after = 3;
    assert(native_agc_wait_source_idle(&source_b) == 0);
    assert(queries == 0); // The decoder may write the other slot immediately.
    assert(native_agc_wait_source_idle(&source_a) == 0);
    assert(sleeps == 3 && presenter.pending_source == nullptr && presenter.pending_marker == 0);
    assert(native_agc_performance().flip_queries == 4);
    assert(native_agc_performance().flip_sleeps == 3);
    presenter.pending_source = &source_a;
    presenter.pending_marker = 0x4444;
    complete_after = 10000;
    assert(native_agc_finish_frame() == -5);
    assert(native_agc_performance().flip_timeouts == 1);
    assert(presenter.pending_source == &source_a && presenter.pending_marker == 0x4444);
    complete_after = 0;
    assert(native_agc_finish_frame() == 0);
    assert(presenter.pending_source == nullptr && presenter.pending_marker == 0);
    assert(native_agc_wait_source_idle(nullptr) == -1);
    native_agc_reset_performance();
    assert(native_agc_performance().flip_queries == 0);
    assert(native_agc_performance().flip_sleeps == 0);
    assert(native_agc_performance().flip_timeouts == 0);
    puts("Presentation source ownership / timeout / retry PASS (mock VideoOut)");
}
