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
#include <vector>

static std::string temporary_report, saved_report;
static bool write_failure, close_failure, log_failure, open_failure;
static unsigned write_calls;
static std::vector<std::string> kernel_records;
extern "C"
{
    int sceKernelDebugOutText(int channel, const char *text)
    {
        assert(channel == 0 && strlen(text) < 512);
        kernel_records.emplace_back(text);
        return log_failure ? -1 : 0;
    }
    ps5_network_metrics_t ps5_network_metrics_read(void)
    {
        return {};
    }
    ps5_thread_placement_stats_t ps5_thread_placement_stats(void)
    {
        ps5_thread_placement_stats_t stats{};
        stats.applied = 11;
        stats.failed = 1;
        stats.receive_verified = 0x400;
        return stats;
    }
    int sceKernelOpen(const char *, int flags, uint16_t mode)
    {
        assert(flags == 0x601 && mode == 0600);
        temporary_report.clear();
        return open_failure ? -1 : 1;
    }
    int64_t sceKernelWrite(int, const void *data, size_t size)
    {
        ++write_calls;
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
int native_agc_vsync_active(void)
{
    return 1;
}
int native_agc_flip_events_active(void)
{
    return 1;
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
    timing.flip_event_wakeups = 2;
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
    state.decoded = 99;
    state.presented = 95;
    state.not_displayed = 4;
    state.drops.network = 2;
    state.drops.decoder = 30;
    state.stream_bytes = 123456;
    state.flush_calls = 1;
    state.pipeline_mode = MOONLIGHT_DECODER_PIPELINE_ADAPTIVE;
    state.pipeline_depth = state.requested_depth = 3;
    state.drain_enabled = true;
    state.drain_calls = 40;
    state.decoder_cores = 5;
    state.decoder_cpu_mask = state.requested_cpu_mask = 0x3ff;
    state.create_attempts = 1;
    state.process_cpu_mask = moonlight::kTitleCpuMask;
    state.layout = moonlight::plan_thread_layout(state.process_cpu_mask, state.decoder_cpu_mask);
    state.slices_requested = 8;
    state.vsync_requested = 1;
    state.decoder_config.max_dpb_frames = 4;
    state.flush_timing.add(1250);
    state.present_wait_timing.add(100);
    state.completion_wait_timing.add(500);
    state.present_call_timing.add(450);
    state.host_timing.add(1200);
    for (unsigned i = 0; i < 100; ++i)
        state.decode_timing.add(3000);
    moonlight_stream_options_t options{};
    options.bitrate_kbps = 80000;
    moonlight::TimingHistogram input;
    input.add(2000);
    save_performance_summary(state, input, &options, 0);
    assert(saved_report.find("\"requested_slices_per_frame\":8,") != std::string::npos);
    const std::string original = saved_report;
    // The same summary reaches klog in bounded parts that concatenate back.
    std::string reconstructed;
    for (size_t i = 0; i < kernel_records.size(); ++i)
    {
        const auto &line = kernel_records[i];
        const std::string prefix = "[ProsperoLight perf] session=1 part=" + std::to_string(i + 1) +
                                   "/" + std::to_string(kernel_records.size()) + " json=";
        assert(line.find(prefix) == 0 && line.back() == '\n');
        reconstructed += line.substr(prefix.size(), line.size() - prefix.size() - 1);
    }
    std::string flattened = original;
    for (auto &c : flattened)
        if (c == '\n')
            c = ' ';
    assert(reconstructed == flattened);
    kernel_records.clear();
    open_failure = true;
    save_performance_summary(state, input, &options, 0);
    assert(!kernel_records.empty() && saved_report == original);
    open_failure = false;
    kernel_records.clear();
    log_failure = true;
    save_performance_summary(state, input, &options, 0);
    assert(kernel_records.size() == 1 && saved_report == original);
    log_failure = false;
    assert(original.find("\"decoder_mode\":\"adaptive\",\"decoder_pipeline_depth\":3,") !=
           std::string::npos);
    assert(original.find("\"decoder_cpu_affinity\":1023,") != std::string::npos);
    assert(original.find("\"receive_cpu_mask\":1024,\"decode_cpu_mask\":4096,"
                         "\"present_cpu_mask\":2048,\"other_cpu_mask\":6144,") !=
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
    sample->host_us = 700;
    save_frame_trace();
    assert(saved_report.find("# schema=2,count=1,omitted=0\n") == 0);
    assert(saved_report.find("\n42,1234,0,2,") != std::string::npos);
    assert(saved_report.rfind(",700\n") == saved_report.size() - 5);
    const std::string trace_report = saved_report;
    write_failure = true;
    save_frame_trace();
    assert(saved_report == trace_report);
    write_failure = false;
    close_failure = true;
    save_frame_trace();
    assert(saved_report == trace_report);
    close_failure = false;
    // Rows are batched: a full trace is written in a few large chunks, with
    // every row intact across the chunk boundaries.
    frame_trace.count = moonlight::FrameTrace::capacity;
    for (size_t i = 0; i < frame_trace.count; ++i)
    {
        frame_trace.samples[i] = {};
        frame_trace.samples[i].frame = static_cast<uint32_t>(i);
        frame_trace.samples[i].receive_us = UINT64_C(1000000000000) + i;
    }
    write_calls = 0;
    save_frame_trace();
    size_t rows = 0;
    for (char c : saved_report)
        rows += c == '\n';
    assert(rows == moonlight::FrameTrace::capacity + 2);
    assert(saved_report.find("\n32767,0,0,0,1000000032767,") != std::string::npos);
    assert(write_calls < saved_report.size() / 7 + 64); // Short writes, but only a few batches.
    assert(frame_trace.append() == nullptr && frame_trace.omitted == 1);
    frame_trace.count = 0;
    sample = frame_trace.append();
    assert(sample && sample->frame == 0 && sample->outcome == 0);
    sample->callback_us = 1000000;
    sample->frame = 1;
    sample->decode_us = 9000;
    sample->host_us = 7000;
    sample->receive_us = 100;
    sample->enqueue_us = 200;
    sample->callback_network_us = 300;
    sample->completion_us = 1010000;
    sample = frame_trace.append();
    sample->callback_us = 6000000; // Exact next five-second window.
    sample->frame = 3;
    sample->decode_us = 4000;
    sample->receive_us = 20100;
    sample->enqueue_us = 20200;
    sample->callback_network_us = 20500;
    sample->outcome = 2;
    kernel_records.clear();
    log_performance_windows(120);
    std::string windows;
    for (const auto &line : kernel_records)
    {
        const auto offset = line.find(" json=") + 6;
        windows += line.substr(offset, line.size() - offset - 1);
    }
    assert(windows.find("\"start_s\":0,\"duration_s\":5,\"frames\":1,\"presented\":1,"
                        "\"not_displayed\":0") != std::string::npos);
    assert(windows.find("\"start_s\":5,\"duration_s\":5,\"frames\":1,\"presented\":0,"
                        "\"not_displayed\":1") != std::string::npos);
    assert(windows.find("\"decode_over_budget\":1,\"budget_us\":8334") != std::string::npos);
    assert(windows.find("\"host_mean_us\":7000") != std::string::npos);
    assert(windows.find("\"receive_gap_max_us\":20000") != std::string::npos);
    assert(windows.find("\"gaps\":1") != std::string::npos);
    assert(windows.find("\"recorded\":2,\"reported\":2,\"omitted\":1") != std::string::npos);
    kernel_records.clear();
    log_performance_windows(0);
    assert(kernel_records.empty());
    puts(original.c_str()); // The runner parses and validates the actual JSON.
}
