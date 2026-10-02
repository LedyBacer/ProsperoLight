// SPDX-License-Identifier: GPL-3.0-or-later
#include "stream_backend.hpp"
#include "video/pyrowave_video_backend.hpp"
#include "video/ps5_presentation_stats.hpp"
#include "native_agc_present.hpp"
#include <deque>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <pthread.h>
#include <ctime>
#include <exception>

extern "C" int wsi_ps5_release_videoout(void);
namespace prosperolight::pyrowave
{
namespace
{
struct Frame
{
    std::vector<uint8_t> bytes;
    std::vector<PyroWaveFraming::Segment> segments;
    uint64_t presentation_us{}, receive_us{}, enqueue_us{};
    uint16_t critical{};
    int number{};
};
struct Session
{
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<Frame> queue;
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
std::atomic<bool> hdr_active{false}, hdr_known{false}, requested_hdr{false};
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

void *worker(void *)
{
    auto &s = *session;
    uint64_t last = now_us(), incoming = 0, decoded = 0, shown = 0, bytes = 0;
    double decode_ms = 0, render_ms = 0;
    uint64_t samples = 0;
    auto counters = ps5_presentation_stats();
    uint64_t vblanks = counters.vblanks;
    bool refresh_reported = false;
    unsigned windows = 0;
    try
    {
        for (;;)
        {
            Frame frame;
            {
                std::unique_lock<std::mutex> lock(s.mutex);
                s.wake.wait(lock, [&] { return !s.running || !s.queue.empty(); });
                if (!s.running)
                    break;
                // All frames are independent: consume newest and retire older work.
                frame = std::move(s.queue.back());
                s.stale += s.queue.size() - 1;
                s.queue.clear();
            }
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
            auto timing = s.backend->present();
            decode_ms += timing.decode_ms;
            render_ms += timing.render_ms;
            ++samples;
            counters = ps5_presentation_stats();
            if (!counters.available)
            {
                record_error("VideoOut presentation counters unavailable");
                log_line("PyroWave worker failed: VideoOut presentation counters unavailable");
                if (error_callback)
                    error_callback(-1);
                return nullptr;
            }
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
                const double refresh =
                    counters.vblank_available ? (counters.vblanks - vblanks) / seconds : 0;
                if (!refresh_reported && ++windows >= 3)
                {
                    log_line("PyroWave VideoOut actual %.3f Hz; selected %.3f Hz", refresh,
                             s.backend->refresh_hz());
                    if (s.fps > 100 && refresh < 100)
                        log_line("PyroWave 120 Hz verification pending: short live window %.3f Hz; "
                                 "continuing stream",
                                 refresh);
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
                    record_error(
                        "VideoOut stopped presenting frames; reconnect or restart ProsperoLight");
                    if (error_callback)
                        error_callback(-1);
                    return nullptr;
                }
                log_line("PyroWave live: in=%.2f decoded=%.2f shown=%.2f vblank=%.2f Mbps=%.2f GPU "
                         "decode=%.3f render=%.3f ms queue=%zu/%zu stale=%llu partial=%llu "
                         "rejected=%llu lost_packets=%llu flip_errors=%llu",
                         (s.incoming.load() - incoming) / seconds,
                         (s.decoded.load() - decoded) / seconds, (s.shown.load() - shown) / seconds,
                         refresh, (s.bytes.load() - bytes) * 8.0 / elapsed,
                         samples ? decode_ms / samples : 0, samples ? render_ms / samples : 0,
                         depth, high, (unsigned long long)s.stale.load(),
                         (unsigned long long)s.partial.load(),
                         (unsigned long long)s.rejected.load(), (unsigned long long)s.losses.load(),
                         (unsigned long long)counters.failed);
                char hud[512];
                snprintf(hud, sizeof(hud),
                         "PyroWave %ux%u %u FPS / %s %s %u-bit\n"
                         "Incoming %.2f   Decoded %.2f   Presented %.2f FPS\n"
                         "VideoOut %.2f Hz   Data %.2f Mbps\n"
                         "GPU decode %.3f ms   render %.3f ms\n"
                         "Queue %zu/%zu   Stale %llu   Partial %llu\n"
                         "Lost packets %llu   Rejected %llu   Flip errors %llu",
                         s.width, s.height, s.fps, s.hdr ? "HDR10" : "SDR",
                         s.chroma444 ? "4:4:4" : "4:2:0", s.hdr ? 10u : 8u,
                         (s.incoming.load() - incoming) / seconds,
                         (s.decoded.load() - decoded) / seconds, (s.shown.load() - shown) / seconds,
                         refresh, (s.bytes.load() - bytes) * 8.0 / elapsed,
                         samples ? decode_ms / samples : 0, samples ? render_ms / samples : 0,
                         depth, high, (unsigned long long)s.stale.load(),
                         (unsigned long long)s.partial.load(), (unsigned long long)s.losses.load(),
                         (unsigned long long)s.rejected.load(),
                         (unsigned long long)counters.failed);
                s.backend->update_hud(hud, native_agc_hud_enabled() != 0);
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
                              selected_tv_safe);
        log_line("PyroWave negotiated: %dx%d @ %d FPS, %s %s %u-bit limited range, compression=0",
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
        log_line("PyroWave HDR10 rejected: enable HDR on the host capture display; colorspace=%u",
                 unit->colorspace);
        if (error_callback)
            error_callback(-1);
        return DR_OK;
    }
    Frame frame;
    frame.bytes.resize(unit->fullLength);
    frame.presentation_us = unit->presentationTimeUs;
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
            session->queue.pop_front();
            ++session->stale;
        }
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
            "PyroWave session summary: incoming=%llu decoded=%llu shown=%llu stale=%llu "
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
            record_error("VideoOut release failed; restart ProsperoLight before streaming again");
        log_line("PyroWave VideoOut handoff complete: rc=%08x", unsigned(release_result));
    }
}
void prepare_callbacks(void (*on_error)(int), bool vsync, bool tv_safe)
{
    record_error("");
    selected_vsync = vsync;
    selected_tv_safe = tv_safe;
    hdr_known = false;
    hdr_active = false;
    error_callback = on_error;
    presented_count = 0;
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
} // namespace prosperolight::pyrowave
