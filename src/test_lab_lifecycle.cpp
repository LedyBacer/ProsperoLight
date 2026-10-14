/* ps5-native-app-boilerplate - bounded control worker and final receipts.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "test_lab_lifecycle.hpp"
#include "app_storage.hpp"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
namespace prosperolight::test_lab
{
namespace
{
uint64_t now_us()
{
    timespec t{};
    clock_gettime(CLOCK_MONOTONIC, &t);
    return uint64_t(t.tv_sec) * 1000000 + t.tv_nsec / 1000;
}
} // namespace
Context &context()
{
    static Context value;
    return value;
}
bool Context::begin(const TestProfile &profile, std::string *error)
{
    if (error)
        error->clear();
    join();
    timeline_ = Timeline(profile, now_us());
    sequence_ = 2;
    commands_.clear();
    last_command_.clear();
    last_action_.clear();
    last_result_.clear();
    sample_ = nullptr;
    stop_ = nullptr;
    data_ = nullptr;
    options_ = {};
    format_ = width_ = height_ = fps_ = 0;
    output_width_ = output_height_ = output_refresh_ = output_pacing_ = 0;
    output_vsync_ = output_hdr_ = false;
    unique_frames_ = 0;
    wall_started_ = uint64_t(time(nullptr));
    const char *baseline_names[] = {"performance-last.json",        "debug-trace.txt",
                                    "performance-frames-mode0.csv", "performance-frames-mode1.csv",
                                    "performance-frames-mode2.csv", "pyrowave-output-mode0.csv",
                                    "pyrowave-output-mode1.csv",    "pyrowave-output-mode2.csv"};
    for (unsigned i = 0; i < 8; ++i)
    {
        struct stat before
        {
        };
        const std::string path = std::string(storage::paths().logs) + "/" + baseline_names[i];
        baseline_valid_[i] = stat(path.c_str(), &before) == 0;
        baseline_sizes_[i] = uint64_t(before.st_size);
        baseline_times_[i] = uint64_t(before.st_mtime);
    }
    stopping_.store(false);
    host_cleanup_confirmed_.store(false);
    host_exit_cleanup_.store(0);
    running_.store(true);
    struct stat receipt
    {
    };
    const std::string receipt_path =
        PROSPEROLIGHT_TEST_LAB_ROOT "/receipts/" + profile.run_id + ".json";
    if (!safe_run_id(profile.run_id) || stat(receipt_path.c_str(), &receipt) == 0)
    {
        running_.store(false);
        timeline_.stop("RUN_ID_REPLAY");
        if (error)
            *error = "RUN_ID_REPLAY";
        stopping_.store(true);
        return false;
    }
    started_ = pthread_create(&thread_, nullptr, worker, this) == 0;
    if (!started_)
    {
        running_.store(false);
        timeline_.stop("CONTROL_THREAD_FAILED");
        if (error)
            *error = "CONTROL_THREAD_FAILED";
        stopping_.store(true);
    }
    return started_;
}
void Context::attach(Observation (*sample)(void *), void (*stop)(void *), void *data,
                     const moonlight_stream_options_t &options)
{
    pthread_mutex_lock(&mutex_);
    sample_ = sample;
    stop_ = stop;
    data_ = data;
    options_ = options;
    pthread_mutex_unlock(&mutex_);
}
void Context::join()
{
    running_.store(false);
    if (started_)
    {
        pthread_join(thread_, nullptr);
        started_ = false;
    }
    sample_ = nullptr;
    stop_ = nullptr;
    data_ = nullptr;
}
void Context::negotiated(int format, int width, int height, int fps)
{
    pthread_mutex_lock(&mutex_);
    format_ = format;
    width_ = width;
    height_ = height;
    fps_ = fps;
    pthread_mutex_unlock(&mutex_);
}
void Context::backend_output(unsigned width, unsigned height, unsigned refresh_x100,
                             unsigned pacing, bool vsync, bool hdr)
{
    pthread_mutex_lock(&mutex_);
    output_width_ = width;
    output_height_ = height;
    output_refresh_ = refresh_x100;
    output_pacing_ = pacing;
    output_vsync_ = vsync;
    output_hdr_ = hdr;
    pthread_mutex_unlock(&mutex_);
}
void Context::fail(const char *reason)
{
    pthread_mutex_lock(&mutex_);
    timeline_.stop(reason);
    stopping_.store(true);
    pthread_mutex_unlock(&mutex_);
}
void *Context::worker(void *self)
{
    static_cast<Context *>(self)->run();
    return nullptr;
}
void Context::run()
{
    while (running_.load())
    {
        const auto now = now_us();
        pthread_mutex_lock(&mutex_);
        const auto prior = timeline_.state;
        Observation observation = sample_ ? sample_(data_) : Observation{};
        if (observation.unique_frames > unique_frames_)
            unique_frames_ = observation.unique_frames;
        timeline_.tick(now, observation);
        std::string ack, ack_reason;
        const std::string path =
            PROSPEROLIGHT_TEST_LAB_ROOT "/control/" + timeline_.profile.run_id + ".json";
        errno = 0;
        FILE *f = std::fopen(path.c_str(), "rb");
        if (f)
        {
            char bytes[2049];
            size_t n = std::fread(bytes, 1, sizeof(bytes), f);
            bool bad = std::ferror(f);
            std::fclose(f);
            ControlCommand command;
            if (bad || n > 2048 ||
                !parse_control(std::string_view(bytes, n), timeline_.profile.run_id, command))
                timeline_.stop("INVALID_CONTROL");
            else
            {
                bool seen = false;
                for (const auto &id : commands_)
                    if (id.command_id == command.command_id)
                    {
                        seen = true;
                        if (id.action != command.action)
                            timeline_.stop("CONTROL_ID_REUSE");
                    }
                if (!seen)
                {
                    if (commands_.size() >= 64)
                        timeline_.stop("CONTROL_LIMIT");
                    else
                    {
                        commands_.push_back(command);
                        ack = command.command_id;
                        last_command_ = ack;
                        last_action_ = command.action;
                        const bool accepted = timeline_.command(command.action, now);
                        last_result_ = accepted ? "accepted" : "rejected";
                        if (!accepted)
                            ack_reason = "COMMAND_NOT_READY";
                    }
                }
            }
        }
        else if (errno != ENOENT)
            timeline_.stop("CONTROL_IO_ERROR");
        if (prior != timeline_.state || !ack.empty())
        {
            // Filesystem work is on this worker, never decoder/presentation paths.
            if (!write_profile_status(timeline_.profile.run_id, timeline_.name(), ++sequence_,
                                      options_.session_overrides.enabled ? &options_ : nullptr,
                                      ack_reason.empty() ? timeline_.reason : ack_reason,
                                      last_command_, last_result_, last_action_,
                                      timeline_.ready_seen))
                timeline_.stop("STATUS_IO_ERROR");
        }
        if (timeline_.state == Timeline::stopping)
        {
            stopping_.store(true);
            if (stop_)
                stop_(data_);
        }
        pthread_mutex_unlock(&mutex_);
        timespec delay{0, 100000000};
        nanosleep(&delay, nullptr);
    }
}
bool Context::finish(const moonlight_stream_metrics_t *metrics, int result)
{
    join();
    bool complete = metrics && result == 0 && metrics->presented_frames && timeline_.ready_seen &&
                    timeline_.measured && timeline_.reason == "COMPLETE";
    const char *reason = timeline_.reason.c_str();
    if (host_exit_cleanup_.load() == 2 || (timeline_.measured && host_exit_cleanup_.load() != 1))
    {
        complete = false;
        reason = "HOST_EXIT_CLEANUP_UNCONFIRMED";
    }
    if (!*reason)
        reason = result ? "STREAM_FAILED" : "USER_STOP";
    const bool pyrowave = options_.video_codec == MOONLIGHT_VIDEO_CODEC_PYROWAVE;
    const std::string trace =
        std::string(pyrowave ? "pyrowave-output-mode" : "performance-frames-mode") +
        std::to_string(options_.session_overrides.pacing) + ".csv";
    const char *names[] = {"performance-last.json", "debug-trace.txt", trace.c_str()};
    struct stat info[3]{};
    bool fresh[3]{};
    bool artifacts = true;
    for (unsigned i = 0; i < 3; ++i)
    {
        const std::string path = std::string(storage::paths().logs) + "/" + names[i];
        fresh[i] = stat(path.c_str(), &info[i]) == 0 && S_ISREG(info[i].st_mode) &&
                   info[i].st_size > 0 && uint64_t(info[i].st_mtime) >= wall_started_;
        const unsigned baseline =
            i < 2 ? i : (pyrowave ? 5 : 2) + options_.session_overrides.pacing;
        if (baseline >= 8 ||
            (baseline_valid_[baseline] && baseline_sizes_[baseline] == uint64_t(info[i].st_size) &&
             baseline_times_[baseline] == uint64_t(info[i].st_mtime)))
            fresh[i] = false;
        // PyroWave exports its own output trace, not the native decoder summary.
        artifacts = artifacts && (pyrowave && i == 0 ? true : fresh[i]);
    }
    const char *verdict =
        complete ? (artifacts ? "PASS" : "INCONCLUSIVE")
                 : (!std::strcmp(reason, "CANCELLED") || !std::strcmp(reason, "STOP_REQUESTED") ||
                            !std::strcmp(reason, "USER_STOP")
                        ? "CANCELLED"
                        : "FAIL");
    const char *root = PROSPEROLIGHT_TEST_LAB_ROOT "/receipts";
    mkdir(root, 0755);
    chmod(root, 0755);
    const auto path = std::string(root) + "/" + timeline_.profile.run_id + ".json",
               tmp = path + ".tmp";
    // A terminal receipt is immutable; a repeated run ID cannot replace it.
    struct stat existing
    {
    };
    if (stat(path.c_str(), &existing) == 0)
        return false;
    FILE *f = std::fopen(tmp.c_str(), "wb");
    if (!f)
        return false;
    bool ok =
        std::fprintf(f,
                     "{\"schema\":\"prosperolight-test-receipt/"
                     "2\",\"run_id\":\"%s\",\"sequence\":%u,\"functional_verdict\":\"%s\","
                     "\"reason\":\"%s\",\"result\":%d,\"stream_ready\":%s,\"measurement_complete\":"
                     "%s,\"presented_frames\":%u,\"artifacts_complete\":%s,\"artifacts\":[",
                     timeline_.profile.run_id.c_str(), ++sequence_, verdict, reason, result,
                     timeline_.ready_seen ? "true" : "false", timeline_.measured ? "true" : "false",
                     metrics ? metrics->presented_frames : 0, artifacts ? "true" : "false") > 0;
    if (ok && !pyrowave)
        ok = std::fprintf(f, "{\"name\":\"%s\",\"bytes\":%llu,\"fresh\":%s},", names[0],
                          (unsigned long long)info[0].st_size, fresh[0] ? "true" : "false") > 0;
    if (ok)
        ok = std::fprintf(f, "{\"name\":\"%s\",\"bytes\":%llu,\"fresh\":%s}]", names[1],
                          (unsigned long long)info[1].st_size, fresh[1] ? "true" : "false") > 0;
    // Append identifiers without attributing flip observation to physical latency.
    if (ok)
    {
        ok = options_.session_overrides.enabled ? write_resolved_options(f, options_)
                                                : std::fputs(",\"resolved\":null", f) >= 0;
        if (format_ && width_ > 0 && height_ > 0 && fps_ > 0)
            ok = ok &&
                 std::fprintf(
                     f,
                     ",\"negotiated\":{\"source\":\"Limelight.renderer.setup.success\","
                     "\"verified\":true,\"video_format\":%d,\"width\":%d,\"height\":%d,\"fps\":%d}",
                     format_, width_, height_, fps_) > 0;
        else
            ok = ok && std::fputs(",\"negotiated\":null", f) >= 0;
        if (output_width_ && output_height_ && output_refresh_)
            ok =
                ok && std::fprintf(
                          f,
                          ",\"backend_output\":{\"source\":\"PyroWave.backend.initialize.success\","
                          "\"verified\":true,\"width\":%u,\"height\":%u,\"refresh_x100\":%u,"
                          "\"pacing\":%u,\"vsync\":%s,\"hdr\":%s}",
                          output_width_, output_height_, output_refresh_, output_pacing_,
                          output_vsync_ ? "true" : "false", output_hdr_ ? "true" : "false") > 0;
        ok = ok && std::fprintf(f, ",\"paired_host_cleanup_confirmed\":%s",
                                host_cleanup_confirmed_.load() ? "true" : "false") > 0;
        ok = ok && std::fprintf(f, ",\"paired_host_exit_cleanup_confirmed\":%s",
                                host_exit_cleanup_.load() == 1 ? "true" : "false") > 0;
        ok = ok && std::fprintf(
                       f,
                       ",\"origin_monotonic_us\":%llu,\"ready_monotonic_us\":%llu,\"measure_start_"
                       "monotonic_us\":%llu,\"measure_end_monotonic_us\":%llu",
                       (unsigned long long)timeline_.origin, (unsigned long long)timeline_.ready_us,
                       (unsigned long long)timeline_.measure_start_us,
                       (unsigned long long)timeline_.measure_end_us) > 0;
        ok = ok &&
             std::fprintf(f,
                          ",\"measure_start_phase_id\":\"%s:measure-start\",\"measure_end_phase_"
                          "id\":\"%s:measure-end\"",
                          timeline_.profile.run_id.c_str(), timeline_.profile.run_id.c_str()) > 0;
        ok = ok &&
             std::fprintf(
                 f,
                 ",\"continuous_progress\":{\"source\":\"confirmed unique completion counter "
                 "polled on control worker\","
                 "\"clock\":\"PS5 monotonic\",\"longest_no_progress_us\":%llu,\"stall_count\":%llu,"
                 "\"stall_threshold_us\":1000000,\"poll_max_us\":%llu,\"samples\":%llu,\"gap_log2_"
                 "bins\":[",
                 (unsigned long long)timeline_.progress_longest_us,
                 (unsigned long long)timeline_.progress_stalls,
                 (unsigned long long)timeline_.progress_poll_max_us,
                 (unsigned long long)timeline_.progress_samples) > 0;
        for (unsigned bin = 0; bin < 64; ++bin)
            ok = ok && std::fprintf(f, "%s%llu", bin ? "," : "",
                                    (unsigned long long)timeline_.progress_gap_bins[bin]) > 0;
        ok = ok && std::fprintf(f, "]}") > 0;
        ok = ok &&
             std::fprintf(
                 f,
                 ",\"measurement_validity\":\"%s\",\"performance_delta\":"
                 "\"NOT_COMPARABLE\",\"unique_completed_frames\":%llu,"
                 "\"measurement_unique_frames\":%llu,\"pid\":%d,"
                 "\"monotonic_us\":%llu,\"completion_clock\":\"ps5_monotonic_flip_"
                 "observation\",\"trace\":{\"name\":\"%s\",\"bytes\":%llu,\"fresh\":%s}}\n",
                 std::strstr(reason, "UNSUPPORTED") ? "UNSUPPORTED" : "INCONCLUSIVE",
                 (unsigned long long)unique_frames_,
                 (unsigned long long)(timeline_.measured && timeline_.measurement_end_frames >=
                                                                timeline_.measurement_start_frames
                                          ? timeline_.measurement_end_frames -
                                                timeline_.measurement_start_frames
                                          : 0),
                 int(getpid()), (unsigned long long)now_us(), names[2],
                 (unsigned long long)info[2].st_size, fresh[2] ? "true" : "false") > 0;
    }
    const int closed = std::fclose(f);
    if (!ok || closed || rename(tmp.c_str(), path.c_str()))
    {
        unlink(tmp.c_str());
        return false;
    }
    return true;
}
} // namespace prosperolight::test_lab
