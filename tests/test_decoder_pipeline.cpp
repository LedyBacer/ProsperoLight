/*
 * ps5-native-app-boilerplate / ProsperoLight - Decode and presentation worker checks.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#define PROSPEROLIGHT_LAN_TELEMETRY 0
#define PROSPEROLIGHT_PERFORMANCE_DETAIL 1
#include "../src/moonlight_stream.cpp"
#include <atomic>
#include <cassert>
#include <chrono>
#include <deque>
#include <thread>

// Videodec2 stand-in. Two buffer models are plausible on hardware and both
// must work: the decoder keeps the frame buffer offered with each access unit
// and later returns it (Queued), or it holds pictures internally and writes
// each one into the buffer offered by the call that outputs it (SameCall).
enum class Model
{
    Queued,
    SameCall
};
static Model model;
static unsigned output_depth; // Decode outputs once this many are held; 0 = never.
static std::deque<void *> held;
static unsigned internal_pictures;
static unsigned resets, flushes, decodes, deletes, creates, submits;
static bool fail_decode, fail_reset, fail_flush, fail_create, orphan_flush;
// Seen on hardware: once a flush has returned a picture while others are still
// held, the decoder refuses to decode until the rest are flushed too.
static bool flushing;
static int pending_after_flush = -1; // A frame arriving while a flush blocks.
static std::atomic<int> pending_frames;
static int decoder_token, classic_token;

static native_renderer_state_t *mock_state;
static uint8_t inputs[INPUT_SLOT_COUNT * 64];
static uint8_t frames[FRAME_SLOT_COUNT * 64];

static void fill_output(videodec2_output_t *output, void *buffer)
{
    const native_video_mode_t *mode = mock_state->mode;
    output->buffer = buffer;
    output->buffer_size = mock_state->frame_size;
    output->codec = mode->codec_type;
    output->width = mode->output_width;
    output->height = mode->output_height;
    output->pitch = mode->output_pitch;
    output->picture_count = output->valid = 1;
}

// Worker-thread scenario: a stand-in for moonlight-common-c's frame queue.
static std::atomic<unsigned> feed_total, feed_next, completed_units, refused_units;
static std::atomic<bool> wake_requested;
static uint8_t access_unit[] = {0, 0, 1, 0x65, 0x80};
static LENTRY fragment;
static DECODE_UNIT queued_unit;

extern "C"
{
    void gs_logf(const char *, const char *, ...)
    {
    }
    int prosperolight_logs_enabled(void)
    {
        return 1;
    }
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
        return pending_frames;
    }
    bool LiGetEstimatedRttInfo(uint32_t *, uint32_t *)
    {
        return false;
    }
    bool LiWaitForNextVideoFrame(VIDEO_FRAME_HANDLE *handle, PDECODE_UNIT *unit)
    {
        for (;;)
        {
            if (wake_requested.exchange(false))
                return false;
            const unsigned next = feed_next.load();
            if (next < feed_total.load())
            {
                feed_next = next + 1;
                queued_unit.frameNumber = static_cast<int>(next + 1);
                queued_unit.frameType = next == 0 ? FRAME_TYPE_IDR : FRAME_TYPE_PFRAME;
                *handle = &queued_unit;
                *unit = &queued_unit;
                return true;
            }
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }
    void LiCompleteVideoFrame(VIDEO_FRAME_HANDLE, int status)
    {
        if (status == DR_NEED_IDR)
            ++refused_units;
        ++completed_units;
    }
    void LiWakeWaitForVideoFrame(void)
    {
        wake_requested = true;
    }
    int32_t sceKernelSendNotificationRequest(uint32_t, void *, size_t, int32_t)
    {
        return 0;
    }
    int sceKernelUsleep(uint32_t microseconds)
    {
        std::this_thread::sleep_for(
            std::chrono::microseconds(microseconds < 500u ? microseconds : 500u));
        return 0;
    }
    int ps5_thread_affinity_set(uint64_t)
    {
        return 0;
    }
    int32_t sceVideodec2Reset(void *)
    {
        ++resets;
        if (fail_reset)
            return -99;
        held.clear();
        internal_pictures = 0;
        flushing = false;
        return 0;
    }
    int32_t sceVideodec2Decode(void *, videodec2_input_t *, videodec2_frame_t *frame,
                               videodec2_output_t *output)
    {
        ++decodes;
        if (fail_decode)
            return -98;
        if (flushing)
            return -94;
        if (model == Model::Queued)
        {
            held.push_back(frame->buffer);
            frame->accepted = 1;
            if (output_depth && held.size() >= output_depth)
            {
                fill_output(output, held.front());
                held.pop_front();
            }
            return 0;
        }
        ++internal_pictures;
        if (output_depth && internal_pictures >= output_depth)
        {
            fill_output(output, frame->buffer);
            frame->accepted = 1;
            --internal_pictures;
        }
        return 0;
    }
    int32_t sceVideodec2Flush(void *, videodec2_frame_t *frame, videodec2_output_t *output)
    {
        ++flushes;
        if (fail_flush)
            return -97;
        if (orphan_flush)
        {
            frame->accepted = 1; // Kept, but belonging to no access unit.
            return 0;
        }
        if (pending_after_flush >= 0)
            pending_frames = pending_after_flush;
        if (model == Model::Queued)
        {
            if (!held.empty())
            {
                fill_output(output, held.front());
                held.pop_front();
            }
            flushing = !held.empty();
            return 0;
        }
        if (internal_pictures)
        {
            fill_output(output, frame->buffer);
            frame->accepted = 1;
            --internal_pictures;
        }
        flushing = internal_pictures != 0;
        return 0;
    }
    int32_t sceVideodec2DeleteDecoder(void *)
    {
        ++deletes;
        return 0;
    }
    int32_t sceVideodec2CreateDecoder(const videodec2_decoder_config_t *config,
                                      const videodec2_decoder_memory_t *, void **decoder)
    {
        ++creates;
        if (fail_create)
            return -95;
        assert(config->pipeline_depth == 1);
        held.clear();
        internal_pictures = 0;
        flushing = false;
        model = Model::SameCall;
        output_depth = 1;
        *decoder = &classic_token;
        return 0;
    }
}
void native_agc_reset_performance()
{
}
int native_agc_finish_frame(void)
{
    return 0;
}
void native_agc_output_status(uint32_t *width, uint32_t *height, uint32_t *refresh_x100)
{
    *width = 3840;
    *height = 2160;
    *refresh_x100 = 11988;
}
int native_agc_vrr_active(void)
{
    return 0;
}
int native_agc_vsync_active(void)
{
    return 1;
}
int native_agc_present_nv12(const void *, size_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                            const native_agc_metrics_t *metrics)
{
    assert(metrics);
    ++submits;
    return 0;
}
int native_agc_present_main10(const void *, size_t, uint32_t, uint32_t, uint32_t, uint32_t,
                              uint32_t, const native_agc_metrics_t *)
{
    return -96;
}

using Pool = moonlight::SlotPool<FRAME_SLOT_COUNT>;

static native_renderer_state_t *make_state(unsigned depth, Model decoder_model,
                                           unsigned decoder_output_depth)
{
    auto *state = new native_renderer_state_t{};
    state->mode = &video_modes[0];
    state->stream_fps = 120;
    state->input_memory = inputs;
    state->frame_memory = frames;
    state->input_size = state->frame_size = 64;
    state->pipeline_depth = state->requested_depth = depth;
    state->drain_enabled = depth > 1;
    state->decoder = &decoder_token;
    state->decoder_config.pipeline_depth = depth;
    state->running = 1;
    renderer_sync_init(state);
    mock_state = state;
    model = decoder_model;
    output_depth = decoder_output_depth;
    held.clear();
    internal_pictures = 0;
    resets = flushes = decodes = deletes = creates = submits = 0;
    fail_decode = fail_reset = fail_flush = fail_create = orphan_flush = flushing = false;
    pending_frames = 0;
    pending_after_flush = -1;
    std::atomic_store(&connection_terminated, 0);
    std::atomic_store(&connection_error, 0);
    return state;
}

static int decode(native_renderer_state_t *state, DECODE_UNIT *unit, int frame, int type)
{
    uint64_t busy = 0;
    unit->frameNumber = frame;
    unit->frameType = type;
    return decode_access_unit(state, unit, &busy);
}

static void pipelined_then_drained(Model decoder_model)
{
    DECODE_UNIT unit{};
    unit.fullLength = sizeof(access_unit);
    unit.bufferList = &fragment;
    auto *state = make_state(3, decoder_model, 3);
    frame_trace.count = frame_trace.omitted = 0;
    // Behind: frames are waiting, so pictures stay in the pipeline.
    pending_frames = 2;
    for (int i = 0; i < 20; ++i)
    {
        assert(decode(state, &unit, i + 1, i ? FRAME_TYPE_PFRAME : FRAME_TYPE_IDR) == DR_OK);
        assert(drain_decoder(state) == 0);
        assert(!state->decoder_needs_reset);
        assert(state->submission_count == (i < 2 ? static_cast<unsigned>(i + 1) : 2u));
    }
    assert(decodes == 20 && flushes == 0 && state->decoded == 18 && state->decoder_delayed == 2);
    // No presenter runs here: every newer picture replaces the waiting one.
    assert(state->not_displayed == 17 && state->mailbox.full && state->mailbox.item.frame == 18);
    assert(state->frames.count(Pool::Ready) == 1);
    assert(state->frames.count(Pool::Decoding) == (decoder_model == Model::Queued ? 2u : 0u));
    assert(frame_trace.samples[0].outcome == 2 && frame_trace.samples[17].outcome == 0);
    assert(frame_trace.samples[19].outcome == 0); // Still inside the decoder.
    // Caught up: take what the pipeline holds instead of waiting a frame time.
    pending_frames = 0;
    drain_decoder(state);
    assert(flushes == 2 && state->drain_calls == 2 && state->decoded == 20);
    assert(state->submission_count == 0 && state->frames.count(Pool::Decoding) == 0);
    assert(state->mailbox.item.frame == 20 && state->pipeline_depth == 3 && state->drain_enabled);
    assert(state->drain_faults == 0 && state->decoder_recreations == 0);
    // Keeping up from here on behaves like depth one: one flush per frame.
    for (int i = 20; i < 30; ++i)
    {
        assert(decode(state, &unit, i + 1, FRAME_TYPE_PFRAME) == DR_OK);
        drain_decoder(state);
        assert(state->submission_count == 0 && state->mailbox.item.frame == i + 1);
    }
    assert(state->decoded == 30 && flushes == 12);
    renderer_sync_destroy(state);
    delete state;
}

static void a_started_drain_runs_to_completion(Model decoder_model)
{
    DECODE_UNIT unit{};
    unit.fullLength = sizeof(access_unit);
    unit.bufferList = &fragment;
    auto *state = make_state(3, decoder_model, 3);
    // One frame was waiting, so the first picture stays in the pipeline.
    pending_frames = 1;
    assert(decode(state, &unit, 1, FRAME_TYPE_IDR) == DR_OK);
    assert(drain_decoder(state) == 0 && flushes == 0 && state->submission_count == 1);
    // Caught up after the second frame: the drain starts with two pictures held,
    // and the next frame arrives while the first flush is still blocking.
    pending_frames = 0;
    assert(decode(state, &unit, 2, FRAME_TYPE_PFRAME) == DR_OK);
    assert(state->submission_count == 2);
    pending_after_flush = 1;
    drain_decoder(state);
    pending_after_flush = -1;
    // Both pictures were taken: stopping after the first would have left the
    // decoder between two flushes, where it refuses the next access unit.
    assert(flushes == 2 && state->decoded == 2 && state->submission_count == 0 && !flushing);
    assert(decode(state, &unit, 3, FRAME_TYPE_PFRAME) == DR_OK);
    assert(resets == 0 && state->pipeline_faults == 0 && state->decode_errors == 0);
    assert(state->pipeline_depth == 3 && state->decoder_recreations == 0);
    // The model itself: a decode between two flushes is an error the client counts.
    pending_frames = 1;
    assert(decode(state, &unit, 4, FRAME_TYPE_PFRAME) == DR_OK); // 3 and 4 are now held.
    flushing = true;
    assert(decode(state, &unit, 5, FRAME_TYPE_PFRAME) == DR_NEED_IDR);
    assert(state->decode_errors == 1 && state->last_decode_error == -94);
    renderer_sync_destroy(state);
    delete state;
}

static void classic_depth_one()
{
    DECODE_UNIT unit{};
    unit.fullLength = sizeof(access_unit);
    unit.bufferList = &fragment;
    // H.264-like: the picture only appears after a flush into the same buffer.
    auto *state = make_state(1, Model::SameCall, 0);
    for (int i = 0; i < 10; ++i)
    {
        assert(decode(state, &unit, i + 1, i ? FRAME_TYPE_PFRAME : FRAME_TYPE_IDR) == DR_OK);
        assert(drain_decoder(state) == 0);
    }
    assert(flushes == 10 && state->flush_calls == 10 && state->drain_calls == 0);
    assert(state->decoded == 10 && state->decoder_delayed == 0 && state->submission_count == 0);
    renderer_sync_destroy(state);
    delete state;
    // Queued buffers at depth one behave the same.
    state = make_state(1, Model::Queued, 0);
    for (int i = 0; i < 10; ++i)
        assert(decode(state, &unit, i + 1, FRAME_TYPE_PFRAME) == DR_OK);
    assert(flushes == 10 && state->decoded == 10 && state->frames.count(Pool::Decoding) == 0);
    renderer_sync_destroy(state);
    delete state;
    // HEVC-like: the picture is returned by the decode call itself.
    state = make_state(1, Model::SameCall, 1);
    for (int i = 0; i < 10; ++i)
        assert(decode(state, &unit, i + 1, FRAME_TYPE_PFRAME) == DR_OK);
    assert(flushes == 0 && state->decoded == 10);
    // A decoder that never produces a picture is an error, not a silent stall.
    output_depth = 0;
    fail_flush = true;
    assert(decode(state, &unit, 11, FRAME_TYPE_PFRAME) == DR_NEED_IDR);
    assert(state->decoder_needs_reset && state->pipeline_faults == 0);
    renderer_sync_destroy(state);
    delete state;
}

static void errors_reset_and_bounded_backlog()
{
    DECODE_UNIT unit{};
    unit.fullLength = sizeof(access_unit);
    unit.bufferList = &fragment;
    auto *state = make_state(3, Model::Queued, 3);
    pending_frames = 1;
    for (int i = 0; i < 5; ++i)
        assert(decode(state, &unit, i + 1, FRAME_TYPE_PFRAME) == DR_OK);
    fail_decode = true;
    assert(decode(state, &unit, 6, FRAME_TYPE_PFRAME) == DR_NEED_IDR);
    assert(state->decoder_needs_reset && state->pipeline_faults == 1 && resets == 0);
    fail_reset = true;
    assert(decode(state, &unit, 7, FRAME_TYPE_IDR) == DR_NEED_IDR);
    assert(resets == 1 && state->pipeline_faults == 2);
    fail_decode = fail_reset = false;
    assert(decode(state, &unit, 8, FRAME_TYPE_IDR) == DR_OK);
    // The reset abandoned every picture the decoder held.
    assert(resets == 2 && state->submission_count == 1);
    assert(state->frames.count(Pool::Decoding) == 1);
    // No unbounded backlog: a pipeline never holds more than its depth.
    output_depth = 0;
    assert(decode(state, &unit, 9, FRAME_TYPE_PFRAME) == DR_OK);
    assert(decode(state, &unit, 10, FRAME_TYPE_PFRAME) == DR_OK);
    assert(decode(state, &unit, 11, FRAME_TYPE_PFRAME) == DR_NEED_IDR);
    assert(state->submission_count == 4 && state->pipeline_faults == 3);
    // Oversized or empty access units are refused before reaching the decoder.
    const unsigned before = decodes;
    unit.fullLength = 65;
    assert(decode(state, &unit, 12, FRAME_TYPE_IDR) == DR_NEED_IDR);
    unit.fullLength = sizeof(access_unit);
    unit.bufferList = nullptr;
    assert(decode(state, &unit, 13, FRAME_TYPE_IDR) == DR_NEED_IDR);
    assert(decodes == before);
    renderer_sync_destroy(state);
    delete state;
}

static void fallback_to_depth_one()
{
    DECODE_UNIT unit{};
    unit.fullLength = sizeof(access_unit);
    unit.bufferList = &fragment;
    // A flush that fails mid-stream: rebuild at depth one, resume on a keyframe.
    auto *state = make_state(3, Model::SameCall, 3);
    assert(decode(state, &unit, 1, FRAME_TYPE_IDR) == DR_OK);
    fail_flush = true;
    drain_decoder(state);
    fail_flush = false;
    assert(state->drain_faults == 1 && state->decoder_recreations == 1 && deletes == 1);
    assert(creates == 1 && state->pipeline_depth == 1 && !state->drain_enabled);
    assert(state->decoder == &classic_token && state->submission_count == 0);
    assert(state->await_keyframe && state->running);
    const unsigned before = decodes;
    assert(decode(state, &unit, 2, FRAME_TYPE_PFRAME) == DR_NEED_IDR);
    assert(decodes == before);
    assert(decode(state, &unit, 3, FRAME_TYPE_IDR) == DR_OK);
    assert(decode(state, &unit, 4, FRAME_TYPE_PFRAME) == DR_OK);
    assert(state->decoded == 2 && state->mailbox.item.frame == 4);
    renderer_sync_destroy(state);
    delete state;
    // A flush that takes a buffer without returning a picture is caught too.
    state = make_state(3, Model::Queued, 3);
    assert(decode(state, &unit, 1, FRAME_TYPE_IDR) == DR_OK);
    orphan_flush = true;
    drain_decoder(state);
    orphan_flush = false;
    assert(state->drain_faults == 1 && state->pipeline_depth == 1);
    assert(state->frames.count(Pool::Decoding) == 0);
    renderer_sync_destroy(state);
    delete state;
    // If the decoder cannot be rebuilt, the stream ends instead of stalling.
    state = make_state(3, Model::SameCall, 3);
    assert(decode(state, &unit, 1, FRAME_TYPE_IDR) == DR_OK);
    fail_flush = fail_create = true;
    drain_decoder(state);
    assert(!state->running && state->decoder == nullptr && connection_terminated);
    assert(connection_error == -95);
    renderer_sync_destroy(state);
    delete state;
}

static void presentation_hands_slots_back()
{
    DECODE_UNIT unit{};
    unit.fullLength = sizeof(access_unit);
    unit.bufferList = &fragment;
    auto *state = make_state(1, Model::SameCall, 1);
    frame_trace.count = frame_trace.omitted = 0;
    assert(decode(state, &unit, 1, FRAME_TYPE_IDR) == DR_OK);
    stream_ready_frame_t item{};
    assert(state->mailbox.take(&item) && item.frame == 1 && item.slot == 0);
    state->frames.state[static_cast<size_t>(item.slot)] = Pool::Presenting;
    // While frame 1 is on its way to the display its slot is never offered.
    assert(decode(state, &unit, 2, FRAME_TYPE_PFRAME) == DR_OK);
    assert(state->mailbox.item.slot != item.slot);
    assert(submit_presentation(state, item) == 0 && submits == 1);
    assert(complete_presentation(state, item) == 0 && state->presented == 1);
    assert(state->frames.state[static_cast<size_t>(item.slot)] == Pool::Free);
    assert(frame_trace.samples[0].outcome == 1 && frame_trace.samples[0].completion_us);
    assert(item.hud.pipeline_depth == 1 && item.hud.video_codec == state->mode->codec_preference);
    renderer_sync_destroy(state);
    delete state;
}

static void workers_run_and_join()
{
    auto *state = make_state(3, Model::Queued, 3);
    queued_unit = {};
    queued_unit.fullLength = sizeof(access_unit);
    queued_unit.bufferList = &fragment;
    feed_next = completed_units = refused_units = 0;
    wake_requested = false;
    feed_total = 500;
    active_renderer = state;
    moonlight_video_callbacks.start();
    assert(state->decode_started && state->present_started);
    for (unsigned waited = 0; completed_units.load() < 500 && waited < 100000; ++waited)
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    assert(completed_units.load() == 500 && refused_units.load() == 0);
    // Let the presenter retire what is left, then stop both workers.
    for (unsigned waited = 0; waited < 100000; ++waited)
    {
        pthread_mutex_lock(&state->lock);
        const bool idle = !state->mailbox.full && state->frames.count(Pool::Presenting) == 0;
        pthread_mutex_unlock(&state->lock);
        if (idle)
            break;
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    moonlight_video_callbacks.stop();
    moonlight_video_callbacks.cleanup();
    assert(!active_renderer && !state->decode_started && !state->present_started);
    assert(state->access_units == 500 && state->decoded == 500 && state->submission_count == 0);
    assert(state->presented >= 1 && state->presented + state->not_displayed == 500);
    assert(submits == state->presented && state->present_errors == 0);
    assert(state->frames.count(Pool::Free) == FRAME_SLOT_COUNT);
    assert(state->drain_faults == 0 && state->pipeline_depth == 3);
    renderer_sync_destroy(state);
    delete state;
}

// moonlight-common-c's own connection preamble: its pull-renderer check, then
// the placeholder fill, which writes into the caller's struct (linked from the
// real FakeCallbacks.c).
extern "C" void fixupMissingCallbacks(PDECODER_RENDERER_CALLBACKS *video,
                                      PAUDIO_RENDERER_CALLBACKS *audio,
                                      PCONNECTION_LISTENER_CALLBACKS *listener);

static bool library_accepts_video_callbacks()
{
    PDECODER_RENDERER_CALLBACKS video = &moonlight_video_callbacks;
    PAUDIO_RENDERER_CALLBACKS audio = nullptr;
    PCONNECTION_LISTENER_CALLBACKS listener = nullptr;

    if ((video->capabilities & CAPABILITY_PULL_RENDERER) && video->submitDecodeUnit)
        return false; // LiStartConnection() returns -1 here.
    fixupMissingCallbacks(&video, &audio, &listener);
    assert(video == &moonlight_video_callbacks);
    return true;
}

static void every_connection_of_a_process_is_accepted()
{
    for (unsigned connection = 0; connection < 3; ++connection)
    {
        const uint32_t slices = connection == 1 ? 4u : 8u;
        prepare_video_callbacks(slices, false);
        assert(moonlight_video_callbacks.capabilities ==
               (CAPABILITY_PULL_RENDERER | CAPABILITY_SLICES_PER_FRAME(slices)));
        assert(library_accepts_video_callbacks());
        // The library has now filled the empty submit slot in our struct: left
        // like this, the next connection would be refused.
        assert(moonlight_video_callbacks.submitDecodeUnit != nullptr);
        assert(!library_accepts_video_callbacks());
    }
    prepare_video_callbacks(8, true);
    assert(moonlight_video_callbacks.submitDecodeUnit == nullptr);
    assert(moonlight_video_callbacks.setup == moonlight_renderer_setup);
}

int main()
{
    fragment.data = reinterpret_cast<char *>(access_unit);
    fragment.length = sizeof(access_unit);
    pipelined_then_drained(Model::Queued);
    pipelined_then_drained(Model::SameCall);
    a_started_drain_runs_to_completion(Model::Queued);
    a_started_drain_runs_to_completion(Model::SameCall);
    classic_depth_one();
    errors_reset_and_bounded_backlog();
    fallback_to_depth_one();
    presentation_hands_slots_back();
    workers_run_and_join();
    every_connection_of_a_process_is_accepted();
    puts("Adaptive pipeline / drain / depth-one fallback / reset / slot ownership / workers / "
         "reconnection PASS");
}
