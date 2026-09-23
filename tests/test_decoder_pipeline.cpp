/*
 * ps5-native-app-boilerplate / ProsperoLight - Delayed native output checks.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#define PROSPEROLIGHT_LAN_TELEMETRY 0
#define PROSPEROLIGHT_PERFORMANCE_DETAIL 1
#ifndef DECODER_PIPELINE_DEPTH
#define DECODER_PIPELINE_DEPTH 2
#endif
#include "../src/moonlight_stream.cpp"
#include <cassert>

static void *queued;
static unsigned resets, flushes, submits;
static bool fail_decode, fail_reset, never_ready;
extern "C"
{
    int lan_http_report_text(const char *)
    {
        return 0;
    }
    uint64_t PltGetMicroseconds(void)
    {
        return monotonic_us();
    }
    int LiGetPendingVideoFrames(void)
    {
        return 0;
    }
    bool LiGetEstimatedRttInfo(uint32_t *, uint32_t *)
    {
        return false;
    }
    int32_t sceKernelSendNotificationRequest(uint32_t, void *, size_t, int32_t)
    {
        return 0;
    }
    int sceKernelUsleep(uint32_t)
    {
        return 0;
    }
    int32_t sceVideodec2Reset(void *)
    {
        ++resets;
        if (fail_reset)
            return -99;
        queued = nullptr;
        return 0;
    }
    int32_t sceVideodec2Decode(void *, videodec2_input_t *, videodec2_frame_t *frame,
                               videodec2_output_t *output)
    {
        if (fail_decode)
            return -98;
        if (queued && !never_ready)
        {
            output->buffer = queued;
            output->buffer_size = active_renderer->frame_size;
            output->codec = active_renderer->mode->codec_type;
            output->width = active_renderer->mode->output_width;
            output->height = active_renderer->mode->output_height;
            output->pitch = active_renderer->mode->output_pitch;
            output->picture_count = output->valid = frame->accepted = 1;
        }
        queued = frame->buffer;
        return 0;
    }
    int32_t sceVideodec2Flush(void *, videodec2_frame_t *, videodec2_output_t *)
    {
        ++flushes;
        return -97;
    }
}
int native_agc_wait_source_idle(const void *)
{
    return 0;
}
int native_agc_finish_frame(void)
{
    return 0;
}
int native_agc_hud_enabled(void)
{
    return 0;
}
int native_agc_present_nv12(const void *, size_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                            const native_agc_metrics_t *)
{
    ++submits;
    return 0;
}
int native_agc_present_main10(const void *, size_t, uint32_t, uint32_t, uint32_t, uint32_t,
                              uint32_t, const native_agc_metrics_t *)
{
    return -96;
}

int main()
{
    static_assert(DECODER_PIPELINE_DEPTH == 2);
    native_renderer_state_t state{};
    uint8_t inputs[PIPELINE_BUFFER_COUNT * 64]{};
    uint8_t frames[PIPELINE_BUFFER_COUNT * 64]{};
    uint8_t au[] = {0, 0, 1, 0x65, 0x80};
    LENTRY fragment{};
    fragment.data = reinterpret_cast<char *>(au);
    fragment.length = sizeof(au);
    DECODE_UNIT unit{};
    unit.fullLength = sizeof(au);
    unit.bufferList = &fragment;
    state.mode = &video_modes[0];
    state.running = 1;
    state.stream_fps = 120;
    state.input_memory = inputs;
    state.frame_memory = frames;
    state.input_size = state.frame_size = 64;
    active_renderer = &state;
    for (unsigned i = 0; i < 20; ++i)
    {
        unit.frameNumber = i + 1;
        assert(moonlight_renderer_submit(&unit) == DR_OK);
        assert(state.submission_count == 1);
        assert(!state.decoder_needs_reset);
    }
    const unsigned expected_submits = PRESENT_EVERY_N == 1 ? 19u : 1u + 19u / PRESENT_EVERY_N;
    assert(flushes == 0 && submits == expected_submits && state.ready_calls == 19);
    assert(state.presentation_decimated == 19u - expected_submits);
    assert(frame_trace.samples[0].outcome == 1);
    assert(frame_trace.samples[19].outcome == 0); // Tail still belongs to decoder.
    fail_decode = true;
    assert(moonlight_renderer_submit(&unit) == DR_NEED_IDR);
    assert(state.decoder_needs_reset);
    fail_reset = true;
    assert(moonlight_renderer_submit(&unit) == DR_NEED_IDR);
    fail_decode = fail_reset = false;
    assert(moonlight_renderer_submit(&unit) == DR_OK);
    assert(resets == 2 && state.submission_count == 1);
    never_ready = true;
    assert(moonlight_renderer_submit(&unit) == DR_NEED_IDR); // No unbounded backlog.
    assert(state.decoder_needs_reset && state.submission_count == 2);
    puts("Delayed decoder output / pool wrap / reset / bounded backlog PASS");
}
