/*
 * ps5-native-app-boilerplate / ProsperoLight - Summary serialization and partial-write checks.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define PROSPEROLIGHT_LAN_TELEMETRY 0
#define PROSPEROLIGHT_PERFORMANCE_DETAIL 1
#include "../src/moonlight_stream.cpp"
#include <cassert>
#include <string>

static std::string temporary_report, saved_report;
static bool write_failure, close_failure;
extern "C"
{
    ps5_network_metrics_t ps5_network_metrics_read(void)
    {
        return {};
    }
    int sceKernelOpen(const char *, int flags, uint16_t mode)
    {
        assert(flags == 0x601 && mode == 0600);
        temporary_report.clear();
        return 1;
    }
    int64_t sceKernelWrite(int, const void *data, size_t size)
    {
        if (write_failure)
            return -1;
        const size_t count = size > 7 ? 7 : size; // Deliberately short writes.
        temporary_report.append(static_cast<const char *>(data), count);
        return static_cast<int64_t>(count);
    }
    int sceKernelClose(int)
    {
        return close_failure ? -1 : 0;
    }
    int sceKernelRename(const char *, const char *)
    {
        saved_report = temporary_report;
        return 0;
    }
}
void native_agc_output_status(uint32_t *width, uint32_t *height, uint32_t *refresh)
{
    *width = 3840;
    *height = 2160;
    *refresh = 11988;
}
const NativeAgcPerformance &native_agc_performance()
{
    static NativeAgcPerformance timing;
    timing = {};
    timing.prepare.add(150);
    timing.cache_flush.add(50);
    timing.submit.add(250);
    timing.flip_queries = 7;
    timing.flip_sleeps = 3;
    return timing;
}

int main()
{
    native_renderer_state_t state{};
    state.mode = &video_modes[0];
    state.stream_fps = 120;
    state.client_refresh_x100 = 11988;
    state.reassembly_invalid_samples = 1;
    assert(moonlight::record_reassembly(state.reassembly_timing, 1000, 4000, 9000));
    state.access_units = 100;
    state.presented = 95;
    state.stale_presentation_drops = 5;
    state.stream_bytes = 123456;
    state.flush_calls = 1;
    state.flush_timing.add(1250);
    state.source_wait_timing.add(100);
    state.completion_wait_timing.add(500);
    state.present_call_timing.add(450);
    state.host_timing.add(1200);
    for (unsigned i = 0; i < 100; ++i)
        state.decode_timing.add(3000);
    moonlight_stream_options_t options{};
    options.bitrate_kbps = 80000;
    moonlight::TimingHistogram input;
    input.add(4000);
    save_performance_summary(state, input, &options, 0);
    assert(saved_report.find("\"requested_slices_per_frame\":" +
                             std::to_string(VIDEO_SLICES_PER_FRAME) + ",") != std::string::npos);
    const std::string original = saved_report;
    assert(original.find("\"decoder_pipeline_depth\":" + std::to_string(DECODER_PIPELINE_DEPTH)) !=
           std::string::npos);
    assert(original.find("\"input_poll_us\":" + std::to_string(INPUT_POLL_US)) !=
           std::string::npos);
    assert(original.find("\"udp_packets\":0") != std::string::npos);
    assert(!original.empty());
    write_failure = true;
    save_performance_summary(state, input, &options, -1);
    assert(saved_report == original);
    write_failure = false;
    close_failure = true;
    save_performance_summary(state, input, &options, -2);
    assert(saved_report == original);
    close_failure = false;
    frame_trace.count = frame_trace.omitted = 0;
    auto *sample = frame_trace.append();
    assert(sample);
    sample->frame = 42;
    sample->bytes = 1234;
    sample->outcome = 2;
    save_frame_trace();
    assert(saved_report.find("# schema=1,count=1,omitted=0\n") == 0);
    assert(saved_report.find("\n42,1234,0,2,") != std::string::npos);
    const std::string trace_report = saved_report;
    write_failure = true;
    save_frame_trace();
    assert(saved_report == trace_report);
    write_failure = false;
    close_failure = true;
    save_frame_trace();
    assert(saved_report == trace_report);
    frame_trace.count = moonlight::FrameTrace::capacity;
    assert(frame_trace.append() == nullptr && frame_trace.omitted == 1);
    frame_trace.count = 0;
    sample = frame_trace.append();
    assert(sample && sample->frame == 0 && sample->outcome == 0);
    puts(original.c_str()); // The runner parses and validates the actual JSON.
}
