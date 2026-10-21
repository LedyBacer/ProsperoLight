/*
 * ps5-native-app-boilerplate - ProsperoLight component.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Native game Moonlight/Sunshine Videodec2 zero-copy stream. */

#include "client_preferences.hpp"
#include "scanout_trace.hpp"
#include "pacing_decision_trace.hpp"
#include "host_preferences.hpp"
#if PROSPEROLIGHT_TEST_LAB
#include "test_lab_lifecycle.hpp"
#endif
#include "ps5_dualsense.hpp"
#include "ps5_waveform.hpp"

#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stddef.h>
#include <stdarg.h>
#include <atomic>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <opus_multistream.h>

#include "moonlight_stream.hpp"
#include "stream_profile.hpp"
#include "frame_cadence.hpp"
#include "frame_pacing.hpp"
#include "presentation_preferences.hpp"
#include "app_storage.hpp"
#include "host_quit_preferences.hpp"
#include "native_modules.hpp"
#include "connecting_plate.hpp"
#include "moonlight_config.hpp"
#include "moonlight_physical_input.hpp"
#include "moonlight_stream_input.hpp"
#include "moonlight_stream_keyboard.hpp"
#include "moonlight_performance.hpp"
#include "moonlight_pipeline.hpp"
#include "video_decoder_lifecycle.hpp"
#include "video_parameter_sets.hpp"
#include "video_stream_policy.hpp"
#include "moonlight_tuning.hpp"
#include "../platform/ps5/ps5_network_metrics.h"
#include "../platform/ps5/ps5_fec_cpu.h"
#include "../platform/ps5/ps5_thread_placement.h"
#include "lan_http_report.hpp"
#include "native_agc_present.hpp"
#include "gamestream/certgen.h"
#include "gamestream/client.h"
#include "gamestream/gs_errors.h"
#include "gamestream/gs_http.h"
#include "gamestream/gs_log.h"
#include "../../third_party/moonlight-common-c/src/Limelight.h"
#if PROSPEROLIGHT_PYROWAVE
#include "pyrowave/stream_backend.hpp"
#include "pyrowave/video/ps5_presentation_stats.hpp"
#endif

// Frame slots cover every picture the deepest pipeline can hold, one waiting
// for presentation, one on screen, the protected latest output and one spare.
// Input slots cover every access unit in flight plus the one being copied.
#define FRAME_SLOT_COUNT 7u
#define INPUT_SLOT_COUNT 5u
#define SUBMISSION_QUEUE_CAPACITY 8u
#define INPUT_SLOT_BYTES 0x800000u
// Errors at depth above one before the decoder is rebuilt at depth one, and
// the clean run that forgives earlier ones.
#define PIPELINE_FAULT_LIMIT 3u
#define PIPELINE_CLEAN_OUTPUTS 600u
#define PRESENT_FAILURE_LIMIT 3u
#define CATCHUP_HOLD_US UINT64_C(250000)
#define MOONLIGHT_IDENTITY_DIRECTORY (storage::paths().pairing)
#define CONTROLLER_KEEPALIVE_US UINT64_C(1000000)
#define CONNECTION_SETUP_TIMEOUT_US UINT64_C(20000000)
#define FIRST_VIDEO_FRAME_TIMEOUT_US UINT64_C(10000000)
#define AUDIO_GRAIN_FRAMES 256u
#define AUDIO_STEREO_CHANNELS 2u
#define AUDIO_51_CHANNELS 6u
#define AUDIO_MAX_CHANNELS 8u
#define AUDIO_RING_FRAMES (AUDIO_GRAIN_FRAMES * 6u)
#define AUDIO_DECODE_MAX_FRAMES 5760u
#define AUDIO_OUT_ALREADY_INIT UINT32_C(0x8026000e)
#define PS5_AUDIO_USER_SYSTEM 0xff
#define PS5_AUDIO_PORT_MAIN 0
#define PS5_AUDIO_FORMAT_S16_STEREO 1
#define PS5_AUDIO_FORMAT_S16_8CH 2
#ifndef PROSPEROLIGHT_LAN_TELEMETRY
#define PROSPEROLIGHT_LAN_TELEMETRY 0
#endif
#ifndef PROSPEROLIGHT_AUDIO_MAX_BACKLOG_MS
#define PROSPEROLIGHT_AUDIO_MAX_BACKLOG_MS 0
#endif
#ifndef PROSPEROLIGHT_FEC_SIMD
#define PROSPEROLIGHT_FEC_SIMD 0
#endif
#ifndef PROSPEROLIGHT_OPUS_SIMD
#define PROSPEROLIGHT_OPUS_SIMD 0
#endif
#ifndef PROSPEROLIGHT_PRESENT_OVERLAP
#define PROSPEROLIGHT_PRESENT_OVERLAP 1
#endif
#ifndef PROSPEROLIGHT_FLIP_POLL_US
#define PROSPEROLIGHT_FLIP_POLL_US 500
#endif
#ifndef PROSPEROLIGHT_GPU_TIMESTAMPS
#define PROSPEROLIGHT_GPU_TIMESTAMPS 0
#endif
#ifndef PROSPEROLIGHT_STREAM_SELF_TEST_FPS
#define PROSPEROLIGHT_STREAM_SELF_TEST_FPS 0
#endif
// Opt-in experiments. A sustained queue of this many frames requests a
// keyframe instead of carrying the delay; zero keeps every frame.
#ifndef PROSPEROLIGHT_CATCHUP_QUEUE_FRAMES
#define PROSPEROLIGHT_CATCHUP_QUEUE_FRAMES 0
#endif
// Recover from loss by invalidating reference frames instead of a keyframe.
#ifndef PROSPEROLIGHT_REFERENCE_FRAME_INVALIDATION
#define PROSPEROLIGHT_REFERENCE_FRAME_INVALIDATION 0
#endif
static_assert(PROSPEROLIGHT_CATCHUP_QUEUE_FRAMES >= 0 && PROSPEROLIGHT_CATCHUP_QUEUE_FRAMES <= 14,
              "catch-up must act before moonlight-common-c's 15-frame queue overflows");
static_assert(PROSPEROLIGHT_REFERENCE_FRAME_INVALIDATION == 0 ||
              PROSPEROLIGHT_REFERENCE_FRAME_INVALIDATION == 1);
// Punktfunk's NVENC GameStream path reserves five reference frames. A four-deep
// slot returns OVERSIZE_DECODE. Six slots also fit streams with fewer references.

static_assert(FRAME_SLOT_COUNT >= moonlight::kMaxDecoderDepth + 4u &&
                  INPUT_SLOT_COUNT >= moonlight::kMaxDecoderDepth + 2u &&
                  SUBMISSION_QUEUE_CAPACITY > moonlight::kMaxDecoderDepth &&
                  DECODER_PIPELINE_DEPTH <= moonlight::kMaxDecoderDepth,
              "slot pools must cover the deepest decoder pipeline");

#define PS5_PAD_BUTTON_L3 0x000002u
#define PS5_PAD_BUTTON_R3 0x000004u
#define PS5_PAD_BUTTON_OPTIONS 0x000008u
#define PS5_PAD_BUTTON_UP 0x000010u
#define PS5_PAD_BUTTON_RIGHT 0x000020u
#define PS5_PAD_BUTTON_DOWN 0x000040u
#define PS5_PAD_BUTTON_LEFT 0x000080u
#define PS5_PAD_BUTTON_L1 0x000400u
#define PS5_PAD_BUTTON_R1 0x000800u
#define PS5_PAD_BUTTON_TRIANGLE 0x001000u
#define PS5_PAD_BUTTON_CIRCLE 0x002000u
#define PS5_PAD_BUTTON_CROSS 0x004000u
#define PS5_PAD_BUTTON_SQUARE 0x008000u
#define PS5_PAD_BUTTON_TOUCH_PAD 0x100000u
#define PS5_PAD_BUTTON_INTERCEPTED UINT32_C(0x80000000)
#define PS5_PAD_SAMPLE_CAPACITY 64
#define PS5_PAD_OPEN_ATTEMPTS 20u
#define PS5_PAD_OPEN_RETRY_US 50000u

extern "C"
{
    int sceKernelUsleep(uint32_t microseconds);
    int64_t sceKernelGetDirectMemorySize(void);
    int32_t sceKernelAllocateDirectMemory(int64_t search_start, int64_t search_end, size_t length,
                                          size_t alignment, int memory_type,
                                          int64_t *direct_memory_start);
    int32_t sceKernelMapDirectMemory(void **address, size_t length, int protection, int flags,
                                     int64_t direct_memory_start, size_t alignment);
    int32_t sceKernelAvailableFlexibleMemorySize(size_t *out_size);
    int32_t sceKernelMapNamedFlexibleMemory(void **address, size_t length, int protection,
                                            int flags, const char *name);
    int32_t sceKernelReleaseFlexibleMemory(void *address, size_t length);
    int32_t sceKernelVirtualQuery(void *address, int flags, void *info, size_t info_size);
    int32_t sceKernelMunmap(void *address, size_t length);
    int32_t sceKernelReleaseDirectMemory(int64_t direct_memory_start, size_t length);
    int32_t sceKernelSendNotificationRequest(uint32_t device, void *request, size_t size,
                                             int32_t blocking);
    int sceSystemServiceHideSplashScreen(void);
    int32_t sceUserServiceInitialize(void *params);
    int32_t sceUserServiceGetInitialUser(int32_t *user_id);
    int32_t sceUserServiceTerminate(void);
    int32_t scePadInit(void);
    int32_t scePadOpen(int32_t user_id, int32_t port_type, int32_t index, const void *params);
    int32_t scePadClose(int32_t handle);
    int32_t scePadRead(int32_t handle, void *samples, int32_t capacity);
    int32_t sceKeyboardInit(void);
    int32_t sceKeyboardOpen(int32_t user_id, int32_t type, int32_t index, const void *params);
    int32_t sceKeyboardRead(int32_t handle, void *data, int32_t capacity);
    int32_t sceKeyboardClose(int32_t handle);
    int32_t sceMouseInit(void);
    int32_t sceMouseOpen(int32_t user_id, int32_t type, int32_t index, const void *params);
    int32_t sceMouseRead(int32_t handle, void *data, int32_t count);
    int32_t sceMouseClose(int32_t handle);
    int32_t sceAudioOutInit(void);
    int32_t sceAudioOutOpen(int32_t user_id, int32_t type, int32_t index, uint32_t length,
                            uint32_t frequency, uint32_t format);
    int32_t sceAudioOutOutput(int32_t handle, const void *buffer);
    int32_t sceAudioOutClose(int32_t handle);
    uint64_t PltGetMicroseconds(void);
    int sceKernelOpen(const char *path, int flags, uint16_t mode);
    int sceKernelClose(int descriptor);
    int64_t sceKernelWrite(int descriptor, const void *buffer, size_t length);
    int sceKernelRename(const char *from, const char *to);
    int sceKernelDebugOutText(int channel, const char *text);
}
typedef struct notification_request
{
    uint8_t reserved[45];
    char message[3075];
} notification_request_t;

typedef struct videodec2_decoder_config
{
    uint64_t size;
    uint32_t resource_type, codec_type, profile, max_level;
    int32_t max_width, max_height, max_dpb_frames;
    uint32_t pipeline_depth;
    uint64_t compute_queue, cpu_affinity;
    int32_t cpu_priority;
    uint32_t optimize_progressive, check_memory_type, reserved;
} videodec2_decoder_config_t;

typedef struct videodec2_decoder_memory
{
    uint64_t size, cpu_size;
    void *cpu;
    uint64_t gpu_size;
    void *gpu;
    uint64_t cpu_gpu_size;
    void *cpu_gpu;
    uint64_t max_frame_size;
    uint32_t frame_alignment, reserved;
} videodec2_decoder_memory_t;

typedef struct videodec2_compute_config
{
    uint64_t size;
    uint16_t pipe_id, queue_id;
    uint8_t check_memory_type, reserved0;
    uint16_t reserved1;
} videodec2_compute_config_t;

typedef struct videodec2_compute_memory
{
    uint64_t size, cpu_gpu_size;
    void *cpu_gpu;
} videodec2_compute_memory_t;

typedef struct videodec2_direct_memory
{
    uint64_t size;
    uint64_t allocation_size;
    void *address;
    int64_t direct_start;
} videodec2_direct_memory_t;

typedef struct videodec2_input
{
    uint64_t size;
    void *au;
    uint64_t au_size, pts, dts, attached;
} videodec2_input_t;

typedef struct videodec2_frame
{
    uint64_t size;
    void *buffer;
    uint64_t buffer_size;
    uint32_t accepted, reserved;
} videodec2_frame_t;

typedef struct videodec2_output
{
    uint64_t size;
    uint8_t valid, error, picture_count, padding;
    uint32_t codec, width, pitch, height, reserved;
    void *buffer;
    uint64_t buffer_size;
    uint32_t frame_format, pitch_bytes;
} videodec2_output_t;

typedef struct native_video_mode
{
    uint32_t codec_preference;
    uint32_t resolution_preference;
    int video_format;
    uint32_t codec_type;
    uint32_t profile;
    uint32_t max_level;
    uint32_t max_width;
    uint32_t max_height;
    uint32_t output_width;
    uint32_t output_height;
    uint32_t alternate_output_height;
    uint32_t output_pitch;
    uint32_t visible_width;
    uint32_t visible_height;
    uint32_t hdr;
    const char *name;
} native_video_mode_t;

static const native_video_mode_t video_modes[] = {
    {MOONLIGHT_VIDEO_CODEC_H264, MOONLIGHT_STREAM_RESOLUTION_1080P, VIDEO_FORMAT_H264, 1, 100, 51,
     1920, 1088, 1920, 1088, 0, 2048, 1920, 1080, 0, "H.264 1080p"},
    {MOONLIGHT_VIDEO_CODEC_H264, MOONLIGHT_STREAM_RESOLUTION_1440P, VIDEO_FORMAT_H264, 1, 100, 51,
     2560, 1440, 2560, 1440, 0, 2560, 2560, 1440, 0, "H.264 1440p"},
    {MOONLIGHT_VIDEO_CODEC_H264, MOONLIGHT_STREAM_RESOLUTION_2160P, VIDEO_FORMAT_H264, 1, 100, 52,
     3840, 2176, 3840, 2160, 2176, 3840, 3840, 2160, 0, "H.264 2160p beta"},
    {MOONLIGHT_VIDEO_CODEC_HEVC, MOONLIGHT_STREAM_RESOLUTION_1080P, VIDEO_FORMAT_H265, 0x000ee049,
     1, 123, 1920, 1088, 1920, 1088, 0, 2048, 1920, 1080, 0, "HEVC Main 1080p"},
    {MOONLIGHT_VIDEO_CODEC_HEVC, MOONLIGHT_STREAM_RESOLUTION_1440P, VIDEO_FORMAT_H265, 0x000ee049,
     1, 150, 2560, 1440, 2560, 1440, 0, 2560, 2560, 1440, 0, "HEVC Main 1440p"},
    {MOONLIGHT_VIDEO_CODEC_HEVC, MOONLIGHT_STREAM_RESOLUTION_2160P, VIDEO_FORMAT_H265, 0x000ee049,
     1, 153, 3840, 2176, 3840, 2160, 2176, 3840, 3840, 2160, 0, "HEVC Main 2160p beta"},
    {MOONLIGHT_VIDEO_CODEC_HEVC, MOONLIGHT_STREAM_RESOLUTION_1080P, VIDEO_FORMAT_H265_MAIN10,
     0x000ee049, 2, 123, 1920, 1088, 1920, 1088, 0, 1920, 1920, 1080, 1, "HEVC Main10 HDR 1080p"},
    {MOONLIGHT_VIDEO_CODEC_HEVC, MOONLIGHT_STREAM_RESOLUTION_1440P, VIDEO_FORMAT_H265_MAIN10,
     0x000ee049, 2, 150, 2560, 1440, 2560, 1440, 0, 2560, 2560, 1440, 1, "HEVC Main10 HDR 1440p"},
    {MOONLIGHT_VIDEO_CODEC_HEVC, MOONLIGHT_STREAM_RESOLUTION_2160P, VIDEO_FORMAT_H265_MAIN10,
     0x000ee049, 2, 153, 3840, 2176, 3840, 2160, 2176, 3840, 3840, 2160, 1,
     "HEVC Main10 HDR 2160p"},
};

static_assert(sizeof(video_modes) / sizeof(video_modes[0]) == 9,
              "every supported SDR and Main10 HDR mode needs one entry");

static const native_video_mode_t *find_video_mode(uint32_t codec, uint32_t resolution, uint32_t hdr)
{
    size_t index;

    for (index = 0; index < sizeof(video_modes) / sizeof(video_modes[0]); ++index)
    {
        if (video_modes[index].codec_preference == codec &&
            video_modes[index].resolution_preference == resolution &&
            video_modes[index].hdr == (hdr != 0))
            return &video_modes[index];
    }
    return NULL;
}

static uint32_t decoder_max_level(const native_video_mode_t *mode, uint32_t stream_fps)
{
    if (!mode || stream_fps <= MOONLIGHT_STREAM_FPS_60)
        return mode ? mode->max_level : 0u;
    if (mode->codec_type == 1u)
    {
        if (mode->resolution_preference == MOONLIGHT_STREAM_RESOLUTION_2160P)
            return 60u;
        if (mode->resolution_preference == MOONLIGHT_STREAM_RESOLUTION_1440P)
            return 52u;
        return 51u;
    }
    if (mode->resolution_preference == MOONLIGHT_STREAM_RESOLUTION_2160P)
        return 156u;
    if (mode->resolution_preference == MOONLIGHT_STREAM_RESOLUTION_1440P)
        return 153u;
    return 150u;
}

using ps5_pad_sample_t = prosperolight::dualsense::PadSample;

static_assert(sizeof(ps5_pad_sample_t) == 120,
              "normal Pad samples must use the verified 120-byte ABI");
static_assert(offsetof(ps5_pad_sample_t, connected) == 0x4c,
              "Pad connection state offset must stay verified");
static_assert(offsetof(ps5_pad_sample_t, timestamp_us) == 0x50,
              "Pad timestamp offset must stay verified");
static_assert(offsetof(ps5_pad_sample_t, connected_count) == 0x68,
              "Pad connection generation offset must stay verified");

typedef struct controller_event
{
    int buttons;
    uint8_t left_trigger, right_trigger;
    int16_t left_x, left_y, right_x, right_y;
} controller_event_t;

typedef struct ps5_controller_state
{
    int32_t user_service_result, user_result, pad_init_result;
    int32_t user_id, handle, arrival_result, removal_result;
    uint32_t polls, samples, empty_reads, max_batch, read_errors;
    uint32_t events, send_errors, nonneutral_samples;
    uint32_t disconnected_samples, intercepted_samples;
    uint32_t observed_raw_buttons, observed_moonlight_buttons;
    uint32_t last_raw_buttons;
    uint32_t last_mouse_buttons, mouse_buttons_down;
    uint32_t mouse_toggles, mouse_motion_events, mouse_button_events;
    uint32_t mouse_scroll_events, mouse_errors;
    controller_event_t last_event;
    controller_event_t mouse_event;
    uint64_t last_event_us;
    uint64_t next_mouse_motion_us;
    uint8_t connected_count;
    uint8_t connected_count_valid;
    int announced;
    int mouse_mode;
    int keyboard_mode;
    uint32_t keyboard_selected;
    int keyboard_shifted;
    std::atomic<int> requested_stop;
    ps5_pad_sample_t sample_batch[PS5_PAD_SAMPLE_CAPACITY];
} ps5_controller_state_t;

typedef struct ps5_keyboard_state
{
    uint64_t timestamp_us;
    uint8_t intercepted;
    uint8_t reserved0[7];
    uint8_t connected;
    uint8_t reserved1[3];
    int32_t length;
    uint32_t leds;
    uint32_t modifiers;
    uint16_t keys[16];
    uint8_t reserved2[32];
} ps5_keyboard_state_t;

static_assert(sizeof(ps5_keyboard_state_t) == 96, "Keyboard state must use the verified PS5 ABI");
static_assert(offsetof(ps5_keyboard_state_t, intercepted) == 0x08,
              "Keyboard intercepted offset must stay verified");
static_assert(offsetof(ps5_keyboard_state_t, connected) == 0x10,
              "Keyboard connected offset must stay verified");
static_assert(offsetof(ps5_keyboard_state_t, length) == 0x14,
              "Keyboard length offset must stay verified");
static_assert(offsetof(ps5_keyboard_state_t, modifiers) == 0x1c,
              "Keyboard modifier offset must stay verified");
static_assert(offsetof(ps5_keyboard_state_t, keys) == 0x20,
              "Keyboard key array offset must stay verified");

typedef struct ps5_mouse_data
{
    uint64_t timestamp_us;
    uint8_t connected;
    uint8_t padding0[3];
    uint32_t buttons;
    int32_t x_axis, y_axis, wheel, tilt;
    uint8_t reserved[8];
} ps5_mouse_data_t;

static_assert(sizeof(ps5_mouse_data_t) == 40, "Mouse data must use the verified PS5 ABI");
static_assert(offsetof(ps5_mouse_data_t, buttons) == 0x0c,
              "Mouse button offset must stay verified");
static_assert(offsetof(ps5_mouse_data_t, x_axis) == 0x10, "Mouse axis offset must stay verified");

typedef struct ps5_mouse_open_param
{
    uint8_t behavior_flag;
    uint8_t reserved[7];
} ps5_mouse_open_param_t;

typedef struct ps5_physical_input_state
{
    int32_t keyboard_module_result, keyboard_unload_result;
    int32_t keyboard_init_result, keyboard_open_result, keyboard_close_result;
    int32_t mouse_module_result, mouse_unload_result;
    int32_t mouse_init_result, mouse_open_result, mouse_close_result;
    int32_t keyboard_handles[12], mouse_handles[8];
    uint32_t keyboard_handle_count, mouse_handle_count;
    uint32_t keyboard_polls, keyboard_read_errors, keyboard_events, keyboard_send_errors;
    uint32_t mouse_polls, mouse_samples, mouse_read_errors, mouse_motion_events;
    uint32_t mouse_button_events, mouse_scroll_events, mouse_send_errors;
    uint32_t mouse_buttons[8];
    ps5_keyboard_state_t keyboards[12];
    ps5_keyboard_state_t keyboard_samples_batch[16];
    ps5_mouse_data_t mouse_samples_batch[64];
    int initialization_attempted;
} ps5_physical_input_state_t;

typedef struct ps5_audio_state
{
    OpusMSDecoder *decoder;
    int32_t init_result, open_result, drain_result, close_result;
    int32_t handle, opus_error;
    int channels, output_channels, samples_per_frame;
    uint32_t ring_head, ring_tail, ring_count;
    uint32_t packets, plc_packets, decode_errors;
    uint32_t output_calls, output_errors, overruns;
    uint32_t packet_samples_min, packet_samples_max, packet_sample_mismatches;
    uint32_t peak_sample;
    RTP_AUDIO_STATS rtp;
    uint64_t decoded_frames, nonzero_samples, dropped_frames;
    uint64_t first_packet_us, last_packet_us;
    uint64_t interval_total_us, interval_min_us, interval_max_us;
    uint64_t decode_total_us, decode_max_us;
    uint64_t output_total_us, output_max_us;
    moonlight::TimingHistogram decode_timing, output_timing;
    uint32_t pending_ms_high_water, ring_high_water, catchup_packets;
    uint64_t catchup_frames;
    int16_t ring[AUDIO_RING_FRAMES * AUDIO_MAX_CHANNELS];
    int16_t output[AUDIO_GRAIN_FRAMES * AUDIO_MAX_CHANNELS];
    int16_t decoded[AUDIO_DECODE_MAX_FRAMES * AUDIO_MAX_CHANNELS];
} ps5_audio_state_t;

static ps5_audio_state_t make_audio_state()
{
    ps5_audio_state_t state{};

    state.open_result = -1;
    state.drain_result = -1;
    state.close_result = -1;
    state.handle = -1;
    return state;
}

static ps5_audio_state_t audio_state = make_audio_state();

extern "C"
{
    int32_t sceVideodec2QueryDecoderMemoryInfo(const videodec2_decoder_config_t *config,
                                               videodec2_decoder_memory_t *memory);
    int32_t sceVideodec2QueryComputeMemoryInfo(videodec2_compute_memory_t *memory);
    int32_t sceVideodec2AllocateComputeQueue(const videodec2_compute_config_t *config,
                                             const videodec2_compute_memory_t *memory,
                                             void **queue);
    int32_t sceVideodec2ReleaseComputeQueue(void *queue);
    int32_t sceVideodec2CreateDecoder(const videodec2_decoder_config_t *config,
                                      const videodec2_decoder_memory_t *memory, void **decoder);
    int32_t sceVideodec2DeleteDecoder(void *decoder);
    int32_t sceVideodec2MapDirectMemory(void *decoder, const videodec2_direct_memory_t *memory);
    int32_t sceVideodec2Reset(void *decoder);
    int32_t sceVideodec2Decode(void *decoder, videodec2_input_t *input, videodec2_frame_t *frame,
                               videodec2_output_t *output);
    int32_t sceVideodec2Flush(void *decoder, videodec2_frame_t *frame, videodec2_output_t *output);
}

extern "C" void moonlight_native_decoder_warmup(int results[4])
{
    videodec2_compute_memory_t compute{};
    compute.size = sizeof(compute);
    results[0] = sceVideodec2QueryComputeMemoryInfo(&compute);
    for (unsigned index = 0; index < 3; ++index)
    {
        const native_video_mode_t *mode =
            find_video_mode(index == 0 ? MOONLIGHT_VIDEO_CODEC_H264 : MOONLIGHT_VIDEO_CODEC_HEVC,
                            MOONLIGHT_STREAM_RESOLUTION_2160P, index == 2);
        videodec2_decoder_config_t config{};
        videodec2_decoder_memory_t memory{};
        config.size = sizeof(config);
        config.resource_type = 1;
        config.codec_type = mode->codec_type;
        config.profile = mode->profile;
        config.max_level = mode->max_level;
        config.max_width = mode->max_width;
        config.max_height = mode->max_height;
        config.max_dpb_frames = 4;
        config.pipeline_depth = 1;
        config.cpu_affinity = moonlight::kClassicDecoderCpuMask;
        config.cpu_priority = DECODER_CPU_PRIORITY;
        config.optimize_progressive = 1;
        memory.size = sizeof(memory);
        results[index + 1] = sceVideodec2QueryDecoderMemoryInfo(&config, &memory);
    }
}

static notification_request_t notification;
static std::atomic<int> connection_terminated;
static std::atomic<int> connection_error;
static std::atomic<int> connection_failed_stage;
static std::atomic<uint32_t> host_hdr_active;
static std::atomic<uint32_t> host_hdr_transitions;
// Bumped whenever queued frames are discarded for decoder backpressure: a
// moonlight-common-c queue overflow, or a decoder refresh this client asked for.
static std::atomic<uint32_t> video_recovery_epoch;
static std::atomic<uint32_t> video_queue_overflows;
static std::atomic<uint32_t> video_unrecoverable_frames;

typedef struct decoder_resources
{
    videodec2_decoder_memory_t memory;
    void *decoder;
    int64_t gpu_start, cpu_gpu_start;
    size_t gpu_size, cpu_gpu_size, cpu_mapping_size;
} decoder_resources_t;

typedef struct stream_submission
{
    uint64_t arrival_us; // local monotonic clock when the decode worker took it
    uint64_t enqueue_us; // moonlight-common-c clock
    uint64_t pts_us;
    int32_t frame;
    moonlight::FrameTrace::Sample *trace;
} stream_submission_t;

// One decoded picture on its way to the display.
typedef struct stream_ready_frame
{
    int slot;
    void *buffer;
    size_t buffer_size;
    uint32_t pitch, height;
    int32_t frame;
    uint64_t arrival_us, enqueue_us, ready_us, pts_us;
    moonlight::FrameTrace::Sample *trace;
    native_agc_metrics_t hud;
} stream_ready_frame_t;

// Three owners. Session policy is set before workers start. The decode worker
// owns decoder storage/configuration and its statistics; presentation borrows
// only ready surfaces and owns AGC/statistics. Reconfiguration joins presentation
// before changing storage. Frames/mailbox/stop_presenting use `lock`; aggregates
// are read only after both workers have joined.
typedef struct native_renderer_state
{
    // Negotiated presentation context (immutable while workers run).
    const native_video_mode_t *mode;
    uint32_t stream_fps;
    uint32_t client_refresh_x100;
    // Decode-owned storage/configuration, replaced only behind the join barrier.
    void *input_memory;
    void *frame_memory;
    size_t input_size;
    size_t frame_size;
    videodec2_decoder_config_t decoder_config;
    videodec2_decoder_memory_t decoder_memory;
    // Session policy and effective placement (snapshotted after join).
    uint32_t pipeline_mode; // MOONLIGHT_DECODER_PIPELINE_*
    uint32_t requested_depth;
    uint32_t decoder_cores;
    uint64_t requested_cpu_mask, decoder_cpu_mask;
    uint32_t create_attempts;
    uint64_t process_cpu_mask;
    moonlight::ThreadLayout layout;
    uint32_t slices_requested;
    uint32_t vsync_requested;
    int main_placement_result;

    // Decode worker is the exclusive lifecycle/resource owner.
    moonlight::video::Lifecycle lifecycle;
    decoder_resources_t owned_resources;
    int64_t direct_limit, owned_input_start, owned_frame_start;
    size_t owned_input_bytes, owned_frame_bytes;
    bool stream_aware;
    bool recovery_pending;
    bool parameters_requested;
    uint32_t test_capacity_errors;
    moonlight::video::ParameterSets parameter_sets;
    uint64_t transition_wait_since;
    uint64_t parameter_transition_waits;
    moonlight::video::PipelineSampler pipeline_sampler;
    moonlight::video::RuntimePolicy stream_policy;
    bool policy_keyframe_wait{};
    uint64_t policy_dropped_aus{};
    uint64_t delivery_session_floor_us{};
    moonlight::TimingHistogram au_size;
    uint32_t active_pps{256};
    moonlight::video::PpsFeatures
        active_pps_features{}; // Last successfully submitted facts survive shutdown.
    uint64_t h264_picture_types[5]{};
    uint32_t test_capacity_decoded_before, test_capacity_presented_before;
    void *decoder;
    uint32_t pipeline_depth; // effective; one after a fallback
    bool drain_enabled;
    bool decoder_needs_reset;
    bool await_keyframe;
    bool decoder_lost;
    uint32_t pipeline_faults; // errors at depth above one since the last clean run
    uint32_t clean_outputs;
    size_t stream_bytes;
    uint32_t access_units;
    uint32_t fragments;
    uint32_t decode_calls;
    uint32_t flush_calls;
    uint32_t drain_calls;
    uint32_t drain_faults;
    uint32_t decoder_recreations;
    uint32_t decoder_refreshes;
    uint32_t decode_errors;
    int32_t last_decode_error;
    uint32_t catchup_refreshes;
    uint32_t decoded;
    std::atomic<uint32_t> not_displayed;
    PacingDecisionTrace *pacing_decisions{};
    uint32_t decoder_delayed;
    uint32_t input_sequence;
    uint64_t copy_total_us;
    uint64_t copy_max_us;
    uint64_t decode_total_us;
    uint64_t decode_max_us;
    uint64_t flush_total_us;
    uint64_t flush_max_us;
    uint64_t callback_to_decode_total_us;
    uint64_t callback_to_decode_min_us;
    uint64_t callback_to_decode_max_us;
    uint32_t ready_calls;
    uint32_t host_latency_frames;
    uint32_t host_latency_min_tenths_ms;
    uint32_t host_latency_max_tenths_ms;
    uint64_t host_latency_total_tenths_ms;
    uint32_t rtt_ms;
    uint32_t rtt_variance_ms;
    int rtt_valid;
    uint64_t first_video_us;
    uint64_t first_decoded_us;
    uint64_t last_video_us;
    uint32_t pending_video_high_water;
    uint32_t reassembly_invalid_samples;
    uint32_t observed_slices_min, observed_slices_max, observed_slices_last, layout_samples;
    moonlight::TimingHistogram idr_decode_timing, inter_decode_timing;
    uint64_t decode_over_budget, decode_first_us;
    uint32_t hdr_mismatch_reported;
    int32_t last_result;
    stream_submission_t submissions[SUBMISSION_QUEUE_CAPACITY];
    uint32_t submission_head;
    uint32_t submission_count;
    moonlight::DropAttribution drops;
    moonlight::ArrivalRate arrival;
    moonlight::RateWindow decoded_rate;
    moonlight::WindowedTiming decode_window, queue_window;
    moonlight::CatchUpGuard catchup;
    moonlight::TimingHistogram copy_timing, decode_timing, queue_timing, reassembly_timing;
    moonlight::TimingHistogram ready_timing, flush_timing, host_timing;

    // Shared under `lock`.
    pthread_mutex_t lock;
    pthread_cond_t wake;
    moonlight::SlotPool<FRAME_SLOT_COUNT> frames;
    moonlight::ReadyMailbox<stream_ready_frame_t> mailbox;
    moonlight::SourceTimestamp source_clock;
    bool stop_presenting;

    // Presentation worker.
    std::atomic<uint32_t> presented;
    uint32_t present_errors;
    uint32_t latency_calls;
    uint64_t present_total_us;
    uint64_t present_max_us;
    uint64_t callback_to_flip_total_us;
    uint64_t callback_to_flip_min_us;
    uint64_t callback_to_flip_max_us;
    uint64_t first_present_us;
    uint64_t last_present_us;
    moonlight::RateWindow rendering_rate;
    moonlight::TimingHistogram frame_age_timing, flip_interval_timing, present_wait_timing;
    moonlight::TimingHistogram present_call_timing, completion_wait_timing;
    std::atomic<int32_t> present_result;

    // Worker lifetime, owned by the thread that starts and stops the stream.
    pthread_t decode_thread, present_thread;
    bool sync_ready, decode_started, present_started;
    int decode_placement_result, present_placement_result;
    std::atomic<int> running;
} native_renderer_state_t;

static native_renderer_state_t *active_renderer;
// Not on the renderer's stack; only the optional probe records this buffer.
#if PROSPEROLIGHT_PERFORMANCE_DETAIL
static moonlight::FrameTrace frame_trace;
#endif
static bool presentation_faulted;

// One bounded local summary AFTER workers join. No addresses, host identities,
// key events or payloads; no per-frame file/network I/O. Failure is nonfatal.
static void log_performance_summary(const char *report, size_t length, bool new_session = true)
{
    if (!prosperolight_logs_enabled())
        return;
    // Bound each kernel record; never dump the per-frame trace into klog.
    constexpr size_t chunk_bytes = 384;
    static unsigned session = 0;
    if (new_session)
        ++session;
    const size_t parts = (length + chunk_bytes - 1) / chunk_bytes;
    for (size_t offset = 0; offset < length; offset += chunk_bytes)
    {
        const size_t count = length - offset < chunk_bytes ? length - offset : chunk_bytes;
        char line[512];
        const int prefix = snprintf(line, sizeof(line),
                                    "[ProsperoLight perf] session=%u part=%zu/%zu json=", session,
                                    offset / chunk_bytes + 1, parts);
        if (prefix < 0 || static_cast<size_t>(prefix) + count + 2 > sizeof(line))
            return;
        for (size_t i = 0; i < count; ++i)
            line[prefix + i] = report[offset + i] == '\n' ? ' ' : report[offset + i];
        line[prefix + count] = '\n';
        line[prefix + count + 1] = '\0';
        if (sceKernelDebugOutText(0, line) < 0)
            return;
    }
}

static void log_performance_windows(uint32_t fps)
{
#if PROSPEROLIGHT_PERFORMANCE_DETAIL
    if (!frame_trace.count || !fps)
        return;
    // Post-stream aggregation only. At most 300 occupied five-second windows.
    const uint64_t origin = frame_trace.samples[0].callback_us;
    const uint64_t budget = (1000000u + fps - 1) / fps;
    uint64_t prior_receive = 0, prior_flip = 0;
    size_t index = 0, windows = 0;
    while (index < frame_trace.count && windows++ < 300)
    {
        const auto window = (frame_trace.samples[index].callback_us - origin) / 5000000u;
        moonlight::TimingHistogram decode{}, host{}, receive{}, queue{}, reassembly{}, flip{};
        uint64_t bytes = 0, over = 0, pending = 0, presented = 0, wait_max = 0, gaps = 0;
        uint64_t superseded = 0;
        const size_t start = index;
        while (index < frame_trace.count)
        {
            const auto &s = frame_trace.samples[index];
            if ((s.callback_us - origin) / 5000000u != window)
                break;
            bytes += s.bytes;
            decode.add(s.decode_us);
            over += s.decode_us > budget;
            superseded += s.outcome == 2u;
            if (s.host_us)
                host.add(s.host_us);
            if (s.pending > pending)
                pending = s.pending;
            if (s.present_wait_us > wait_max)
                wait_max = s.present_wait_us;
            if (s.receive_us && prior_receive && s.receive_us >= prior_receive)
                receive.add(s.receive_us - prior_receive);
            if (s.receive_us)
                prior_receive = s.receive_us;
            if (s.receive_us && s.enqueue_us >= s.receive_us)
                reassembly.add(s.enqueue_us - s.receive_us);
            if (s.enqueue_us && s.callback_network_us >= s.enqueue_us)
                queue.add(s.callback_network_us - s.enqueue_us);
            if (s.completion_us)
            {
                ++presented;
                if (prior_flip && s.completion_us >= prior_flip)
                    flip.add(s.completion_us - prior_flip);
                prior_flip = s.completion_us;
            }
            if (index && s.frame > frame_trace.samples[index - 1].frame &&
                s.frame - frame_trace.samples[index - 1].frame > 1)
                gaps += s.frame - frame_trace.samples[index - 1].frame - 1;
            ++index;
        }
        char row[1024];
        const int length = snprintf(
            row, sizeof(row),
            "{\"kind\":\"window\",\"start_s\":%llu,\"duration_s\":5,\"frames\":%zu,"
            "\"presented\":%llu,\"not_displayed\":%llu,\"bytes\":%llu,\"gaps\":%llu,"
            "\"pending_max\":%llu,"
            "\"decode_mean_us\":%llu,\"decode_p99_upper_us\":%llu,\"decode_max_us\":%llu,"
            "\"decode_over_budget\":%llu,\"budget_us\":%llu,\"host_count\":%llu,"
            "\"host_mean_us\":%llu,\"host_max_us\":%llu,\"receive_gap_max_us\":%llu,"
            "\"reassembly_max_us\":%llu,\"queue_max_us\":%llu,"
            "\"flip_gap_max_us\":%llu,\"present_wait_max_us\":%llu}",
            (unsigned long long)(window * 5), index - start, (unsigned long long)presented,
            (unsigned long long)superseded, (unsigned long long)bytes, (unsigned long long)gaps,
            (unsigned long long)pending, (unsigned long long)(decode.total_us / decode.count),
            (unsigned long long)decode.percentile(99), (unsigned long long)decode.max_us,
            (unsigned long long)over, (unsigned long long)budget, (unsigned long long)host.count,
            (unsigned long long)(host.count ? host.total_us / host.count : 0),
            (unsigned long long)host.max_us, (unsigned long long)receive.max_us,
            (unsigned long long)reassembly.max_us, (unsigned long long)queue.max_us,
            (unsigned long long)flip.max_us, (unsigned long long)wait_max);
        if (length > 0 && static_cast<size_t>(length) < sizeof(row))
            log_performance_summary(row, static_cast<size_t>(length), false);
    }
    char end[192];
    const int length = snprintf(end, sizeof(end),
                                "{\"kind\":\"windows_end\",\"recorded\":%zu,"
                                "\"reported\":%zu,\"omitted\":%zu}",
                                frame_trace.count, index, frame_trace.omitted);
    if (length > 0 && static_cast<size_t>(length) < sizeof(end))
        log_performance_summary(end, static_cast<size_t>(length), false);
#else
    (void)fps;
#endif
}

static bool write_performance_bytes(int descriptor, const char *data, size_t length)
{
    size_t written = 0;
    while (written < length)
    {
        const int64_t count = sceKernelWrite(descriptor, data + written, length - written);
        if (count <= 0 || static_cast<uint64_t>(count) > length - written)
            return false;
        written += static_cast<size_t>(count);
    }
    return true;
}

static void save_frame_trace(unsigned mode)
{
    if (!prosperolight_logs_enabled())
        return;
#if PROSPEROLIGHT_PERFORMANCE_DETAIL
    char temporary[176], destination[176];
    snprintf(destination, sizeof(destination), "%s/performance-frames-mode%u.csv",
             storage::paths().performance, mode);
    snprintf(temporary, sizeof(temporary), "%s.tmp", destination);
    // Rows are batched: one write per row cost tens of thousands of system calls.
    static char batch[65536];
    const int descriptor = sceKernelOpen(temporary, 0x601, 0644);
    if (descriptor < 0)
        return;
    size_t used = 0;
    int length = snprintf(batch, sizeof(batch),
                          "# schema=2,count=%zu,omitted=%zu\n"
                          "# scope=dequeued_AUs_only;receive_us_excludes_upstream_discards\n"
                          "frame,bytes,pending,outcome,receive_us,enqueue_us,callback_network_us,"
                          "callback_us,pts_us,decode_us,ready_us,present_wait_us,submit_us,"
                          "completion_us,host_us\n",
                          frame_trace.count, frame_trace.omitted);
    bool ok = length > 0 && static_cast<size_t>(length) < sizeof(batch);
    if (ok)
        used = static_cast<size_t>(length);
    for (size_t i = 0; ok && i < frame_trace.count; ++i)
    {
        const auto &s = frame_trace.samples[i];
        if (sizeof(batch) - used < 512u)
        {
            ok = write_performance_bytes(descriptor, batch, used);
            used = 0;
        }
        length =
            snprintf(batch + used, sizeof(batch) - used,
                     "%u,%u,%u,%u,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%u\n", s.frame,
                     s.bytes, s.pending, s.outcome, (unsigned long long)s.receive_us,
                     (unsigned long long)s.enqueue_us, (unsigned long long)s.callback_network_us,
                     (unsigned long long)s.callback_us, (unsigned long long)s.pts_us,
                     (unsigned long long)s.decode_us, (unsigned long long)s.ready_us,
                     (unsigned long long)s.present_wait_us, (unsigned long long)s.submit_us,
                     (unsigned long long)s.completion_us, s.host_us);
        ok = ok && length > 0 && static_cast<size_t>(length) < sizeof(batch) - used;
        if (ok)
            used += static_cast<size_t>(length);
    }
    if (ok && used)
        ok = write_performance_bytes(descriptor, batch, used);
    const int closed = sceKernelClose(descriptor);
    if (ok && closed == 0)
        (void)sceKernelRename(temporary, destination);
#endif
}

__attribute__((format(printf, 4, 5))) static bool
report_append(char *report, size_t capacity, size_t *length, const char *format, ...)
{
    va_list arguments;
    const size_t available = capacity - *length;
    va_start(arguments, format);
    const int added = vsnprintf(report + *length, available, format, arguments);
    va_end(arguments);
    if (added < 0 || static_cast<size_t>(added) >= available)
        return false;
    *length += static_cast<size_t>(added);
    return true;
}

static prosperolight::dualsense::Statistics controller_summary{};

static void save_performance_summary(const native_renderer_state_t &state,
                                     const moonlight::TimingHistogram &input_intervals,
                                     const moonlight_stream_options_t *options, int result)
{
    if (!prosperolight_logs_enabled() || !state.mode || !options)
        return;
    static char report[16384];
    const auto &agc = native_agc_performance();
    const auto network = ps5_network_metrics_read();
    const auto placement = ps5_thread_placement_stats();
    uint32_t output_width = 0, output_height = 0, refresh_x100 = 0;
    size_t length = 0;
    native_agc_output_status(&output_width, &output_height, &refresh_x100);
    bool ok = report_append(
        report, sizeof(report), &length,
        "{\n\"schema\":3,\"result\":%d,\"width\":%u,\"height\":%u,\"fps\":%u,"
        "\"bitrate_kbps\":%u,\"codec\":%u,\"hdr\":%u,\"audio_channels\":%d,"
        "\"output_width\":%u,\"output_height\":%u,\"refresh_x100\":%u,\n"
        "\"client_refresh_x100\":%u,\"reassembly_invalid_samples\":%u,"
        "\"fec_simd\":%d,\"fec_path\":\"%s\",\"opus_simd\":%d,\"audio_backlog_limit_ms\":%d,"
        "\"requested_slices_per_frame\":%u,\"present_overlap\":%d,\"flip_poll_us\":%d,"
        "\"performance_detail\":%d,\"stream_bytes\":%llu,\n",
        result, state.mode->visible_width, state.mode->visible_height, state.stream_fps,
        options->bitrate_kbps, state.mode->codec_preference, state.mode->hdr, audio_state.channels,
        output_width, output_height, refresh_x100, state.client_refresh_x100,
        state.reassembly_invalid_samples, PROSPEROLIGHT_FEC_SIMD,
        ps5_fec_cpu_supports("avx2")    ? "avx2"
        : ps5_fec_cpu_supports("ssse3") ? "ssse3"
                                        : "scalar",
        PROSPEROLIGHT_OPUS_SIMD, PROSPEROLIGHT_AUDIO_MAX_BACKLOG_MS, state.slices_requested,
        PROSPEROLIGHT_PRESENT_OVERLAP, PROSPEROLIGHT_FLIP_POLL_US, PROSPEROLIGHT_PERFORMANCE_DETAIL,
        (unsigned long long)state.stream_bytes);
    ok = ok &&
         report_append(
             report, sizeof(report), &length,
             "\"access_units\":%u,\"decoded\":%u,\"presented\":%u,\"not_displayed\":%u,"
             "\"network_frame_gaps\":%llu,\"decoder_frame_gaps\":%llu,\"queue_overflows\":%u,"
             "\"decoder_refreshes\":%u,\"decode_errors\":%u,\"last_decode_error\":%d,"
             "\"catchup_refreshes\":%u,\"unrecoverable_frames\":%u,"
             "\"pending_video_high_water\":%u,\n",
             state.access_units, state.decoded, state.presented.load(), state.not_displayed.load(),
             (unsigned long long)state.drops.network, (unsigned long long)state.drops.decoder,
             std::atomic_load_explicit(&video_queue_overflows, std::memory_order_relaxed),
             state.decoder_refreshes, state.decode_errors, (int)state.last_decode_error,
             state.catchup_refreshes,
             std::atomic_load_explicit(&video_unrecoverable_frames, std::memory_order_relaxed),
             state.pending_video_high_water);
    ok = ok &&
         report_append(
             report, sizeof(report), &length,
             "\"decoder_mode\":\"%s\",\"decoder_pipeline_depth\":%u,"
             "\"decoder_depth_requested\":%u,\"decoder_drain\":%u,\"drain_calls\":%u,"
             "\"drain_faults\":%u,\"decoder_recreations\":%u,\"decoder_create_attempts\":%u,"
             "\"decoder_cores\":%u,\"decoder_cpu_affinity\":%llu,"
             "\"decoder_cpu_affinity_requested\":%llu,\"decoder_cpu_priority\":%d,"
             "\"max_dpb_frames\":%d,\"reference_frame_invalidation\":%d,"
             "\"catchup_queue_frames\":%d,\"flush_calls\":%u,\"decoder_delayed\":%u,"
             "\"observed_slices_min\":%u,\"observed_slices_max\":%u,\"layout_samples\":%u,\n",
             state.pipeline_mode == MOONLIGHT_DECODER_PIPELINE_CLASSIC ? "classic" : "adaptive",
             state.pipeline_depth, state.requested_depth, state.drain_enabled ? 1u : 0u,
             state.drain_calls, state.drain_faults, state.decoder_recreations,
             state.create_attempts, state.decoder_cores, (unsigned long long)state.decoder_cpu_mask,
             (unsigned long long)state.requested_cpu_mask, DECODER_CPU_PRIORITY,
             state.decoder_config.max_dpb_frames, PROSPEROLIGHT_REFERENCE_FRAME_INVALIDATION,
             PROSPEROLIGHT_CATCHUP_QUEUE_FRAMES, state.flush_calls, state.decoder_delayed,
             state.observed_slices_min, state.observed_slices_max, state.layout_samples);
    ok = ok &&
         report_append(report, sizeof(report), &length,
                       "\"decoder_test_capacity_decoded_before\":%u,\"decoder_test_capacity_"
                       "presented_before\":%u,\n",
                       state.test_capacity_decoded_before, state.test_capacity_presented_before);
    ok = ok && report_append(report, sizeof(report), &length,
                             "\"decoder_parameter_transition_waits\":%llu,\n",
                             (unsigned long long)state.parameter_transition_waits);
    const auto &window = state.pipeline_sampler.last;
    const auto &features = state.active_pps_features;
    ok = ok &&
         report_append(
             report, sizeof(report), &length,
             "\"stream_bit_depth\":%u,\"stream_reorder_frames\":%u,\"stream_vps_id\":%u,"
             "\"stream_poc_type\":%u,\"stream_scaling_lists\":%u,\"stream_pps_features_known\":%s,"
             "\"stream_cabac\":%u,\"stream_hevc_tiles\":%u,\"stream_hevc_wpp\":%u,"
             "\"stream_hevc_tile_columns\":%u,\"stream_hevc_tile_rows\":%u,"
             "\"h264_p_pictures\":%llu,\"h264_b_pictures\":%llu,\"h264_i_pictures\":%llu,"
             "\"au_size_bytes_p50_upper\":%llu,\"au_size_bytes_p95_upper\":%llu,\"au_size_bytes_"
             "max\":%llu,\n",
             state.lifecycle.stream.bit_depth, state.lifecycle.stream.reorder,
             state.lifecycle.stream.vps_id, state.lifecycle.stream.poc_type,
             state.lifecycle.stream.scaling_lists ? 1u : 0u, features.known ? "true" : "false",
             features.cabac ? 1u : 0u, features.tiles ? 1u : 0u, features.wpp ? 1u : 0u,
             features.tile_columns, features.tile_rows,
             (unsigned long long)state.h264_picture_types[0],
             (unsigned long long)state.h264_picture_types[1],
             (unsigned long long)state.h264_picture_types[2],
             (unsigned long long)state.au_size.percentile(50),
             (unsigned long long)state.au_size.percentile(95),
             (unsigned long long)state.au_size.max_us);
    ok =
        ok &&
        report_append(
            report, sizeof(report), &length,
            "\"pipeline_diagnostics\":{\"classification\":\"%s\",\"confidence\":\"%s\","
            "\"window_us\":%llu,\"window_count\":%llu,\"submitted\":%llu,\"decoded\":%llu,"
            "\"presented\":%llu,"
            "\"arrival_estimate_valid\":%s,\"arrival_fps_x100\":%u,\"decoded_fps_x100\":%u,"
            "\"presented_fps_x100\":%u,\"decode_busy_us\":%llu,\"ready_mean_us\":%llu,"
            "\"queue_mean_us\":%llu,\"pending_begin\":%u,\"pending_end\":%u,"
            "\"semantics\":\"last client-only window; no independent host FPS; suspected stage, "
            "not physical latency\"},\n",
            moonlight::video::name(window.bottleneck),
            window.bottleneck == moonlight::video::Bottleneck::Insufficient ? "unknown"
            : window.bottleneck == moonlight::video::Bottleneck::Healthy    ? "observed-delivery"
                                                                            : "suspected",
            (unsigned long long)window.elapsed_us,
            (unsigned long long)state.pipeline_sampler.windows,
            (unsigned long long)window.submitted, (unsigned long long)window.decoded,
            (unsigned long long)window.presented, window.incoming_estimate_valid ? "true" : "false",
            window.incoming_fps_x100, window.decoded_fps_x100, window.presented_fps_x100,
            (unsigned long long)window.busy_us, (unsigned long long)window.ready_mean_us,
            (unsigned long long)window.queue_mean_us, window.pending_begin, window.pending_end);
    const auto &policy = state.stream_policy;
    ok = ok &&
         report_append(
             report, sizeof(report), &length,
             "\"stream_policy\":{\"last_action\":\"%s\",\"depth_trials\":%u,\"rollbacks\":%u,"
             "\"resyncs\":%u,\"rejected\":%u,\"kept\":%u,\"evaluating\":%s,\"throughput_exceeded\":"
             "%s,"
             "\"pending_depth\":%u,\"discarded_aus\":%llu,\"quality_changed\":false,"
             "\"reconfiguration_enabled\":%s},\n",
             moonlight::video::name(policy.last_action), policy.trials, policy.rollbacks,
             policy.resyncs, policy.rejected, policy.kept, policy.evaluating ? "true" : "false",
             policy.throughput_exceeded ? "true" : "false", policy.pending_depth,
             (unsigned long long)state.policy_dropped_aus,
             policy.allow_reconfiguration ? "true" : "false");
#ifdef LI_VIDEO_DELIVERY_STATS_VERSION
    LI_VIDEO_DELIVERY_STATS delivery{};
    if (LiGetVideoDeliveryStats(&delivery) && state.delivery_session_floor_us &&
        delivery.start_us >= state.delivery_session_floor_us)
    {
        ok = ok &&
             report_append(
                 report, sizeof(report), &length,
                 "\"video_delivery\":{\"available\":true,\"version\":1,\"scope\":\"whole-session\","
                 "\"clock\":\"Limelight "
                 "monotonic\",\"start_us\":%llu,\"end_us\":%llu,\"quiescent\":%s,"
                 "\"video_packets\":%llu,"
                 "\"video_bytes\":%llu,"
                 "\"completed_frame_events\":%llu,"
                 "\"recovery_filtered_aus\":%llu,"
                 "\"empty_aus\":%llu,"
                 "\"assembled_aus\":%llu,"
                 "\"offered_aus\":%llu,"
                 "\"queue_accepted_aus\":%llu,"
                 "\"dequeued_aus\":%llu,"
                 "\"overflow_discarded_aus\":%llu,"
                 "\"refresh_discarded_aus\":%llu,"
                 "\"shutdown_discarded_aus\":%llu,"
                 "\"interrupted_discarded_aus\":%llu,"
                 "\"allocation_discarded_aus\":%llu,"
                 "\"direct_submit\":%s},\n",
                 (unsigned long long)delivery.start_us, (unsigned long long)delivery.end_us,
                 delivery.end_us ? "true" : "false", (unsigned long long)delivery.video_packets,
                 (unsigned long long)delivery.video_bytes,
                 (unsigned long long)delivery.completed_frame_events,
                 (unsigned long long)delivery.recovery_filtered_aus,
                 (unsigned long long)delivery.empty_aus, (unsigned long long)delivery.assembled_aus,
                 (unsigned long long)delivery.offered_aus,
                 (unsigned long long)delivery.queue_accepted_aus,
                 (unsigned long long)delivery.dequeued_aus,
                 (unsigned long long)delivery.overflow_discarded_aus,
                 (unsigned long long)delivery.refresh_discarded_aus,
                 (unsigned long long)delivery.shutdown_discarded_aus,
                 (unsigned long long)delivery.interrupted_discarded_aus,
                 (unsigned long long)delivery.allocation_discarded_aus,
                 delivery.direct_submit ? "true" : "false");
    }
    else
#endif
        ok = ok && report_append(
                       report, sizeof(report), &length,
                       "\"video_delivery\":{\"available\":false,\"scope\":\"whole-session\"},\n");
    const auto &life = state.lifecycle;
    ok = ok &&
         report_append(
             report, sizeof(report), &length,
             "\"decoder_lifecycle_state\":\"%s\",\"decoder_generation\":%u,"
             "\"decoder_initialization_us\":%llu,\"decoder_reconfiguration_count\":%u,"
             "\"decoder_reconfiguration_us\":%llu,\"decoder_recovery_attempts\":%u,"
             "\"decoder_recovery_successes\":%u,\"decoder_recovery_failures\":%u,"
             "\"decoder_lifecycle_failures\":%u,\"decoder_recovery_reason\":%u,\"decoder_test_"
             "capacity_errors\":%u,\"stream_parameters_verified\":%u,"
             "\"stream_coded_width\":%u,\"stream_coded_height\":%u,"
             "\"stream_profile\":%u,\"stream_level\":%u,\"stream_reference_frames\":%u,"
             "\"stream_required_dpb_frames\":%u,\"stream_standard_max_dpb_frames\":%u,"
             "\"decoder_cpu_memory_bytes\":%llu,\"decoder_gpu_memory_bytes\":%llu,"
             "\"decoder_shared_memory_bytes\":%llu,\"decoder_frame_pool_bytes\":%llu,"
             "\"decoder_ready_us\":%llu,\"decoder_first_decoded_us\":%llu,"
             "\"decoder_first_presented_us\":%llu,\n",
             moonlight::video::name(life.state), life.generation,
             (unsigned long long)life.initialization_us, life.reconfigurations,
             (unsigned long long)life.reconfiguration_us, life.attempts, life.successes,
             life.failed_recoveries, life.failures, unsigned(life.reason),
             state.test_capacity_errors, life.stream.verified ? 1u : 0u, life.stream.width,
             life.stream.height, life.stream.profile, life.stream.level, life.stream.references,
             life.stream.buffering, life.stream.standard_dpb,
             (unsigned long long)(state.owned_resources.memory.cpu
                                      ? state.owned_resources.cpu_mapping_size
                                      : 0),
             (unsigned long long)(state.owned_resources.gpu_start >= 0
                                      ? state.owned_resources.gpu_size
                                      : 0),
             (unsigned long long)(state.owned_resources.cpu_gpu_start >= 0
                                      ? state.owned_resources.cpu_gpu_size
                                      : 0),
             (unsigned long long)(state.owned_frame_start >= 0 ? state.owned_frame_bytes : 0),
             (unsigned long long)life.first_ready_us, (unsigned long long)state.first_decoded_us,
             (unsigned long long)state.first_present_us);
    ok = ok &&
         report_append(
             report, sizeof(report), &length,
             "\"process_cpu_mask\":%llu,\"receive_cpu_mask\":%llu,\"decode_cpu_mask\":%llu,"
             "\"present_cpu_mask\":%llu,\"other_cpu_mask\":%llu,\"placement_applied\":%u,"
             "\"placement_failed\":%u,\"receive_placement_verified\":%llu,"
             "\"decode_placement_result\":%d,\"present_placement_result\":%d,"
             "\"main_placement_result\":%d,\"input_poll_us\":%d,\n",
             (unsigned long long)state.process_cpu_mask, (unsigned long long)state.layout.receive,
             (unsigned long long)state.layout.decode, (unsigned long long)state.layout.present,
             (unsigned long long)state.layout.other, placement.applied, placement.failed,
             (unsigned long long)placement.receive_verified, state.decode_placement_result,
             state.present_placement_result, state.main_placement_result, INPUT_POLL_US);
    ok = ok &&
         report_append(
             report, sizeof(report), &length,
             "\"vsync_requested\":%u,\"vsync_active\":%d,\"vsync_fallbacks\":%llu,"
             "\"flip_events_active\":%d,\"flip_event_wakeups\":%llu,\"flip_event_errors\":%llu,"
             "\"flip_queries\":%llu,\"flip_sleeps\":%llu,\"flip_timeouts\":%llu,"
             "\"present_errors\":%u,\"gpu_timestamps\":%d,\"gpu_samples_invalid\":%llu,\n",
             state.vsync_requested, native_agc_vsync_active(),
             (unsigned long long)agc.vsync_fallbacks, native_agc_flip_events_active(),
             (unsigned long long)agc.flip_event_wakeups, (unsigned long long)agc.flip_event_errors,
             (unsigned long long)agc.flip_queries, (unsigned long long)agc.flip_sleeps,
             (unsigned long long)agc.flip_timeouts, state.present_errors,
             PROSPEROLIGHT_GPU_TIMESTAMPS, (unsigned long long)agc.gpu_samples_invalid);
    ok = ok && report_append(report, sizeof(report), &length,
                             "\"controllers_peak\":%u,\"controller_arrivals\":%u,"
                             "\"controller_removals\":%u,\"controller_open_errors\":%u,"
                             "\"controller_send_errors\":%u,\"user_scan_errors\":%u,\n",
                             controller_summary.peak, controller_summary.arrivals,
                             controller_summary.removals, controller_summary.open_errors,
                             controller_summary.send_errors, controller_summary.scan_errors);
    ok = ok &&
         report_append(
             report, sizeof(report), &length,
             "\"audio_pending_ms_high_water\":%u,\"audio_ring_frames_high_water\":%u,"
             "\"audio_catchup_packets\":%u,\"audio_catchup_frames\":%llu,"
             "\"audio_decode_errors\":%u,\"audio_output_errors\":%u,"
             "\"audio_ring_overruns\":%u,"
             "\"udp_packets\":%llu,\"udp_bytes\":%llu,\"udp_receive_errors\":%llu,"
             "\"socket_poll_calls\":%llu,\"socket_heap_polls\":%llu,"
             "\"rcvbuf_requests\":%llu,\"rcvbuf_failures\":%llu,"
             "\"last_rcvbuf_requested\":%llu,\"last_rcvbuf_actual\":%llu,\"timings_us\":{\n",
             audio_state.pending_ms_high_water, audio_state.ring_high_water,
             audio_state.catchup_packets, (unsigned long long)audio_state.catchup_frames,
             audio_state.decode_errors, audio_state.output_errors, audio_state.overruns,
             (unsigned long long)network.packets, (unsigned long long)network.bytes,
             (unsigned long long)network.receive_errors, (unsigned long long)network.poll_calls,
             (unsigned long long)network.heap_polls, (unsigned long long)network.buffer_requests,
             (unsigned long long)network.buffer_failures,
             (unsigned long long)network.last_buffer_requested,
             (unsigned long long)network.last_buffer_actual);
    if (!ok)
        return;
    const struct
    {
        const char *name;
        const moonlight::TimingHistogram *timing;
    } timings[] = {{"copy", &state.copy_timing},
                   {"decode", &state.decode_timing},
                   {"receive_to_enqueue", &state.reassembly_timing},
                   {"enqueue_to_callback", &state.queue_timing},
                   {"callback_to_ready", &state.ready_timing},
                   {"enqueue_to_flip_observed", &state.frame_age_timing},
                   {"flip_observed_interval", &state.flip_interval_timing},
                   {"input_poll_interval", &input_intervals},
                   {"audio_decode", &audio_state.decode_timing},
                   {"audio_output", &audio_state.output_timing},
                   {"ready_to_present", &state.present_wait_timing},
                   {"flush", &state.flush_timing},
                   {"present_call", &state.present_call_timing},
                   {"completion_wait", &state.completion_wait_timing},
                   {"host_processing", &state.host_timing},
                   {"agc_prepare", &agc.prepare},
                   {"agc_cache_flush", &agc.cache_flush},
                   {"agc_submit", &agc.submit},
                   {"overlay_refresh", &agc.overlay},
                   {"gpu_render", &agc.gpu_render}};
    for (size_t i = 0; i < sizeof(timings) / sizeof(timings[0]); ++i)
    {
        const auto &t = *timings[i].timing;
        if (!report_append(report, sizeof(report), &length,
                           "%s\"%s\":{\"count\":%llu,\"mean\":%llu,\"p95_upper\":%llu,"
                           "\"p99_upper\":%llu,\"max\":%llu}\n",
                           i ? "," : "", timings[i].name, (unsigned long long)t.count,
                           (unsigned long long)(t.count ? t.total_us / t.count : 0),
                           (unsigned long long)t.percentile(95),
                           (unsigned long long)t.percentile(99), (unsigned long long)t.max_us))
            return;
    }
    if (length + 4 >= sizeof(report))
        return;
    memcpy(report + length, "}}\n", 3);
    length += 3;
    log_performance_summary(report, length);
    if (!prosperolight_logs_enabled())
        return;
    char temporary[176];
    char destination[176];
    snprintf(temporary, sizeof(temporary), "%s/performance-last.json.tmp",
             storage::paths().performance);
    snprintf(destination, sizeof(destination), "%s/performance-last.json",
             storage::paths().performance);
    const int descriptor = sceKernelOpen(temporary, 0x601, 0644);
    if (descriptor < 0)
        return;
    const bool written = write_performance_bytes(descriptor, report, length);
    const int closed = sceKernelClose(descriptor);
    if (written && closed == 0)
        (void)sceKernelRename(temporary, destination);
}

static int ps5_controller_disconnect_only(ps5_controller_state_t *state)
{
    int count;

    if (!state || state->handle < 0)
        return 0;
    count = scePadRead(state->handle, state->sample_batch, PS5_PAD_SAMPLE_CAPACITY);
    if (count <= 0)
        return 0;
    for (int index = 0; index < count; ++index)
    {
        const ps5_pad_sample_t *sample = &state->sample_batch[index];

        if (sample->connected && !(sample->buttons & PS5_PAD_BUTTON_INTERCEPTED) &&
            moonlight_stream_disconnect_requested(sample->buttons))
            return 1;
    }
    return 0;
}

typedef struct connection_loading_state
{
    ps5_controller_state_t *controller;
    std::atomic<int> active;
    std::atomic<int> cancel_requested;
    std::atomic<int> timed_out;
    std::atomic<int> connection_pending;
    uint64_t started_us;
    pthread_t thread;
    int thread_started;
    int create_result;
} connection_loading_state_t;

static std::atomic<connection_loading_state_t *> active_connection_loading;
static uint64_t monotonic_us(void);

static void *connection_loading_thread(void *context)
{
    auto *state = static_cast<connection_loading_state_t *>(context);

    while (std::atomic_load_explicit(&state->active, std::memory_order_relaxed))
    {
        if (ps5_controller_disconnect_only(state->controller))
            state->controller->requested_stop = 1;
        if (state->controller && state->controller->requested_stop)
            std::atomic_store_explicit(&state->cancel_requested, 1, std::memory_order_relaxed);
        if (monotonic_us() - state->started_us >= CONNECTION_SETUP_TIMEOUT_US)
        {
            std::atomic_store_explicit(&state->timed_out, 1, std::memory_order_relaxed);
            std::atomic_store_explicit(&state->cancel_requested, 1, std::memory_order_relaxed);
        }
        if (std::atomic_load_explicit(&state->cancel_requested, std::memory_order_relaxed))
        {
            http_interrupt();
            if (std::atomic_load_explicit(&state->connection_pending, std::memory_order_acquire))
            {
                LiInterruptConnection();
                std::atomic_store_explicit(&state->active, 0, std::memory_order_relaxed);
                break;
            }
        }
        sceKernelUsleep(10000);
    }
    return NULL;
}

static int start_connection_loading(connection_loading_state_t *state,
                                    ps5_controller_state_t *controller)
{
    state->controller = controller;
    state->started_us = monotonic_us();
    std::atomic_store_explicit(&state->active, 1, std::memory_order_relaxed);
    std::atomic_store_explicit(&state->cancel_requested, 0, std::memory_order_relaxed);
    std::atomic_store_explicit(&state->timed_out, 0, std::memory_order_relaxed);
    std::atomic_store_explicit(&state->connection_pending, 0, std::memory_order_relaxed);
    std::atomic_store_explicit(&active_connection_loading, state, std::memory_order_release);
    state->create_result = pthread_create(&state->thread, NULL, connection_loading_thread, state);
    if (state->create_result == 0)
        state->thread_started = 1;
    else
        std::atomic_store_explicit(&state->active, 0, std::memory_order_relaxed);
    return state->create_result;
}

static void stop_connection_loading(void)
{
    connection_loading_state_t *state =
        std::atomic_exchange_explicit(&active_connection_loading, NULL, std::memory_order_acq_rel);

    if (!state)
        return;
    std::atomic_store_explicit(&state->active, 0, std::memory_order_relaxed);
    if (state->thread_started)
    {
        (void)pthread_join(state->thread, NULL);
        state->thread_started = 0;
    }
}

static size_t align_16k(size_t value)
{
    return (value + 0x3fff) & ~(size_t)0x3fff;
}

static uint64_t monotonic_us(void)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * UINT64_C(1000000) + (uint64_t)now.tv_nsec / UINT64_C(1000);
}

static void renderer_sync_init(native_renderer_state_t *state)
{
    if (state->sync_ready)
        return;
    (void)pthread_mutex_init(&state->lock, NULL);
    (void)pthread_cond_init(&state->wake, NULL);
    state->sync_ready = true;
}

static void renderer_sync_destroy(native_renderer_state_t *state)
{
    if (!state->sync_ready)
        return;
    (void)pthread_cond_destroy(&state->wake);
    (void)pthread_mutex_destroy(&state->lock);
    state->sync_ready = false;
}

static void *frame_slot_address(const native_renderer_state_t *state, int slot)
{
    return static_cast<uint8_t *>(state->frame_memory) +
           static_cast<size_t>(slot) * state->frame_size;
}

static int frame_slot_index(const native_renderer_state_t *state, const void *buffer)
{
    const auto *base = static_cast<const uint8_t *>(state->frame_memory);
    const auto *address = static_cast<const uint8_t *>(buffer);

    if (!buffer || !base || !state->frame_size || address < base)
        return -1;
    const size_t offset = static_cast<size_t>(address - base);
    if (offset % state->frame_size || offset / state->frame_size >= FRAME_SLOT_COUNT)
        return -1;
    return static_cast<int>(offset / state->frame_size);
}

static void fail_stream(int32_t code)
{
    connection_error = code;
    connection_terminated = 1;
}

static int32_t allocate_direct(size_t size, int protection, int64_t limit, int64_t *start,
                               void **address)
{
    int32_t result = sceKernelAllocateDirectMemory(0, limit, size, 0x4000, 12, start);
    if (result == 0)
        result = sceKernelMapDirectMemory(address, size, protection, 0, *start, 0x4000);
    return result;
}

static void release_direct(void *address, int64_t start, size_t size)
{
    if (address)
        (void)sceKernelMunmap(address, size);
    if (start >= 0)
        (void)sceKernelReleaseDirectMemory(start, size);
}

// ---- the connecting screen ---------------------------------------------------
// The launcher hands over a picture of its connecting screen. The stream keeps
// it on the television, its bar still moving, until the first picture of the
// stream is ready; then the bar runs to its end and the screen fades out.
typedef struct connecting_screen_state
{
    connecting::Plate plate;
    void *surface;
    int64_t surface_start;
    size_t surface_size;
    int hdr;
    uint32_t output_source_width;
    uint32_t output_source_height;
    uint32_t requested_fps;
    uint32_t output_refresh_x100;
    float progress;
    uint64_t started_us;
    // The bar is at least at the floor, and creeps towards the ceiling while
    // the step it stands for is still running.
    std::atomic<uint32_t> floor_x1000;
    std::atomic<uint32_t> ceiling_x1000;
    std::atomic<int> active;
    std::atomic<int> finish;
    pthread_t thread;
    int thread_started;
    int present_result;
} connecting_screen_state_t;

static connecting_screen_state_t connecting_screen;

static int connecting_screen_present(connecting_screen_state_t *state)
{
    return native_agc_present_still(state->surface, connecting::surface_bytes(state->hdr != 0),
                                    state->hdr, state->output_source_width,
                                    state->output_source_height, state->requested_fps);
}

static int connecting_screen_show(connecting_screen_state_t *state, float progress,
                                  float brightness)
{
    state->plate.compose(state->surface, progress, brightness);
    native_agc_flush_source(state->surface, connecting::surface_bytes(state->hdr != 0));
    return connecting_screen_present(state);
}

static int connecting_screen_show_bar(connecting_screen_state_t *state, float progress)
{
    connecting::Range written[2];

    state->plate.compose_bar(state->surface, progress, written);
    for (const connecting::Range &range : written)
        native_agc_flush_source(static_cast<const uint8_t *>(state->surface) + range.offset,
                                range.bytes);
    return connecting_screen_present(state);
}

static void *connecting_screen_thread(void *context)
{
    auto *state = static_cast<connecting_screen_state_t *>(context);
    const bool has_bar = state->plate.has_bar();
    uint64_t last_us = monotonic_us();
    float progress = state->progress;

    while (std::atomic_load_explicit(&state->active, std::memory_order_relaxed) &&
           !std::atomic_load_explicit(&state->finish, std::memory_order_acquire))
    {
        if (!has_bar)
        {
            // A black screen: there is nothing to move.
            sceKernelUsleep(10000);
            continue;
        }
        const uint64_t now_us = monotonic_us();
        const float elapsed = static_cast<float>(now_us - last_us) / 1e6f;
        const float dt = elapsed < 0.1f ? elapsed : 0.1f;
        const float floor = static_cast<float>(std::atomic_load_explicit(
                                &state->floor_x1000, std::memory_order_relaxed)) /
                            1000.0f;
        const float ceiling = static_cast<float>(std::atomic_load_explicit(
                                  &state->ceiling_x1000, std::memory_order_relaxed)) /
                              1000.0f;
        last_us = now_us;
        if (progress < floor)
            progress += (floor - progress) * (1.0f - expf(-dt / 0.12f));
        if (progress < ceiling)
            progress += (ceiling - progress) * (1.0f - expf(-dt / 2.5f));
        // Each call returns at the television's next refresh.
        state->present_result = connecting_screen_show_bar(state, progress);
        if (state->present_result != 0)
            return NULL;
    }
    if (!has_bar || !std::atomic_load_explicit(&state->active, std::memory_order_relaxed))
        return NULL;

    // The first picture is ready: the bar runs to its end, then the screen
    // fades to black and the stream takes its place.
    const float from = progress;
    uint64_t started_us = monotonic_us();
    for (;;)
    {
        const float t = static_cast<float>(monotonic_us() - started_us) / 220000.0f;
        const float eased = t >= 1.0f ? 1.0f : 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
        state->present_result = connecting_screen_show_bar(state, from + (1.0f - from) * eased);
        if (state->present_result != 0 || t >= 1.0f)
            break;
    }
    if (state->present_result == 0)
        sceKernelUsleep(70000);
    started_us = monotonic_us();
    while (state->present_result == 0)
    {
        const float t = static_cast<float>(monotonic_us() - started_us) / 260000.0f;
        const float shade = t >= 1.0f ? 1.0f : t * t * (3.0f - 2.0f * t);
        state->present_result = connecting_screen_show(state, 1.0f, 1.0f - shade);
        if (t >= 1.0f)
            break;
    }
    return NULL;
}

// Sets how far the bar is: at least floor, creeping towards ceiling.
static void connecting_screen_stage(float floor, float ceiling)
{
    std::atomic_store_explicit(&connecting_screen.floor_x1000,
                               static_cast<uint32_t>(floor * 1000.0f), std::memory_order_relaxed);
    std::atomic_store_explicit(&connecting_screen.ceiling_x1000,
                               static_cast<uint32_t>(ceiling * 1000.0f), std::memory_order_relaxed);
}

// Puts the picture on the television, from the caller's thread, and starts
// the thread that keeps its bar moving. Without a picture the screen is black.
static int connecting_screen_begin(const moonlight_connecting_picture_t *picture, int hdr,
                                   uint32_t output_source_width, uint32_t output_source_height,
                                   uint32_t requested_fps, int64_t direct_memory_limit)
{
    connecting_screen_state_t *state = &connecting_screen;
    connecting::Bar bar;
    const uint8_t *rgba = picture ? picture->rgba : NULL;
    int32_t result;

    if (rgba)
    {
        bar.x = picture->bar_x;
        bar.y = picture->bar_y;
        bar.width = picture->bar_width;
        bar.height = picture->bar_height;
        memcpy(bar.fill, picture->fill, sizeof(bar.fill));
    }
    state->plate.build(rgba, bar, hdr != 0);
    state->hdr = hdr;
    state->output_source_width = output_source_width;
    state->output_source_height = output_source_height;
    state->requested_fps = requested_fps;
    state->output_refresh_x100 = 0;
    state->progress = rgba ? picture->progress : 0.0f;
    state->started_us = monotonic_us();
    state->thread_started = 0;
    state->present_result = -1;
    state->surface = NULL;
    state->surface_start = -1;
    state->surface_size = align_16k(connecting::surface_bytes(hdr != 0));
    std::atomic_store_explicit(&state->active, 0, std::memory_order_relaxed);
    std::atomic_store_explicit(&state->finish, 0, std::memory_order_relaxed);
    connecting_screen_stage(state->progress, state->progress);

    result = allocate_direct(state->surface_size, 0x32, direct_memory_limit, &state->surface_start,
                             &state->surface);
    if (result != 0)
        return result;
    state->present_result = connecting_screen_show(state, state->progress, 1.0f);
    if (state->present_result != 0)
    {
        (void)native_agc_present_shutdown();
        return state->present_result;
    }
    // The refresh rate the console really got, read on the thread that
    // presented, before the bar's thread starts presenting.
    {
        uint32_t width = 0, height = 0;
        native_agc_output_status(&width, &height, &state->output_refresh_x100);
    }
    std::atomic_store_explicit(&state->active, 1, std::memory_order_relaxed);
    if (pthread_create(&state->thread, NULL, connecting_screen_thread, state) == 0)
        state->thread_started = 1;
    else
        std::atomic_store_explicit(&state->active, 0, std::memory_order_relaxed);
    return 0;
}

// The first picture of the stream is about to be shown: let the bar finish
// and the screen fade, and wait for that. Called by the presenting thread.
static void connecting_screen_finish(void)
{
    connecting_screen_state_t *state = &connecting_screen;

    if (!state->thread_started)
        return;
    std::atomic_store_explicit(&state->finish, 1, std::memory_order_release);
    (void)pthread_join(state->thread, NULL);
    state->thread_started = 0;
    printf("[PL] stream: first picture %llu ms after the connecting screen, present=%08x\n",
           (unsigned long long)((monotonic_us() - state->started_us) / 1000u),
           (uint32_t)state->present_result);
}

// The stream ended before its first picture: stop where the bar is.
static void connecting_screen_stop(void)
{
    connecting_screen_state_t *state = &connecting_screen;

    std::atomic_store_explicit(&state->active, 0, std::memory_order_relaxed);
    if (state->thread_started)
    {
        (void)pthread_join(state->thread, NULL);
        state->thread_started = 0;
    }
}

// After the presenter has shut down: nothing reads the picture any more.
static void connecting_screen_release(void)
{
    connecting_screen_state_t *state = &connecting_screen;

    release_direct(state->surface, state->surface_start, state->surface_size);
    state->surface = NULL;
    state->surface_start = -1;
    state->plate.clear();
}

static decoder_resources_t make_decoder_resources()
{
    decoder_resources_t resources{};

    resources.gpu_start = -1;
    resources.cpu_gpu_start = -1;
    return resources;
}

static void release_decoder_resources(decoder_resources_t *resources)
{
    release_direct(resources->memory.cpu_gpu, resources->cpu_gpu_start, resources->cpu_gpu_size);
    release_direct(resources->memory.gpu, resources->gpu_start, resources->gpu_size);
    if (resources->memory.cpu)
    {
        (void)sceKernelReleaseFlexibleMemory(resources->memory.cpu, resources->cpu_mapping_size);
        (void)sceKernelMunmap(resources->memory.cpu, resources->cpu_mapping_size);
    }
    *resources = make_decoder_resources();
}

// One attempt at a decoder configuration. On failure the caller releases the
// attempt's memory and may try a more conservative configuration.
static int32_t create_stream_decoder(decoder_resources_t *resources,
                                     const videodec2_decoder_config_t *config, int64_t limit,
                                     const std::atomic<int> *running = nullptr,
                                     bool *retryable = nullptr)
{
    videodec2_decoder_memory_t *memory = &resources->memory;
    uint8_t mapping_info[0x48] = {};
    size_t flexible_available = 0;
    int32_t result;

    if (retryable)
        *retryable = true;
    if (running && !running->load())
        return -34;
    memset(memory, 0, sizeof(*memory));
    memory->size = sizeof(*memory);
    result = sceVideodec2QueryDecoderMemoryInfo(config, memory);
    snprintf(notification.message, sizeof(notification.message),
             "Native embedded query: rc=%08x depth=%u affinity=%llx cpu=%llx gpu=%llx shared=%llx "
             "frame=%llx align=%x",
             (uint32_t)result, config->pipeline_depth, (unsigned long long)config->cpu_affinity,
             (unsigned long long)memory->cpu_size, (unsigned long long)memory->gpu_size,
             (unsigned long long)memory->cpu_gpu_size, (unsigned long long)memory->max_frame_size,
             memory->frame_alignment);
    (void)lan_http_report_text(notification.message);
    if (result != 0)
        return result;
    if (retryable)
        *retryable = false;
    // Bound API-reported workspaces before alignment/multiplication. This is a
    // resource policy, not a claim about native hardware DPB limits.
    constexpr uint64_t workspace_limit = UINT64_C(1024) * 1024 * 1024;
    if (!memory->cpu_size || !memory->gpu_size || !memory->max_frame_size ||
        memory->cpu_size > workspace_limit || memory->gpu_size > workspace_limit ||
        memory->cpu_gpu_size > workspace_limit || memory->max_frame_size > 64u * 1024u * 1024u)
        return -33;

    if (running && !running->load())
        return -34;
    resources->cpu_mapping_size = align_16k((size_t)memory->cpu_size);
    result = sceKernelAvailableFlexibleMemorySize(&flexible_available);
    if (result == 0)
        result = sceKernelMapNamedFlexibleMemory(&memory->cpu, resources->cpu_mapping_size, 0x03, 0,
                                                 "MoonlightVdecCpu");
    snprintf(
        notification.message, sizeof(notification.message),
        "Native flexible CPU workspace: rc=%08x available=%zx requested=%llx mapped=%zx ptr=%p",
        (uint32_t)result, flexible_available, (unsigned long long)memory->cpu_size,
        resources->cpu_mapping_size, memory->cpu);
    (void)lan_http_report_text(notification.message);
    if (result != 0)
    {
        memory->cpu = NULL;
        return result;
    }

    if (running && !running->load())
        return -34;
    result = sceKernelVirtualQuery(memory->cpu, 0, mapping_info, sizeof(mapping_info));
    snprintf(notification.message, sizeof(notification.message),
             "Native flexible CPU query: rc=%08x protection=%x type=%x flags=%x", (uint32_t)result,
             *(const uint32_t *)(mapping_info + 0x18), *(const uint32_t *)(mapping_info + 0x1c),
             *(const uint32_t *)(mapping_info + 0x20));
    (void)lan_http_report_text(notification.message);
    if (result != 0)
        return result;

    if (running && !running->load())
        return -34;
    resources->gpu_size = align_16k((size_t)memory->gpu_size);
    resources->cpu_gpu_size = align_16k((size_t)memory->cpu_gpu_size);
    memory->gpu_size = resources->gpu_size;
    if (resources->cpu_gpu_size != 0)
        memory->cpu_gpu_size = resources->cpu_gpu_size;
    result = allocate_direct(resources->gpu_size, 0x32, limit, &resources->gpu_start, &memory->gpu);
    if (result == 0 && running && !running->load())
        return -34;
    if (result == 0 && resources->cpu_gpu_size != 0)
        result = allocate_direct(resources->cpu_gpu_size, 0x33, limit, &resources->cpu_gpu_start,
                                 &memory->cpu_gpu);
    if (result == 0 && running && !running->load())
        return -34;
    if (result == 0)
    {
        if (retryable)
            *retryable = true;
        result = sceVideodec2CreateDecoder(config, memory, &resources->decoder);
    }
    snprintf(notification.message, sizeof(notification.message),
             "Native zero-copy stage 4: create=%08x decoder=%p depth=%u affinity=%llx gpu=%p/%zx "
             "shared=%p/%zx",
             (uint32_t)result, resources->decoder, config->pipeline_depth,
             (unsigned long long)config->cpu_affinity, memory->gpu, resources->gpu_size,
             memory->cpu_gpu, resources->cpu_gpu_size);
    (void)lan_http_report_text(notification.message);
    if (result != 0)
        resources->decoder = NULL;
    return result;
}

static int moonlight_renderer_setup(int video_format, int width, int height, int redraw_rate,
                                    void *context, int dr_flags);

// After a decoder reset every picture it still held is abandoned.
static void abandon_decoder_pictures(native_renderer_state_t *state)
{
    pthread_mutex_lock(&state->lock);
    state->frames.release_decoding();
    pthread_mutex_unlock(&state->lock);
    for (uint32_t i = 0; i < state->submission_count; ++i)
    {
        auto *trace =
            state->submissions[(state->submission_head + i) % SUBMISSION_QUEUE_CAPACITY].trace;
        if (trace)
            trace->outcome = 3;
    }
    state->submission_head = state->submission_count = 0;
}

static void snapshot_hud_metrics(const native_renderer_state_t *state, int pending,
                                 native_agc_metrics_t *hud)
{
    const uint64_t observed =
        (uint64_t)state->access_units + state->drops.network + state->drops.decoder;

    *hud = {};
    hud->video_codec = state->mode->codec_preference;
    hud->incoming_fps_x100 = state->arrival.fps_x100;
    hud->decoded_fps_x100 = state->decoded_rate.fps_x100;
    hud->network_drop_percent_x100 =
        observed ? (uint32_t)(state->drops.network * UINT64_C(10000) / observed) : 0;
    hud->decoder_drop_percent_x100 =
        observed ? (uint32_t)(state->drops.decoder * UINT64_C(10000) / observed) : 0;
    hud->not_displayed = state->not_displayed;
    hud->rtt_ms = state->rtt_ms;
    hud->rtt_variance_ms = state->rtt_variance_ms;
    hud->rtt_valid = state->rtt_valid ? 1u : 0u;
    hud->host_min_tenths_ms = state->host_latency_min_tenths_ms;
    hud->host_max_tenths_ms = state->host_latency_max_tenths_ms;
    hud->host_average_tenths_ms =
        state->host_latency_frames
            ? (uint32_t)(state->host_latency_total_tenths_ms / state->host_latency_frames)
            : 0;
    hud->decoder_load_permille = state->decode_window.last_load_permille;
    hud->decode_average_us = state->decode_window.last_mean_us;
    hud->decode_p95_us = state->decode_window.last_p95_us;
    hud->queue_delay_average_us = state->queue_window.last_mean_us;
    hud->queue_delay_max_us = state->queue_window.last_max_us;
    hud->pending_frames = pending > 0 ? (uint32_t)pending : 0;
    hud->slices_requested = state->slices_requested;
    hud->slices_observed = state->observed_slices_last;
    hud->pipeline_depth = state->pipeline_depth;
    hud->decoder_cores = state->decoder_cores;
    hud->vsync_enabled = state->vsync_requested;
    hud->decoder_cpu_mask = state->decoder_cpu_mask;
}

// Unpaced replaces the pending picture; paced modes retain two ready
// pictures. Capacity overflow retires the oldest surface without blocking decode.
static void publish_ready_frame(native_renderer_state_t *state, const stream_ready_frame_t &ready)
{
    stream_ready_frame_t displaced{};

    pthread_mutex_lock(&state->lock);
    state->frames.state[static_cast<size_t>(ready.slot)] =
        moonlight::SlotPool<FRAME_SLOT_COUNT>::Ready;
    state->frames.last_output = ready.slot;
    const bool replaced = state->mailbox.publish(ready, &displaced);
    if (replaced)
    {
        state->frames.release(displaced.slot);
        if (state->pacing_decisions)
            state->pacing_decisions->record(displaced.frame, displaced.pts_us, monotonic_us(), 0,
                                            "drop-capacity");
    }
    pthread_cond_signal(&state->wake);
    pthread_mutex_unlock(&state->lock);
    if (replaced)
    {
        ++state->not_displayed;
        if (displaced.trace)
            displaced.trace->outcome = 2;
    }
}

static void report_decoder_call(const native_renderer_state_t *state, const char *phase,
                                int32_t result, int32_t frame_number, int offered,
                                const videodec2_frame_t &frame, const videodec2_output_t &output)
{
    char receipt[512];

    snprintf(receipt, sizeof(receipt),
             "Moonlight decoder error: phase=%s rc=%08x frame=%d au=%u depth=%u in_flight=%u "
             "offered=%d accepted=%u valid=%u error=%u pictures=%u output=%ux%u pitch=%u "
             "pitch_bytes=%u buffer=%llx out_slot=%d",
             phase, (uint32_t)result, frame_number, state->access_units, state->pipeline_depth,
             state->submission_count, offered, frame.accepted, output.valid, output.error,
             output.picture_count, output.width, output.height, output.pitch, output.pitch_bytes,
             (unsigned long long)output.buffer_size, frame_slot_index(state, output.buffer));
    (void)lan_http_report_text(receipt);
}

// Accounts for one Decode or Flush call. `offered` is the free slot passed to
// the call. Videodec2 may write the picture into that slot, or keep it and
// return a slot it accepted with an earlier access unit; both are handled.
static int32_t settle_decoder_call(native_renderer_state_t *state, int offered,
                                   const videodec2_frame_t &frame, const videodec2_output_t &output,
                                   uint64_t completed_us, int pending)
{
    using Pool = moonlight::SlotPool<FRAME_SLOT_COUNT>;
    const native_video_mode_t *mode = state->mode;
    int output_slot = -1;

    if (output.valid)
    {
        output_slot = frame_slot_index(state, output.buffer);
        pthread_mutex_lock(&state->lock);
        const bool owned =
            output_slot >= 0 &&
            (output_slot == offered ||
             state->frames.state[static_cast<size_t>(output_slot)] == Pool::Decoding);
        pthread_mutex_unlock(&state->lock);
        if (!owned || !state->submission_count || output.codec != mode->codec_type ||
            output.width != mode->output_width ||
            (output.height != mode->output_height &&
             (!mode->alternate_output_height || output.height != mode->alternate_output_height)) ||
            output.pitch != mode->output_pitch || output.picture_count != 1)
            return -17;
        if (mode->hdr && (output.pitch_bytes != mode->output_pitch * sizeof(uint16_t) ||
                          output.buffer_size == 0 || output.buffer_size > state->frame_size))
            return -19;
    }
    if (frame.accepted && offered != output_slot)
    {
        pthread_mutex_lock(&state->lock);
        state->frames.state[static_cast<size_t>(offered)] = Pool::Decoding;
        pthread_mutex_unlock(&state->lock);
    }
    if (output.valid)
    {
        const stream_submission_t submission = state->submissions[state->submission_head];
        stream_ready_frame_t ready{};

        state->submission_head = (state->submission_head + 1u) % SUBMISSION_QUEUE_CAPACITY;
        --state->submission_count;
        const uint64_t elapsed =
            completed_us > submission.arrival_us ? completed_us - submission.arrival_us : 0;
        state->ready_timing.add(elapsed);
        state->callback_to_decode_total_us += elapsed;
        if (state->ready_calls == 0 || elapsed < state->callback_to_decode_min_us)
            state->callback_to_decode_min_us = elapsed;
        if (elapsed > state->callback_to_decode_max_us)
            state->callback_to_decode_max_us = elapsed;
        ++state->ready_calls;
        if (submission.trace)
            submission.trace->ready_us = elapsed;
        if (!state->decoded)
            state->first_decoded_us = completed_us;
        ++state->decoded;
        state->lifecycle.output(state->recovery_pending);
        state->recovery_pending = false;
        state->decoded_rate.update(completed_us, state->decoded);
        if (++state->clean_outputs >= PIPELINE_CLEAN_OUTPUTS)
            state->pipeline_faults = 0;
        ready.slot = output_slot;
        ready.buffer = output.buffer;
        ready.buffer_size = (size_t)output.buffer_size;
        ready.pitch = output.pitch;
        ready.height = output.height;
        ready.frame = submission.frame;
        ready.arrival_us = submission.arrival_us;
        ready.enqueue_us = submission.enqueue_us;
        ready.ready_us = completed_us;
        ready.pts_us = submission.pts_us;
        ready.trace = submission.trace;
        snapshot_hud_metrics(state, pending, &ready.hud);
        publish_ready_frame(state, ready);
    }
    pthread_mutex_lock(&state->lock);
    const unsigned held = state->frames.count(Pool::Decoding);
    pthread_mutex_unlock(&state->lock);
    // A slot the decoder holds always belongs to an access unit still in flight.
    return held > state->submission_count ? -21 : 0;
}

static void *video_present_thread(void *context);

static void lifecycle_failure(native_renderer_state_t *state, moonlight::video::Error reason,
                              int32_t code)
{
    // A connection-level retry belongs to the session owner, never this worker.
    // gs_start_app() is not an ownership-safe transport reconnect (see design).
    state->lifecycle.fail(reason);
    state->last_result = code;
    state->running = 0;
    char line[192];
    snprintf(line, sizeof(line), "Native decoder failed: reason=%u code=%08x generation=%u",
             unsigned(reason), uint32_t(code), state->lifecycle.generation);
    (void)lan_http_report_text(line);
    fail_stream(code);
}

// Decode is serialized by its sole owner. Join presentation before invalidating
// any borrowed frame/trace, then retire GPU reads before deleting native memory.
static bool retire_stream_decoder(native_renderer_state_t *state)
{
    if (state->present_started)
    {
        pthread_mutex_lock(&state->lock);
        state->stop_presenting = true;
        pthread_cond_broadcast(&state->wake);
        pthread_mutex_unlock(&state->lock);
        (void)pthread_join(state->present_thread, nullptr);
        state->present_started = false;
    }
    // Recovery can precede the first output, while the loading worker still
    // owns AGC. Join it as well before retiring shared native submissions.
    connecting_screen_stop();
    const int idle = native_agc_finish_frame();
    if (idle != 0)
    {
        lifecycle_failure(state, moonlight::video::Error::Presentation, idle);
        return false;
    }
    if (state->decoder)
    {
        const int deleted = sceVideodec2DeleteDecoder(state->decoder);
        if (deleted != 0)
        {
            lifecycle_failure(state, moonlight::video::Error::Reset, deleted);
            return false;
        }
        state->decoder = nullptr;
    }
    abandon_decoder_pictures(state);
    pthread_mutex_lock(&state->lock);
    stream_ready_frame_t discarded{};
    while (state->mailbox.take(&discarded))
    {
        if (discarded.trace)
            discarded.trace->outcome = 3;
        ++state->not_displayed;
    }
    state->frames = {};
    state->source_clock = {};
    pthread_mutex_unlock(&state->lock);
    release_direct(state->frame_memory, state->owned_frame_start, state->owned_frame_bytes);
    release_direct(state->input_memory, state->owned_input_start, state->owned_input_bytes);
    state->frame_memory = state->input_memory = nullptr;
    state->owned_frame_start = state->owned_input_start = -1;
    release_decoder_resources(&state->owned_resources);
    return true;
}

static bool configure_stream_decoder(native_renderer_state_t *state,
                                     const moonlight::video::Requirements &requirements,
                                     moonlight::video::Plan plan, uint32_t restore_depth = 0)
{
    using namespace moonlight::video;
    const uint64_t started = monotonic_us();
    const uint64_t preferred_affinity =
        restore_depth ? state->decoder_cpu_mask : state->requested_cpu_mask;
    state->policy_keyframe_wait = false;
    const bool replacement = state->decoder != nullptr;
    const bool recovering = state->lifecycle.state == State::Recovering;
    state->recovery_pending = recovering;
    state->lifecycle.state = recovering    ? State::Recovering
                             : replacement ? State::Reconfiguring
                                           : State::Configuring;
    if (replacement && !retire_stream_decoder(state))
        return false;
    if (!state->running)
        return false;
    auto config = state->decoder_config;
    config.max_width = requirements.width;
    config.max_height = requirements.height;
    config.max_level = requirements.level;
    config.max_dpb_frames = plan.dpb;
    // The currently supported native profiles use H.264 profile_idc or HEVC profile_idc.
    config.profile = requirements.profile;
    int32_t result = -1;
    const uint32_t depths[]{plan.depth, restore_depth};
    // An optional performance trial may fail after retirement. Make one bounded
    // attempt to restore the original accepted depth, using the same owner path.
    for (unsigned phase = 0;
         phase < (restore_depth ? 2u : 1u) && !state->owned_resources.decoder && state->running;
         ++phase)
    {
        bool restore = false;
        for (uint32_t depth = depths[phase];
             depth && !restore && !state->owned_resources.decoder && state->running; --depth)
            for (unsigned attempt = 0;
                 attempt < 2 && !state->owned_resources.decoder && state->running; ++attempt)
            {
                const uint64_t affinity =
                    attempt ? moonlight::kClassicDecoderCpuMask : preferred_affinity;
                if (attempt && affinity == preferred_affinity)
                    continue;
                config.pipeline_depth = depth;
                config.cpu_affinity = affinity;
                ++state->create_attempts;
                bool retryable = false;
                result = create_stream_decoder(&state->owned_resources, &config,
                                               state->direct_limit, &state->running, &retryable);
                if (result != 0)
                {
                    release_decoder_resources(&state->owned_resources);
                    if (!retryable)
                    {
                        if (restore_depth && phase == 0)
                        {
                            restore = true;
                            break;
                        }
                        if (state->running)
                            lifecycle_failure(state, Error::Allocation, result);
                        return false;
                    }
                }
            }
    }
    if (!state->owned_resources.decoder)
    {
        if (state->running)
            lifecycle_failure(state, Error::Allocation, result);
        return false;
    }
    state->decoder = state->owned_resources.decoder;
    state->decoder_config = config;
    state->decoder_memory = state->owned_resources.memory;
    state->pipeline_depth = config.pipeline_depth;
    state->decoder_cpu_mask = config.cpu_affinity;
    state->decoder_cores = __builtin_popcountll(config.cpu_affinity) / 2;
    const auto stable = moonlight::plan_thread_layout(
        state->process_cpu_mask, state->requested_cpu_mask | moonlight::kClassicDecoderCpuMask);
    state->layout = moonlight::plan_thread_layout(state->process_cpu_mask, config.cpu_affinity);
    state->layout.receive = stable.receive;
    state->layout.other = stable.other;
    state->decode_placement_result =
        state->layout.decode ? ps5_thread_affinity_set(state->layout.decode) : -1;
    if (state->decode_placement_result != 0)
    {
        lifecycle_failure(state, Error::Presentation, state->decode_placement_result);
        return false;
    }
    state->drain_enabled = config.pipeline_depth > 1;
    state->frame_size = align_16k(size_t(state->decoder_memory.max_frame_size));
    state->owned_input_bytes = state->input_size * INPUT_SLOT_COUNT;
    state->owned_frame_bytes = state->frame_size * FRAME_SLOT_COUNT;
    if (!state->running)
        return false;
    result = allocate_direct(state->owned_input_bytes, 0x32, state->direct_limit,
                             &state->owned_input_start, &state->input_memory);
    if (!result && state->running)
        result = allocate_direct(state->owned_frame_bytes, 0x32, state->direct_limit,
                                 &state->owned_frame_start, &state->frame_memory);
    if (!result && state->running)
        result = sceVideodec2Reset(state->decoder);
    if (!state->running)
        return false;
    if (result != 0)
    {
        lifecycle_failure(state, Error::Allocation, result);
        return false;
    }
    state->parameter_sets.forget_submitted();
    state->decoder_needs_reset = false;
    state->decoder_lost = false;
    state->pipeline_faults = 0;
    state->clean_outputs = 0;
    plan.depth = config.pipeline_depth;
    state->lifecycle.configured(requirements, plan, monotonic_us(), monotonic_us() - started);
    state->decoder_recreations = state->lifecycle.reconfigurations;
    if (recovering)
        state->lifecycle.state = State::Recovering;
    pthread_mutex_lock(&state->lock);
    state->stop_presenting = false;
    pthread_mutex_unlock(&state->lock);
    if (!state->present_started)
    {
        result = pthread_create(&state->present_thread, nullptr, video_present_thread, state);
        state->present_started = result == 0;
        if (result != 0)
        {
            lifecycle_failure(state, Error::Presentation, result);
            return false;
        }
    }
    char line[256];
    snprintf(line, sizeof(line),
             "Native decoder ready: generation=%u coded=%ux%u profile=%u level=%u stream_dpb=%u "
             "standard_dpb=%u capacity=%u depth=%u elapsed_us=%llu",
             state->lifecycle.generation, requirements.width, requirements.height,
             requirements.profile, requirements.level, requirements.buffering,
             requirements.standard_dpb, plan.dpb, plan.depth,
             (unsigned long long)(monotonic_us() - started));
    (void)lan_http_report_text(line);
    return true;
}

// A changed parameter advertisement is not an activation. PPS/slice IDs select
// the stream facts. New/reset instances require fresh matching SPS/PPS at an
// actual IDR boundary. Never replay cached raw parameter NALs.
static bool prepare_stream_decoder(native_renderer_state_t *state, PDECODE_UNIT unit)
{
    using namespace moonlight::video;
    const Codec codec = state->mode->codec_type == 1 ? Codec::H264 : Codec::Hevc;
    auto &sets = state->parameter_sets;
    unsigned fragments = 0;
    std::array<uint64_t, 32> supplied{};
    std::array<uint64_t, 256> supplied_pps{};
    sets.selected_pps = 256;
    sets.picture_type = UINT32_MAX;
    sets.supplied_sps = sets.supplied_pps = false;
    bool conflict = false;
    for (auto *entry = unit->bufferList; entry; entry = entry->next)
    {
        if (++fragments > 1024 || !entry->data || entry->length <= 0)
        {
            lifecycle_failure(state, Error::Parameters, -30);
            return false;
        }
        const auto *data = reinterpret_cast<const uint8_t *>(entry->data);
        if (entry->bufferType == BUFFER_TYPE_SPS)
        {
            Requirements r{};
            const auto parsed = parse_sps(codec, data, entry->length, r);
            if (parsed == ParseResult::Unsupported && unit->frameType != FRAME_TYPE_IDR)
            {
                if (state->policy_keyframe_wait)
                {
                    if (state->stream_policy.pending_depth)
                        state->stream_policy.reject();
                    state->policy_keyframe_wait = false;
                    state->transition_wait_since = 0;
                }
                if (!state->transition_wait_since)
                {
                    state->transition_wait_since = monotonic_us();
                    ++state->parameter_transition_waits;
                }
                return false;
            }
            if (parsed != ParseResult::Valid)
            {
                lifecycle_failure(state,
                                  parsed == ParseResult::Unsupported ? Error::Unsupported
                                                                     : Error::Parameters,
                                  -30);
                return false;
            }
            conflict |= supplied[r.parameter_id] && supplied[r.parameter_id] != r.identity;
            sets.remember(r);
            supplied[r.parameter_id] = r.identity;
        }
        else if (entry->bufferType == BUFFER_TYPE_PPS)
        {
            uint32_t id{}, parent{};
            if (!parameter_ids(codec, data, entry->length, false, id, parent) ||
                !sets.pps(codec, data, entry->length))
            {
                lifecycle_failure(state, Error::Parameters, -30);
                return false;
            }
            conflict |= supplied_pps[id] && supplied_pps[id] != sets.pps_identity[id];
            supplied_pps[id] = sets.pps_identity[id];
        }
    }
    const Requirements *selected = nullptr;
    bool safe_idr = true, unknown = false;
    for (auto *entry = unit->bufferList; entry; entry = entry->next)
        if (entry->bufferType == BUFFER_TYPE_PICDATA)
        {
            uint32_t id{}, unused{};
            bool idr = false, found = false;
            if (!parameter_ids(codec, reinterpret_cast<const uint8_t *>(entry->data), entry->length,
                               true, id, unused, &idr, &found, &sets.picture_type))
            {
                unknown |= found;
                continue;
            } // Continuation fragments have no Annex-B slice header.
            safe_idr &= idr;
            if (!sets.pps_valid[id])
            {
                unknown = true;
                continue;
            }
            const auto *candidate = &sets.sps[sets.pps_sps[id]];
            if (selected && sets.selected_pps != id)
                conflict = true;
            selected = candidate;
            sets.selected_pps = id;
        }
    const bool activation =
        selected && (!state->decoder || state->lifecycle.parameters_changed(*selected) ||
                     !sets.native_ready(sets.selected_pps, *selected));
    if (selected)
    {
        sets.supplied_sps = supplied[selected->parameter_id] == selected->identity;
        sets.supplied_pps = supplied_pps[sets.selected_pps] == sets.pps_identity[sets.selected_pps];
    }
    // A policy refresh retains a usable native owner. A genuinely changed
    // picture configuration supersedes that optional wait and gets its own bound.
    if (state->policy_keyframe_wait &&
        (activation || state->decoder_needs_reset || !state->decoder))
    {
        if (state->stream_policy.pending_depth)
            state->stream_policy.reject();
        state->policy_keyframe_wait = false;
        state->await_keyframe = true;
        state->transition_wait_since = monotonic_us();
    }
    const bool verified_continuation = selected && !unknown && !conflict && state->decoder &&
                                       !activation && !state->decoder_needs_reset &&
                                       state->lifecycle.state == State::Ready;
    if (verified_continuation && state->policy_keyframe_wait &&
        (!safe_idr || unit->frameType != FRAME_TYPE_IDR))
        return true; // Optional IDR request does not stop valid old-instance pictures.
    bool optional_transition = state->stream_policy.pending_depth != 0;
    const bool retained_idr = selected && state->decoder && state->policy_keyframe_wait &&
                              !state->decoder_needs_reset && !activation && safe_idr &&
                              unit->frameType == FRAME_TYPE_IDR;
    if (optional_transition && retained_idr && (!sets.supplied_sps || !sets.supplied_pps))
    {
        // Optimization must not turn an already usable IDR into a missing-PPS
        // failure. Keep the old instance, which actually received these sets.
        state->stream_policy.reject();
        optional_transition = false;
    }
    const bool complete_required =
        activation || state->await_keyframe || state->decoder_needs_reset || optional_transition;
    const bool fresh_required = !retained_idr || optional_transition;
    if (!selected || unknown || conflict ||
        (complete_required && (!safe_idr || unit->frameType != FRAME_TYPE_IDR ||
                               (fresh_required && (!sets.supplied_sps || !sets.supplied_pps)))))
    {
        // Unknown/conflicting pictures cannot be treated as usable optional
        // continuation. Keep their mandatory resolution bound until an exact
        // old-instance continuation or a complete safe bootstrap proves safety.
        if (state->policy_keyframe_wait)
        {
            if (state->stream_policy.pending_depth)
                state->stream_policy.reject();
            state->policy_keyframe_wait = false;
            state->transition_wait_since = 0;
        }
        if (!state->transition_wait_since)
        {
            state->transition_wait_since = monotonic_us();
            ++state->parameter_transition_waits;
        }
        return false;
    }
    if (!complete_required)
    {
        // selected is the verified, actually submitted SPS/PPS generation and
        // no reset/recreation/keyframe operation remains. A prior rejected AU
        // must not leave a fatal deadline armed until an unrelated future IDR.
        state->transition_wait_since = 0;
        state->parameters_requested = false;
        state->policy_keyframe_wait = false;
        return true;
    }
    Limits limits{};
    limits.width = state->mode->max_width;
    limits.height = state->mode->max_height;
    limits.level_h264 = 60;
    limits.level_hevc = 156;
    limits.bit_depth = state->mode->hdr ? 10 : 8;
    if (optional_transition && state->lifecycle.parameters_changed(*selected))
    {
        state->stream_policy.reject();
        optional_transition = false;
    }
    limits.depth =
        optional_transition ? state->stream_policy.pending_depth : state->requested_depth;
    auto configuration = initial_plan(*selected, limits, INPUT_SLOT_COUNT, FRAME_SLOT_COUNT);
    if (optional_transition)
        configuration.dpb = std::max(configuration.dpb, state->lifecycle.effective.dpb);
    if (!configuration.supported || selected->visible_width != state->mode->visible_width ||
        selected->visible_height != state->mode->visible_height)
    {
        lifecycle_failure(state, Error::Unsupported, -31);
        return false;
    }
    if (optional_transition)
    {
        auto candidate = state->decoder_config;
        candidate.pipeline_depth = configuration.depth;
        candidate.max_dpb_frames = configuration.dpb;
        videodec2_decoder_memory_t memory{};
        memory.size = sizeof(memory);
        if (sceVideodec2QueryDecoderMemoryInfo(&candidate, &memory) != 0)
            state->stream_policy.reject(); // Existing instance remains usable.
        else
        {
            if (!state->running)
                return false;
            const auto previous_depth = state->pipeline_depth;
            if (!configure_stream_decoder(state, *selected, configuration, previous_depth))
                return false;
            state->stream_policy.configured(monotonic_us(), state->pipeline_depth);
        }
    }
    else if (state->lifecycle.changed(*selected) || !state->decoder)
    {
        if (!configure_stream_decoder(state, *selected, configuration))
            return false;
    }
    else
    {
        if (activation)
            state->decoder_needs_reset = true;
        state->lifecycle.stream = *selected;
    }
    state->transition_wait_since = 0;
    state->parameters_requested = false;
    state->policy_keyframe_wait = false;
    return true;
}

static bool decoder_deadline(native_renderer_state_t *state, uint64_t now)
{
    if (state->policy_keyframe_wait && !state->await_keyframe && state->decoder &&
        !state->decoder_needs_reset && state->lifecycle.state == moonlight::video::State::Ready &&
        state->transition_wait_since && now >= state->transition_wait_since &&
        now - state->transition_wait_since >= moonlight::video::Lifecycle::startup_timeout_us)
    {
        if (state->stream_policy.pending_depth)
            state->stream_policy.reject();
        state->policy_keyframe_wait = false;
        state->await_keyframe = false;
        state->transition_wait_since = 0;
        state->parameters_requested = false;
    }
    if (state->lifecycle.expired(now) ||
        (state->transition_wait_since && now >= state->transition_wait_since &&
         now - state->transition_wait_since >= moonlight::video::Lifecycle::startup_timeout_us))
    {
        lifecycle_failure(state, moonlight::video::Error::Parameters, -32);
        return false;
    }
    return state->running;
}

// Do not pass future parameter advertisements to a native instance configured
// for a different SPS. All picture bytes and active sets retain zero-copy decode.
static bool future_parameter(native_renderer_state_t *state, PLENTRY entry)
{
    using namespace moonlight::video;
    const Codec codec = state->mode->codec_type == 1 ? Codec::H264 : Codec::Hevc;
    if (entry->bufferType == BUFFER_TYPE_SPS)
    {
        Requirements r{};
        return parse_sps(codec, reinterpret_cast<const uint8_t *>(entry->data), entry->length, r) ==
                   ParseResult::Valid &&
               state->lifecycle.parameters_changed(r);
    }
    if (entry->bufferType == BUFFER_TYPE_PPS)
    {
        uint32_t id{}, parent{};
        return parameter_ids(codec, reinterpret_cast<const uint8_t *>(entry->data), entry->length,
                             false, id, parent) &&
               (!state->parameter_sets.pps_valid[id] ||
                state->lifecycle.parameters_changed(state->parameter_sets.sps[parent]) ||
                (id != state->parameter_sets.selected_pps &&
                 !state->parameter_sets.native_ready(id, state->parameter_sets.sps[parent])));
    }
    return false;
}

static bool recover_stream_capacity(native_renderer_state_t *state)
{
    using namespace moonlight::video;
    auto &l = state->lifecycle;
    const uint32_t alternative = l.stream.standard_dpb + 1;
    if (!l.stream.verified || alternative <= uint32_t(state->decoder_config.max_dpb_frames) ||
        alternative > 17 || !l.recover(Error::Capacity))
    {
        lifecycle_failure(state, Error::Capacity, int32_t(0x811d0302u));
        return false;
    }
    auto next = l.effective;
    next.dpb = alternative;
    if (!configure_stream_decoder(state, l.stream, next))
        return false;
    state->await_keyframe = true;
    state->transition_wait_since = monotonic_us();
    state->parameter_sets.clear();
    state->parameter_sets.remember(state->lifecycle.stream);
    state->parameters_requested = true;
    return true;
}

static int decode_refused(native_renderer_state_t *state, int32_t code,
                          moonlight::FrameTrace::Sample *trace)
{
    state->policy_keyframe_wait = false;
    state->last_result = code;
    state->last_decode_error = code;
    ++state->decode_errors;
    if (trace)
        trace->outcome = 3;
    if (state->stream_aware)
    {
        if (uint32_t(code) == 0x811d0302u)
            return recover_stream_capacity(state) ? DR_NEED_IDR : DR_OK;
        if (!state->lifecycle.corruption())
        {
            lifecycle_failure(state, moonlight::video::Error::Exhausted, code);
            return DR_OK;
        }
    }
    if (state->pipeline_depth > 1u)
    {
        ++state->pipeline_faults;
        state->clean_outputs = 0;
    }
    return DR_NEED_IDR;
}

// Copies one access unit into decoder input memory and submits it. Returns the
// status for LiCompleteVideoFrame(); `busy_us` is the time spent in Videodec2.
static int decode_access_unit(native_renderer_state_t *state, PDECODE_UNIT decode_unit,
                              uint64_t *busy_us)
{
    videodec2_input_t input = {};
    videodec2_frame_t frame = {};
    videodec2_output_t output = {};
    moonlight::FrameTrace::Sample *trace = nullptr;
    const uint64_t arrival_us = monotonic_us();
    const uint64_t callback_network_us = PltGetMicroseconds();
    const char *phase = "decode";
    PLENTRY entry;
    uint32_t fragment_count = 0;
    uint32_t hdr_active;
    uint32_t hdr_transitions;
    size_t copied = 0, omitted_parameters = 0;
    uint64_t started;
    uint64_t completed_us;
    uint64_t elapsed;
    uint64_t busy = 0;
    int32_t result;
    char receipt[512];

    if (busy_us)
        *busy_us = 0;
    if (!state || !decode_unit)
        return DR_NEED_IDR;
    if (!state->running)
        return DR_OK;
    if (state->stream_aware && (!decode_unit->bufferList || decode_unit->fullLength <= 0 ||
                                size_t(decode_unit->fullLength) > state->input_size))
    {
        lifecycle_failure(state, moonlight::video::Error::Parameters, -30);
        return DR_OK;
    }
    if (state->stream_aware && !prepare_stream_decoder(state, decode_unit))
    {
        if (state->policy_keyframe_wait)
            ++state->policy_dropped_aus;
        if (!state->running || state->parameters_requested)
            return DR_OK;
        state->parameters_requested = true;
        return DR_NEED_IDR;
    }
#if PROSPEROLIGHT_TEST_LAB
    // This is a declared synthetic API error, not a hardware-error observation.
    // The native quiescence, replacement, keyframe and subsequent output are real.
    if (state->stream_aware && prosperolight::test_lab::active() &&
        prosperolight::test_lab::active_profile().decoder_capacity_probe &&
        state->test_capacity_errors == 0 &&
        (prosperolight::test_lab::active_profile().decoder_capacity_probe_first ||
         (state->decoded >= 60 && state->presented.load() >= 30)))
    {
        state->test_capacity_decoded_before = state->decoded;
        state->test_capacity_presented_before = state->presented.load();
        ++state->test_capacity_errors;
        (void)lan_http_report_text("Native decoder developer probe: synthetic capacity error once "
                                   "(first-AU or established mode)");
        return decode_refused(state, int32_t(0x811d0302u), nullptr);
    }
#endif
    if (state->await_keyframe)
    {
        // The decoder was rebuilt outside a frame: resume only on a keyframe.
        if (decode_unit->frameType != FRAME_TYPE_IDR)
            return state->stream_aware ? DR_OK : DR_NEED_IDR;
        state->await_keyframe = false;
        state->policy_keyframe_wait = false;
        state->transition_wait_since = 0;
        state->parameters_requested = false;
    }
    if (state->decoder_needs_reset)
    {
        const int reset_result = sceVideodec2Reset(state->decoder);
        if (reset_result != 0)
        {
            if (state->stream_aware)
            {
                lifecycle_failure(state, moonlight::video::Error::Reset, reset_result);
                return DR_OK;
            }
            return decode_refused(state, reset_result, nullptr);
        }
        abandon_decoder_pictures(state);
        state->parameter_sets.forget_submitted();
    }
    // Every error exit requires reset before consuming the requested IDR.
    state->decoder_needs_reset = true;
    hdr_active = std::atomic_load_explicit(&host_hdr_active, std::memory_order_relaxed);
    hdr_transitions = std::atomic_load_explicit(&host_hdr_transitions, std::memory_order_relaxed);
    if (state->mode->hdr &&
        ((hdr_transitions && !hdr_active) || decode_unit->colorspace != COLORSPACE_REC_2020))
    {
        if (!state->hdr_mismatch_reported)
        {
            notification_request_t hdr_notification = {};

            state->hdr_mismatch_reported = 1;
            snprintf(receipt, sizeof(receipt),
                     "Moonlight HDR unavailable: frame=%d reported=%u confirmed=%u transitions=%u "
                     "colorspace=%u expected=%u",
                     decode_unit->frameNumber, decode_unit->hdrActive ? 1u : 0u, hdr_active,
                     hdr_transitions, decode_unit->colorspace, COLORSPACE_REC_2020);
            (void)lan_http_report_text(receipt);
            snprintf(hdr_notification.message, sizeof(hdr_notification.message),
                     hdr_active
                         ? "ProsperoLight HDR stopped: Sunshine sent an unexpected color space."
                         : "ProsperoLight HDR unavailable: enable HDR on Sunshine's captured "
                           "display and retry.");
            (void)sceKernelSendNotificationRequest(0, &hdr_notification, sizeof(hdr_notification),
                                                   0);
        }
        state->last_result = -20;
        state->running = 0;
        fail_stream(-20);
        return DR_NEED_IDR;
    }
    if (decode_unit->fullLength <= 0 || (size_t)decode_unit->fullLength > state->input_size ||
        !decode_unit->bufferList)
        return decode_refused(state, -12, nullptr);

    const uint64_t network_enqueue_us =
        decode_unit->enqueueTimeUs ? decode_unit->enqueueTimeUs : PltGetMicroseconds();
    // Use the upstream clock on both sides of enqueue->dequeue. This excludes
    // our copy, decode, and flip wait.
    // Captured alongside arrival_us, before deferred initialization, so the
    // clock translation and queue age exclude configuration work.
#if PROSPEROLIGHT_PERFORMANCE_DETAIL
    trace = frame_trace.append();
    if (trace)
    {
        trace->frame = decode_unit->frameNumber;
        trace->bytes = decode_unit->fullLength;
        trace->receive_us = decode_unit->receiveTimeUs;
        trace->enqueue_us = decode_unit->enqueueTimeUs;
        trace->callback_network_us = callback_network_us;
        trace->callback_us = arrival_us;
        trace->pts_us = decode_unit->presentationTimeUs;
        trace->host_us = static_cast<uint32_t>(decode_unit->frameHostProcessingLatency) * 100u;
    }
#endif
    if (!moonlight::record_reassembly(state->reassembly_timing, decode_unit->receiveTimeUs,
                                      decode_unit->enqueueTimeUs, callback_network_us))
        ++state->reassembly_invalid_samples;
    elapsed =
        callback_network_us > network_enqueue_us ? callback_network_us - network_enqueue_us : 0;
    state->queue_timing.add(elapsed);
    state->queue_window.add(arrival_us, elapsed);
    const int pending_video = LiGetPendingVideoFrames();
    if (trace)
        trace->pending = pending_video > 0 ? static_cast<uint32_t>(pending_video) : 0;
    if (pending_video > 0 && static_cast<uint32_t>(pending_video) > state->pending_video_high_water)
        state->pending_video_high_water = static_cast<uint32_t>(pending_video);
    if (!state->first_video_us)
        state->first_video_us = network_enqueue_us;
    state->last_video_us = network_enqueue_us;
    state->drops.observe(
        decode_unit->frameNumber,
        std::atomic_load_explicit(&video_recovery_epoch, std::memory_order_relaxed));
    const bool rate_updated = state->arrival.update(network_enqueue_us, decode_unit->frameNumber);
    if (!state->access_units || rate_updated)
        state->rtt_valid = LiGetEstimatedRttInfo(&state->rtt_ms, &state->rtt_variance_ms);
    if (decode_unit->frameHostProcessingLatency)
    {
        uint32_t latency = decode_unit->frameHostProcessingLatency;

        if (PROSPEROLIGHT_PERFORMANCE_DETAIL)
            state->host_timing.add(static_cast<uint64_t>(latency) * 100u);

        if (!state->host_latency_frames || latency < state->host_latency_min_tenths_ms)
            state->host_latency_min_tenths_ms = latency;
        if (latency > state->host_latency_max_tenths_ms)
            state->host_latency_max_tenths_ms = latency;
        state->host_latency_total_tenths_ms += latency;
        ++state->host_latency_frames;
    }
    if (state->catchup.update(arrival_us, pending_video, PROSPEROLIGHT_CATCHUP_QUEUE_FRAMES,
                              CATCHUP_HOLD_US))
    {
        // Opt-in bounded catch-up: trade one keyframe for the accumulated delay.
        ++state->catchup_refreshes;
        if (trace)
            trace->outcome = 3;
        return DR_NEED_IDR;
    }

    // In-flight access units never exceed the pipeline depth, so a round-robin
    // input slot is always one the decoder has finished reading.
    uint8_t *input_slot = static_cast<uint8_t *>(state->input_memory) +
                          (state->input_sequence++ % INPUT_SLOT_COUNT) * state->input_size;
    pthread_mutex_lock(&state->lock);
    const int offered = state->frames.acquire_free();
    pthread_mutex_unlock(&state->lock);
    if (offered < 0)
        return decode_refused(state, -18, trace);
    started = monotonic_us();
    for (entry = decode_unit->bufferList; entry; entry = entry->next)
    {
        if (!entry->data || entry->length <= 0 ||
            (size_t)entry->length > state->input_size - copied)
            return decode_refused(state, -13, trace);
        if (state->stream_aware && future_parameter(state, entry))
        {
            omitted_parameters += entry->length;
            continue;
        }
        memcpy(input_slot + copied, entry->data, (size_t)entry->length);
        copied += (size_t)entry->length;
        ++fragment_count;
    }
    elapsed = monotonic_us() - started;
    state->copy_total_us += elapsed;
    state->copy_timing.add(elapsed);
    if (elapsed > state->copy_max_us)
        state->copy_max_us = elapsed;
    if (!copied || copied + omitted_parameters != (size_t)decode_unit->fullLength)
        return decode_refused(state, -14, trace);

    // A cheap header scan on the first frames and twice a second afterwards.
    if (state->access_units < 8 || state->access_units % 60u == 0)
    {
        const auto slices =
            moonlight::count_video_slices(input_slot, copied, state->mode->codec_type != 1);
        if (slices)
        {
            if (!state->layout_samples || slices < state->observed_slices_min)
                state->observed_slices_min = slices;
            if (slices > state->observed_slices_max)
                state->observed_slices_max = slices;
            state->observed_slices_last = slices;
            ++state->layout_samples;
        }
    }
    input.size = sizeof(input);
    input.au = input_slot;
    input.au_size = copied;
    input.pts = decode_unit->presentationTimeUs;
    input.dts = UINT64_MAX;
    frame.size = sizeof(frame);
    frame.buffer = frame_slot_address(state, offered);
    frame.buffer_size = state->frame_size;
    output.size = sizeof(output);
    started = monotonic_us();
    result = sceVideodec2Decode(state->decoder, &input, &frame, &output);
    completed_us = monotonic_us();
    elapsed = completed_us - started;
    busy = elapsed;
    state->decode_total_us += elapsed;
    state->decode_timing.add(elapsed);
    if (!state->decode_calls)
        state->decode_first_us = elapsed;
    else
        (decode_unit->frameType == FRAME_TYPE_IDR ? state->idr_decode_timing
                                                  : state->inter_decode_timing)
            .add(elapsed);
    if (state->stream_fps && elapsed * state->stream_fps > 1000000u)
        ++state->decode_over_budget;
    if (trace)
        trace->decode_us = elapsed;
    if (elapsed > state->decode_max_us)
        state->decode_max_us = elapsed;
    ++state->decode_calls;
    ++state->access_units;
    state->fragments += fragment_count;
    state->stream_bytes += copied;
    state->au_size.add(copied);
    if (busy_us)
        *busy_us = busy;

    if (result != 0 || output.error)
    {
        report_decoder_call(state, phase, result, decode_unit->frameNumber, offered, frame, output);
        return decode_refused(state, result != 0 ? result : -15, trace);
    }
    if (state->stream_aware)
    {
        state->parameter_sets.submitted();
        state->active_pps = state->parameter_sets.selected_pps;
        state->active_pps_features = state->active_pps < 256
                                         ? state->parameter_sets.features[state->active_pps]
                                         : moonlight::video::PpsFeatures{};
        if (state->mode->codec_type == 1 && state->parameter_sets.picture_type < 5)
            ++state->h264_picture_types[state->parameter_sets.picture_type];
    }
    if (state->submission_count == SUBMISSION_QUEUE_CAPACITY)
        return decode_refused(state, -16, trace);
    {
        stream_submission_t &submission =
            state->submissions[(state->submission_head + state->submission_count) %
                               SUBMISSION_QUEUE_CAPACITY];
        submission.arrival_us = arrival_us;
        submission.enqueue_us = network_enqueue_us;
        submission.pts_us =
            state->source_clock.update(decode_unit->rtpTimestamp, decode_unit->presentationTimeUs);
        submission.frame = decode_unit->frameNumber;
        submission.trace = trace;
        ++state->submission_count;
    }

    if (!output.valid && state->pipeline_depth == 1u)
    {
        // Depth one: ask for the picture now, into the same frame buffer.
        memset(&output, 0, sizeof(output));
        output.size = sizeof(output);
        started = monotonic_us();
        result = sceVideodec2Flush(state->decoder, &frame, &output);
        completed_us = monotonic_us();
        elapsed = completed_us - started;
        busy += elapsed;
        state->flush_total_us += elapsed;
        if (PROSPEROLIGHT_PERFORMANCE_DETAIL)
            state->flush_timing.add(elapsed);
        if (elapsed > state->flush_max_us)
            state->flush_max_us = elapsed;
        ++state->flush_calls;
        phase = "flush";
        if (busy_us)
            *busy_us = busy;
        if (result != 0 || output.error)
        {
            report_decoder_call(state, phase, result, decode_unit->frameNumber, offered, frame,
                                output);
            return decode_refused(state, result != 0 ? result : -15, nullptr);
        }
    }

    result = settle_decoder_call(state, offered, frame, output, completed_us, pending_video);
    if (result == 0 && !output.valid)
    {
        // Deeper pipelines return the picture with a later access unit, but
        // never hold more than their depth.
        if (state->pipeline_depth == 1u || state->submission_count > state->pipeline_depth)
            result = -17;
        else
            ++state->decoder_delayed;
    }
    if (result != 0)
    {
        report_decoder_call(state, phase, result, decode_unit->frameNumber, offered, frame, output);
        return decode_refused(state, result, nullptr);
    }
    state->last_result = 0;
    state->decoder_needs_reset = false;
    return DR_OK;
}

// Depth above one depends on Videodec2 behaviour that only shows at run time.
// When it misbehaves, rebuild the decoder at depth one (the 01.000.062 path) on
// the same memory and resume from a keyframe.
static void fallback_to_classic_decoder(native_renderer_state_t *state, int32_t cause)
{
    if (state->stream_aware)
    {
        if (!state->lifecycle.recover(moonlight::video::Error::Reset))
        {
            lifecycle_failure(state, moonlight::video::Error::Exhausted, cause);
            return;
        }
        auto next = state->lifecycle.effective;
        next.depth = 1;
        if (configure_stream_decoder(state, state->lifecycle.stream, next))
        {
            state->await_keyframe = true;
            state->transition_wait_since = monotonic_us();
            state->parameter_sets.clear();
            state->parameter_sets.remember(state->lifecycle.stream);
            state->parameters_requested = false;
        }
        return;
    }
    videodec2_decoder_config_t config = state->decoder_config;
    const uint32_t previous_depth = state->pipeline_depth;
    void *decoder = nullptr;
    char receipt[256];
    int32_t result = sceVideodec2DeleteDecoder(state->decoder);

    if (result == 0)
    {
        state->decoder = nullptr;
        config.pipeline_depth = 1;
        result = sceVideodec2CreateDecoder(&config, &state->decoder_memory, &decoder);
    }
    if (result == 0)
    {
        state->decoder = decoder;
        result = sceVideodec2Reset(decoder);
    }
    snprintf(receipt, sizeof(receipt),
             "Moonlight decoder fallback: cause=%08x depth=%u->1 rc=%08x decoder=%p",
             (uint32_t)cause, previous_depth, (uint32_t)result, state->decoder);
    (void)lan_http_report_text(receipt);
    abandon_decoder_pictures(state);
    state->drain_enabled = false;
    state->pipeline_faults = 0;
    state->await_keyframe = true;
    if (result != 0)
    {
        state->last_result = result;
        state->decoder_lost = true;
        state->running = 0;
        fail_stream(result);
        return;
    }
    state->decoder_config = config;
    state->pipeline_depth = 1;
    state->decoder_needs_reset = false;
    ++state->decoder_recreations;
}

// While nothing else is queued, take the pictures the pipeline still holds:
// depth-one latency when keeping up, overlapped decoding when behind. One
// picture per Flush call. Videodec2 refuses a decode issued between two
// flushes of one drain (seen on hardware), so a drain that has started runs
// until the pipeline is empty, even if frames arrive meanwhile. Returns the
// time spent in Videodec2.
static uint64_t drain_decoder(native_renderer_state_t *state)
{
    uint64_t spent = 0;
    bool draining = false;

    while (
        !state->decoder_needs_reset && state->pipeline_depth > 1u && state->submission_count > 0u &&
        (draining || (state->running &&
                      moonlight::should_drain(state->drain_enabled, state->pipeline_depth,
                                              state->submission_count, LiGetPendingVideoFrames()))))
    {
        videodec2_frame_t frame = {};
        videodec2_output_t output = {};

        pthread_mutex_lock(&state->lock);
        const int offered = state->frames.acquire_free();
        pthread_mutex_unlock(&state->lock);
        if (offered < 0)
        {
            // A drain that cannot finish leaves the decoder unable to continue.
            if (draining)
            {
                state->decoder_needs_reset = true;
                state->await_keyframe = true;
            }
            break;
        }
        draining = true;
        frame.size = sizeof(frame);
        frame.buffer = frame_slot_address(state, offered);
        frame.buffer_size = state->frame_size;
        output.size = sizeof(output);
        const uint64_t started = monotonic_us();
        int32_t result = sceVideodec2Flush(state->decoder, &frame, &output);
        const uint64_t completed_us = monotonic_us();
        const uint64_t elapsed = completed_us - started;
        spent += elapsed;
        state->flush_total_us += elapsed;
        if (PROSPEROLIGHT_PERFORMANCE_DETAIL)
            state->flush_timing.add(elapsed);
        if (elapsed > state->flush_max_us)
            state->flush_max_us = elapsed;
        ++state->flush_calls;
        ++state->drain_calls;
        if (result == 0 && (output.error || !output.valid))
            result = -22;
        if (result == 0)
            result = settle_decoder_call(state, offered, frame, output, completed_us, 0);
        if (result != 0)
        {
            // Flushing mid-stream is not usable on this decoder: stop relying on it.
            report_decoder_call(state, "drain", result, -1, offered, frame, output);
            ++state->drain_faults;
            if (state->stream_aware && uint32_t(result) == 0x811d0302u)
                (void)recover_stream_capacity(state);
            else
                fallback_to_classic_decoder(state, result);
            break;
        }
    }
    return spent;
}

// Two-second decode-owner snapshots reuse existing totals. No new receive hooks,
// presenter locks, clocks per picture, or cross-thread reads of plain counters.
static void observe_pipeline(native_renderer_state_t *state, uint64_t now)
{
    if (!state->stream_aware || !state->pipeline_sampler.due(now))
        return;
    moonlight::video::PipelineCounters c{};
    c.submitted = state->access_units;
    c.decoded = state->decoded;
    c.presented = state->presented.load(std::memory_order_relaxed);
    c.bytes = state->stream_bytes;
    c.busy_us = state->decode_total_us + state->flush_total_us;
    c.ready_us = state->callback_to_decode_total_us;
    c.ready_calls = state->ready_calls;
    c.queue_us = state->queue_timing.total_us;
    c.losses = state->drops.network;
    c.overflows = video_queue_overflows.load(std::memory_order_relaxed);
    c.errors = state->decode_errors + state->drain_faults;
    c.stream_identity = state->lifecycle.stream.identity;
    c.parameter_identity =
        state->active_pps < 256 ? state->parameter_sets.pps_identity[state->active_pps] : 0;
    c.pending = std::max(0, LiGetPendingVideoFrames());
    c.in_flight = state->submission_count;
    c.generation = state->lifecycle.generation;
    c.recovery_epoch = video_recovery_epoch.load(std::memory_order_relaxed);
    const bool native_fault =
        state->pipeline_sampler.initialized && c.errors != state->pipeline_sampler.previous.errors;
    if (!state->pipeline_sampler.update(now, c, state->stream_fps))
    {
        state->stream_policy.discontinuity(now, native_fault);
        // A reset/insufficient sampler window cannot suspend an active trial's
        // absolute lifetime. Its empty window is ineligible for comparisons.
    }
    if (!state->decoder || state->await_keyframe || state->transition_wait_since ||
        state->decoder_needs_reset || state->lifecycle.state != moonlight::video::State::Ready)
        return;
    const auto action = state->stream_policy.observe(now, state->pipeline_sampler.last,
                                                     state->lifecycle.stream, state->stream_fps,
                                                     state->pipeline_depth, c.parameter_identity);
    if (action == moonlight::video::PolicyAction::None)
        return;
    state->policy_keyframe_wait = true;
    // The old native owner remains valid while an optional IDR is outstanding.
    // Mandatory native recovery sets await_keyframe independently.
    state->await_keyframe = false;
    state->parameters_requested = true;
    state->transition_wait_since = now;
    ++state->decoder_refreshes;
    video_recovery_epoch.fetch_add(1u, std::memory_order_relaxed);
    char line[224];
    snprintf(
        line, sizeof(line), "Native stream policy: action=%s depth=%u->%u queue=%llu ready=%llu us",
        moonlight::video::name(action), state->pipeline_depth, state->stream_policy.pending_depth,
        (unsigned long long)state->pipeline_sampler.last.queue_mean_us,
        (unsigned long long)state->pipeline_sampler.last.ready_mean_us);
    (void)lan_http_report_text(line);
    LiRequestIdrFrame();
}

static void *video_decode_thread(void *context)
{
    auto *state = static_cast<native_renderer_state_t *>(context);

    if (state->layout.decode)
        state->decode_placement_result = ps5_thread_affinity_set(state->layout.decode);
    while (std::atomic_load_explicit(&state->running, std::memory_order_acquire))
    {
        VIDEO_FRAME_HANDLE handle = nullptr;
        PDECODE_UNIT decode_unit = nullptr;
        uint64_t busy_us = 0;

        if (state->stream_aware && !decoder_deadline(state, monotonic_us()))
            break;
        const bool bounded_wait =
            state->stream_aware && (!state->decoder || state->transition_wait_since);
        if (!(bounded_wait ? LiPollNextVideoFrame(&handle, &decode_unit)
                           : LiWaitForNextVideoFrame(&handle, &decode_unit)))
        {
            // Shutdown or an explicit wake: `running` decides. Never spin.
            if (std::atomic_load_explicit(&state->running, std::memory_order_acquire))
                sceKernelUsleep(1000);
            continue;
        }
        const int status = decode_access_unit(state, decode_unit, &busy_us);
        LiCompleteVideoFrame(handle, status);
        if (status == DR_NEED_IDR)
        {
            // moonlight-common-c discarded its queue with this status.
            ++state->decoder_refreshes;
            std::atomic_fetch_add_explicit(&video_recovery_epoch, 1u, std::memory_order_relaxed);
        }
        else if (state->running && state->decoder && !state->await_keyframe)
            busy_us += drain_decoder(state);
        if (state->pipeline_depth > 1u && state->pipeline_faults >= PIPELINE_FAULT_LIMIT)
            fallback_to_classic_decoder(state, state->last_result);
        const uint64_t now = monotonic_us();
        state->decode_window.add(now, busy_us);
        observe_pipeline(state, now);
    }
    return nullptr;
}

static moonlight::FramePacing stream_pacer;
static unsigned stream_presentation_mode = 0;
static unsigned stream_vrr_profile = 1;

static bool wait_presentation_deadline(native_renderer_state_t *state,
                                       const stream_ready_frame_t &item,
                                       moonlight::VrrRepeatPolicy &vrr,
                                       PacingDecisionTrace &decisions)
{
    if (!stream_presentation_mode)
        return true;
    uint32_t width = 0, height = 0, refresh = 0;
    native_agc_output_status(&width, &height, &refresh);
    // VRR uses source cadence, not the changing observed display rate. Fixed
    // VSync uses completed-flip observations as a best-effort phase anchor.
    const bool fixed =
        native_agc_vsync_active() && (stream_presentation_mode == 1 || !native_agc_vrr_active());
    const uint64_t started = monotonic_us();
    // The latest ready successor can justify catching up within VideoOut's
    // refresh ceiling. Never accelerate a lone frame or an idle watchdog.
    pthread_mutex_lock(&state->lock);
    const bool successor_queued = state->mailbox.full;
    pthread_mutex_unlock(&state->lock);
    const uint64_t queue_age_us = started > item.ready_us ? started - item.ready_us : 0;
    const uint64_t deadline =
        native_agc_vrr_active()
            ? vrr.picture_target(started, successor_queued, queue_age_us)
            : stream_pacer.target(
                  item.frame, item.pts_us, started, fixed ? refresh : 0,
                  fixed ? state->last_present_us : 0, fixed ? 0 : refresh,
                  std::max<uint64_t>(250, state->present_call_timing.percentile(99) +
                                              stream_pacer.wake_lead_us()));
    decisions.record(item.frame, item.pts_us, started, deadline, "target");
    uint64_t now = started;
    while (now < deadline)
    {
        pthread_mutex_lock(&state->lock);
        const bool stopping = state->stop_presenting;
        pthread_mutex_unlock(&state->lock);
        if (stopping)
            return false;
        const uint64_t remaining = deadline - now;
        const uint64_t lead = stream_pacer.wake_lead_us();
        if (remaining > lead)
            sceKernelUsleep(static_cast<uint32_t>(std::min<uint64_t>(1000, remaining - lead)));
        else
            __builtin_ia32_pause(); // Active tail is bounded to at most 250 us.
        now = monotonic_us();
    }
    if (native_agc_vrr_active())
        vrr.waited(started, deadline, now);
    else
        stream_pacer.submitted(deadline, now, now - started);
    return true;
}

static int submit_presentation(native_renderer_state_t *state, const stream_ready_frame_t &item)
{
    native_agc_metrics_t hud = item.hud;
    const native_video_mode_t *mode = state->mode;
    const uint64_t started = monotonic_us();
    const uint64_t waited = started > item.ready_us ? started - item.ready_us : 0;

    hud.rendering_fps_x100 = state->rendering_rate.fps_x100;
    hud.vsync_enabled = native_agc_vsync_active() ? 1u : 0u;
    hud.pacing_mode = stream_presentation_mode;
    hud.vrr_api_active = native_agc_vrr_active();
    hud.pacing_reserve_us = stream_pacer.stats.reserve_us;
    if (prosperolight_debug_enabled() == 1)
    {
        // The numbers of the statistics overlay, once a second, on this (presentation) thread.
        static uint64_t traced_us = 0;
        if (started - traced_us >= 1000000u)
        {
            traced_us = started;
            prosperolight_debug_line(
                "stream",
                "fps in=%u.%02u decoded=%u.%02u shown=%u.%02u | lost network=%u.%02u%% "
                "decoder=%u.%02u%% not_shown=%u | "
                "decode avg=%llu p95=%llu us load=%u/1000 | queue avg=%llu max=%llu us pending=%u "
                "| "
                "rtt=%u+-%u ms valid=%u host=%u.%u ms | present wait=%llu us vsync=%u pacing=%u "
                "vrr=%u",
                hud.incoming_fps_x100 / 100, hud.incoming_fps_x100 % 100,
                hud.decoded_fps_x100 / 100, hud.decoded_fps_x100 % 100,
                hud.rendering_fps_x100 / 100, hud.rendering_fps_x100 % 100,
                hud.network_drop_percent_x100 / 100, hud.network_drop_percent_x100 % 100,
                hud.decoder_drop_percent_x100 / 100, hud.decoder_drop_percent_x100 % 100,
                hud.not_displayed, static_cast<unsigned long long>(hud.decode_average_us),
                static_cast<unsigned long long>(hud.decode_p95_us), hud.decoder_load_permille,
                static_cast<unsigned long long>(hud.queue_delay_average_us),
                static_cast<unsigned long long>(hud.queue_delay_max_us), hud.pending_frames,
                hud.rtt_ms, hud.rtt_variance_ms, hud.rtt_valid, hud.host_average_tenths_ms / 10,
                hud.host_average_tenths_ms % 10, static_cast<unsigned long long>(waited),
                hud.vsync_enabled, hud.pacing_mode, hud.vrr_api_active);
        }
    }
    if (PROSPEROLIGHT_PERFORMANCE_DETAIL)
        state->present_wait_timing.add(waited);
    if (item.trace)
        item.trace->present_wait_us = waited;
    const int result =
        mode->hdr ? native_agc_present_main10(item.buffer, item.buffer_size, item.pitch,
                                              item.height, mode->visible_width,
                                              mode->visible_height, state->stream_fps, &hud)
                  : native_agc_present_nv12(item.buffer, item.buffer_size, item.pitch, item.height,
                                            mode->visible_width, mode->visible_height,
                                            state->stream_fps, &hud);
    const uint64_t finished = monotonic_us();
    const uint64_t elapsed = finished - started;
    state->present_total_us += elapsed;
    if (PROSPEROLIGHT_PERFORMANCE_DETAIL)
        state->present_call_timing.add(elapsed);
    if (elapsed > state->present_max_us)
        state->present_max_us = elapsed;
    if (result != 0)
    {
        std::atomic_store_explicit(&state->present_result, result, std::memory_order_relaxed);
        return result;
    }
    if (item.trace)
    {
        item.trace->submit_us = finished;
        item.trace->outcome = 1;
    }
    return 0;
}

// Waits for the submitted flip; afterwards the GPU no longer reads the slot.
static int complete_presentation(native_renderer_state_t *state, const stream_ready_frame_t &item)
{
    const uint64_t wait_started = moonlight::performance_now_us();
    const int result = native_agc_finish_frame();

    moonlight::record_performance_elapsed(state->completion_wait_timing, wait_started);
    if (result != 0)
    {
        std::atomic_store_explicit(&state->present_result, result, std::memory_order_relaxed);
        return result;
    }
    const uint64_t flipped_us = monotonic_us();
    const uint32_t presented =
        std::atomic_load_explicit(&state->presented, std::memory_order_relaxed);
    if (item.trace)
        item.trace->completion_us = flipped_us;
    if (presented != 0)
        state->flip_interval_timing.add(flipped_us - state->last_present_us);
    else
        state->first_present_us = flipped_us;
    state->last_present_us = flipped_us;
    const uint64_t flipped_network_us = PltGetMicroseconds();
    state->frame_age_timing.add(
        flipped_network_us > item.enqueue_us ? flipped_network_us - item.enqueue_us : 0);
    const uint64_t elapsed = flipped_us > item.arrival_us ? flipped_us - item.arrival_us : 0;
    state->callback_to_flip_total_us += elapsed;
    if (state->latency_calls == 0 || elapsed < state->callback_to_flip_min_us)
        state->callback_to_flip_min_us = elapsed;
    if (elapsed > state->callback_to_flip_max_us)
        state->callback_to_flip_max_us = elapsed;
    ++state->latency_calls;
    std::atomic_store_explicit(&state->presented, presented + 1u, std::memory_order_relaxed);
    state->rendering_rate.update(flipped_us, presented + 1u);
    pthread_mutex_lock(&state->lock);
    state->frames.release(item.slot);
    pthread_mutex_unlock(&state->lock);
    if (PROSPEROLIGHT_LAN_TELEMETRY && (presented == 0 || (presented + 1u) % 300u == 0u))
    {
        char receipt[256];

        snprintf(receipt, sizeof(receipt),
                 "Moonlight presentation progress: frame=%d presented=%u slot=%d "
                 "dequeue_to_flip_us=%llu",
                 item.frame, presented + 1u, item.slot, (unsigned long long)elapsed);
        (void)lan_http_report_text(receipt);
    }
    return 0;
}

static void *video_present_thread(void *context)
{
    using Pool = moonlight::SlotPool<FRAME_SLOT_COUNT>;
    auto *state = static_cast<native_renderer_state_t *>(context);
    stream_ready_frame_t current{};
    bool flip_pending = false;
    bool first_picture = state->presented.load(std::memory_order_relaxed) == 0;
    unsigned failures = 0;
    uint64_t last_scanout_us = 0, repeated = 0;
    uint64_t unique_request_us = 0; // Last submitted real flip, not idle repeats.
    // Deferred repeats retry with bounded backoff, even on a static desktop.
    moonlight::RepeatRetryPolicy repeat_retry;
    uint64_t failed_repeats = 0;
    ScanoutTrace scanout_trace("native", stream_presentation_mode);
    PacingDecisionTrace decisions("native", stream_presentation_mode);
    // Decode may publish while presentation exits. Publish/clear the borrowed
    // trace under the mailbox lock so a producer cannot use a destroyed ring.
    struct DecisionLifetime
    {
        native_renderer_state_t *state;
        DecisionLifetime(native_renderer_state_t *s, PacingDecisionTrace *trace) : state(s)
        {
            pthread_mutex_lock(&state->lock);
            state->pacing_decisions = trace;
            pthread_mutex_unlock(&state->lock);
        }
        ~DecisionLifetime()
        {
            pthread_mutex_lock(&state->lock);
            state->pacing_decisions = nullptr;
            pthread_mutex_unlock(&state->lock);
        }
    } decision_lifetime(state, &decisions);
    moonlight::VrrRepeatPolicy vrr_repeats;
    vrr_repeats.reset(state->stream_fps);
    uint32_t vrr_width = 0, vrr_height = 0, vrr_refresh = 0;
    native_agc_output_status(&vrr_width, &vrr_height, &vrr_refresh);
    vrr_repeats.configure(stream_vrr_profile, vrr_refresh);
    unsigned cadence_rate = state->stream_fps, cadence_copies = vrr_repeats.repeat_factor();
    struct VrrSummary
    {
        const moonlight::VrrRepeatPolicy &vrr;
        ~VrrSummary()
        {
            if (native_agc_vrr_active())
                LOGI("Native VRR scheduler: period_us=%llu pictures=%llu repeats=%llu wait_us=%llu "
                     "late_max_us=%llu submission_gap_max_us=%llu source_fps=%u repeat_factor=%u "
                     "interval_us=%llu profile=%u vrr_reserve_us=%llu feedback_misses=%llu "
                     "feedback_ambiguous=%llu feedback_epochs=%llu",
                     (unsigned long long)vrr.period(), (unsigned long long)vrr.stats.pictures,
                     (unsigned long long)vrr.stats.repeats,
                     (unsigned long long)vrr.stats.wait_total_us,
                     (unsigned long long)vrr.stats.late_max_us,
                     (unsigned long long)vrr.stats.gap_max_us, vrr.source_rate(),
                     vrr.repeat_factor(), (unsigned long long)vrr.interval(), stream_vrr_profile,
                     (unsigned long long)vrr.playout_reserve_us(),
                     (unsigned long long)vrr.feedback_misses(),
                     (unsigned long long)vrr.feedback_ambiguous(),
                     (unsigned long long)vrr.feedback_epoch_resets());
        }
    } vrr_summary{vrr_repeats};

    if (state->layout.present)
        state->present_placement_result = ps5_thread_affinity_set(state->layout.present);
    while (failures < PRESENT_FAILURE_LIMIT)
    {
        if (flip_pending)
        {
            // Retire the flip before choosing the next picture, so the choice is
            // made as late as possible and the newest decoded frame wins.
            if (complete_presentation(state, current) != 0)
            {
                ++failures;
                ++state->present_errors;
                pthread_mutex_lock(&state->lock);
                const bool stopping = state->stop_presenting;
                pthread_mutex_unlock(&state->lock);
                if (stopping)
                    return nullptr; // Teardown retires or faults the pending flip.
                continue;
            }
            flip_pending = false;
            last_scanout_us = monotonic_us();
            // Learn only after the unique flip is reported complete by
            // VideoOut. Observation time is not a physical HDMI timestamp.
            vrr_repeats.observe_output_feedback(uint32_t(current.frame), current.pts_us,
                                                current.ready_us, last_scanout_us,
                                                native_agc_vrr_active(), unique_request_us);
            uint64_t count = 0, argument = 0;
            if (native_agc_scanout_counter(&count, &argument) == 0)
            {
                scanout_trace.observe(count, argument, last_scanout_us, false);
                vrr_repeats.scanned(count, last_scanout_us);
            }
            repeat_retry.succeeded();
            failures = 0;
        }
        pthread_mutex_lock(&state->lock);
        while (!state->mailbox.full && !state->stop_presenting)
        {
            // A fixed-refresh television holds the last picture by itself. Only a
            // variable output needs it sent again, to stay above the display's
            // lowest rate while the PC has nothing new.
            if (!last_scanout_us || !native_agc_vrr_active())
            {
                pthread_cond_wait(&state->wake, &state->lock);
                continue;
            }
            const uint64_t now = monotonic_us();
            const uint64_t repeat_deadline = repeat_retry.deadline(vrr_repeats.idle_deadline());
            if (now >= repeat_deadline)
            {
                // Decode can publish during the timed wait. A fresh picture
                // always beats an optional LFC duplicate.
                if (state->mailbox.full)
                    break;
                pthread_mutex_unlock(&state->lock);
                const uint64_t repeat_submitted = monotonic_us();
                const int result = native_agc_repeat_frame();
                last_scanout_us = monotonic_us();
                if (result == 0)
                {
                    vrr_repeats.repeated(repeat_submitted);
                    repeat_retry.succeeded();
                }
                else
                {
                    ++failed_repeats;
                    repeat_retry.failed(last_scanout_us);
                }
                decisions.record(0, 0, repeat_submitted, repeat_deadline,
                                 result == 0 ? "repeat" : "repeat-deferred");
                uint64_t count = 0, argument = 0;
                if (result == 0 && native_agc_scanout_counter(&count, &argument) == 0)
                {
                    scanout_trace.observe(count, argument, last_scanout_us, true);
                    vrr_repeats.scanned(count, last_scanout_us);
                }
                if (result == 0)
                    ++repeated;
                if ((result == 0 && (repeated == 1 || repeated % 600 == 0)) ||
                    (result != 0 && (failed_repeats == 1 || failed_repeats % 240 == 0)))
                {
                    char receipt[160];
                    snprintf(receipt, sizeof(receipt),
                             "Scanout repeat: count=%llu rc=%08x delay_us=%llu vrr_api=%d "
                             "compensation=%d",
                             (unsigned long long)repeated, (unsigned)result,
                             (unsigned long long)vrr_repeats.interval(), native_agc_vrr_active(),
                             vrr_repeats.compensating());
                    (void)lan_http_report_text(receipt);
                }
                pthread_mutex_lock(&state->lock);
                // Keep accepting real pictures; retry optional repeats later.
                continue;
            }
            timespec timeout{};
            clock_gettime(CLOCK_REALTIME, &timeout);
            timeout.tv_nsec += (repeat_deadline - now) * 1000u;
            timeout.tv_sec += timeout.tv_nsec / 1000000000;
            timeout.tv_nsec %= 1000000000;
            pthread_cond_timedwait(&state->wake, &state->lock, &timeout);
        }
        if (state->stop_presenting || !state->mailbox.take(&current))
        {
            pthread_mutex_unlock(&state->lock);
            return nullptr;
        }
        decisions.record(current.frame, current.pts_us, monotonic_us(), current.ready_us, "ready");
        // Only discard an old ready image if a decoded replacement exists.
        // GPU/decoder service is not counted as replaceable queue residence.
        while (stream_presentation_mode && state->mailbox.full &&
               monotonic_us() >
                   current.ready_us + stream_pacer.admission_limit_us(
                                          0, native_agc_vrr_active() && vrr_repeats.grid_active()
                                                 ? vrr_repeats.interval()
                                                 : 0))
        {
            decisions.record(current.frame, current.pts_us, monotonic_us(), 0, "drop-age");
            state->frames.release(current.slot);
            if (current.trace)
                current.trace->outcome = 2;
            ++state->not_displayed;
            state->mailbox.take(&current);
        }
        state->frames.state[static_cast<size_t>(current.slot)] = Pool::Presenting;
        pthread_mutex_unlock(&state->lock);
        if (first_picture)
        {
            // The connecting screen leaves before the first picture is shown,
            // and the presenter's figures start with the stream's own frames.
            connecting_screen_finish();
            native_agc_reset_performance();
            first_picture = false;
        }
        const bool was_compensating = vrr_repeats.compensating();
        vrr_repeats.observe_picture(current.pts_us, uint32_t(current.frame));
        vrr_repeats.observe_readiness(current.pts_us, current.ready_us, uint32_t(current.frame));
        if (native_agc_vrr_active() && (cadence_rate != vrr_repeats.source_rate() ||
                                        cadence_copies != vrr_repeats.repeat_factor()))
        {
            cadence_rate = vrr_repeats.source_rate();
            cadence_copies = vrr_repeats.repeat_factor();
            LOGI("Native VRR cadence: source_fps=%u repeat_factor=%u target_refresh_x100=%u "
                 "interval_us=%llu",
                 cadence_rate, cadence_copies, vrr_repeats.target_refresh_x100(),
                 (unsigned long long)vrr_repeats.interval());
        }

        if (native_agc_vrr_active() && was_compensating && !vrr_repeats.compensating())
        {
            stream_pacer.resume(monotonic_us());
            char receipt[96];
            snprintf(receipt, sizeof(receipt), "Native VRR recovery: source_fps=%u phase_resume=1",
                     vrr_repeats.source_rate());
            (void)lan_http_report_text(receipt);
        }
        if (!wait_presentation_deadline(state, current, vrr_repeats, decisions))
        {
            pthread_mutex_lock(&state->lock);
            state->frames.release(current.slot);
            pthread_mutex_unlock(&state->lock);
            return nullptr;
        }
        if (submit_presentation(state, current) == 0)
        {
            unique_request_us = monotonic_us();
            decisions.record(current.frame, current.pts_us, unique_request_us, 0, "submit");
            vrr_repeats.presented(unique_request_us);
            flip_pending = true;
            continue;
        }
        // Nothing reads the picture: return its slot and try the next one.
        ++failures;
        ++state->present_errors;
        ++state->not_displayed;
        decisions.record(current.frame, current.pts_us, monotonic_us(), 0, "drop-present-error");
        pthread_mutex_lock(&state->lock);
        state->frames.release(current.slot);
        pthread_mutex_unlock(&state->lock);
    }
    // Presentation stopped responding; end the stream instead of spinning.
    fail_stream(std::atomic_load_explicit(&state->present_result, std::memory_order_relaxed));
    return nullptr;
}

static void join_video_workers(native_renderer_state_t *state)
{
    state->running = 0;
    if (state->decode_started)
    {
        (void)pthread_join(state->decode_thread, NULL);
        state->decode_started = false;
    }
    if (state->present_started)
    {
        pthread_mutex_lock(&state->lock);
        state->stop_presenting = true;
        pthread_cond_broadcast(&state->wake);
        pthread_mutex_unlock(&state->lock);
        (void)pthread_join(state->present_thread, NULL);
        state->present_started = false;
    }
    state->parameter_sets.clear();
    state->transition_wait_since = 0;
    state->parameters_requested = false;
    if (state->stream_aware && state->lifecycle.state != moonlight::video::State::Failed)
        state->lifecycle.state = moonlight::video::State::Stopping;
}

static void moonlight_renderer_start(void);
static void moonlight_renderer_stop(void);
static void moonlight_renderer_cleanup(void);

// Pull renderer: moonlight-common-c creates no decoder thread and calls no
// submit callback; the decode worker takes frames from its queue instead.
static DECODER_RENDERER_CALLBACKS moonlight_video_callbacks = {
    .setup = moonlight_renderer_setup,
    .start = moonlight_renderer_start,
    .stop = moonlight_renderer_stop,
    .cleanup = moonlight_renderer_cleanup,
    .submitDecodeUnit = nullptr,
    .capabilities = CAPABILITY_PULL_RENDERER,
};

// moonlight-common-c fills the empty slots of this struct with placeholders
// on every connection, and refuses a pull renderer that has a submit
// callback. Restore the pull-renderer form before each LiStartConnection(),
// or only the first connection of the process succeeds.
static void prepare_video_callbacks(uint32_t slices, bool h264)
{
    moonlight_video_callbacks.submitDecodeUnit = nullptr;
    moonlight_video_callbacks.capabilities =
        CAPABILITY_PULL_RENDERER | CAPABILITY_SLICES_PER_FRAME(slices);
    if (PROSPEROLIGHT_REFERENCE_FRAME_INVALIDATION)
        moonlight_video_callbacks.capabilities |=
            h264 ? CAPABILITY_REFERENCE_FRAME_INVALIDATION_AVC
                 : CAPABILITY_REFERENCE_FRAME_INVALIDATION_HEVC;
}

static int moonlight_renderer_setup(int video_format, int width, int height, int redraw_rate,
                                    void *context, int dr_flags)
{
    char receipt[256];

    auto *state = static_cast<native_renderer_state_t *>(context);

    if (!state || !state->mode || video_format != state->mode->video_format ||
        width != (int)state->mode->visible_width || height != (int)state->mode->visible_height ||
        redraw_rate != (int)state->stream_fps || dr_flags != 0)
        return -1;

    active_renderer = state;
    active_renderer->last_result = 0;
#if PROSPEROLIGHT_TEST_LAB
    if (prosperolight::test_lab::active())
        prosperolight::test_lab::context().negotiated(video_format, width, height, redraw_rate);
#endif
    snprintf(receipt, sizeof(receipt),
             "Moonlight callbacks setup: format=%x display=%dx%d fps=%d flags=%x capabilities=%x",
             video_format, width, height, redraw_rate, dr_flags,
             (unsigned)moonlight_video_callbacks.capabilities);
    (void)lan_http_report_text(receipt);
    return 0;
}

static void moonlight_renderer_start(void)
{
    native_renderer_state_t *state = active_renderer;
    int result;

#if PROSPEROLIGHT_PERFORMANCE_DETAIL
    frame_trace.count = frame_trace.omitted = 0;
#endif
    if (!state)
        return;
    renderer_sync_init(state);
    state->stop_presenting = false;
    state->running = 1;
    if (state->stream_aware)
        state->lifecycle.wait(monotonic_us());
    result = state->stream_aware
                 ? 0
                 : pthread_create(&state->present_thread, NULL, video_present_thread, state);
    state->present_started = !state->stream_aware && result == 0;
    if (result == 0)
    {
        result = pthread_create(&state->decode_thread, NULL, video_decode_thread, state);
        state->decode_started = result == 0;
    }
    if (result != 0)
    {
        state->last_result = result;
        fail_stream(result);
    }
}

static void moonlight_renderer_stop(void)
{
    native_renderer_state_t *state = active_renderer;

    if (!state)
        return;
    state->running = 0;
    // Not every moonlight-common-c exit path shuts the frame queue down.
    if (state->decode_started)
        LiWakeWaitForVideoFrame();
}

static void moonlight_renderer_cleanup(void)
{
    if (active_renderer)
        join_video_workers(active_renderer);
    active_renderer = NULL;
}

static void audio_ring_drop(ps5_audio_state_t *state, uint32_t frames)
{
    state->ring_head = (state->ring_head + frames) % AUDIO_RING_FRAMES;
    state->ring_count -= frames;
    state->dropped_frames += frames;
}

static void audio_ring_push(ps5_audio_state_t *state, const int16_t *pcm, uint32_t frames)
{
    uint32_t i;

    if (frames > AUDIO_RING_FRAMES)
    {
        uint32_t skip = frames - AUDIO_RING_FRAMES;

        pcm += skip * state->channels;
        state->dropped_frames += skip + state->ring_count;
        state->ring_head = 0;
        state->ring_tail = 0;
        state->ring_count = 0;
        frames = AUDIO_RING_FRAMES;
        ++state->overruns;
    }
    else if (state->ring_count + frames > AUDIO_RING_FRAMES)
    {
        audio_ring_drop(state, state->ring_count + frames - AUDIO_RING_FRAMES);
        ++state->overruns;
    }

    for (i = 0; i < frames; ++i)
    {
        int16_t *output = &state->ring[state->ring_tail * state->output_channels];

        memcpy(output, &pcm[i * state->channels], state->channels * sizeof(int16_t));
        if (state->output_channels > state->channels)
            memset(output + state->channels, 0,
                   (state->output_channels - state->channels) * sizeof(int16_t));
        state->ring_tail = (state->ring_tail + 1u) % AUDIO_RING_FRAMES;
    }
    state->ring_count += frames;
}

static void audio_ring_pop(ps5_audio_state_t *state, int16_t *pcm, uint32_t frames)
{
    uint32_t i;

    for (i = 0; i < frames; ++i)
    {
        memcpy(&pcm[i * state->output_channels],
               &state->ring[state->ring_head * state->output_channels],
               state->output_channels * sizeof(int16_t));
        state->ring_head = (state->ring_head + 1u) % AUDIO_RING_FRAMES;
    }
    state->ring_count -= frames;
}

static bool ps5_audio_surround_available()
{
    char receipt[192];
    const int32_t init_result = sceAudioOutInit();
    int32_t handle = -1;
    int32_t close_result = -1;

    if (init_result == 0 || (uint32_t)init_result == AUDIO_OUT_ALREADY_INIT)
        handle = sceAudioOutOpen(PS5_AUDIO_USER_SYSTEM, PS5_AUDIO_PORT_MAIN, 0, AUDIO_GRAIN_FRAMES,
                                 48000, PS5_AUDIO_FORMAT_S16_8CH);
    if (handle > 0)
        close_result = sceAudioOutClose(handle);
    snprintf(receipt, sizeof(receipt),
             "Moonlight 5.1 probe: init=%08x open=%08x close=%08x available=%u",
             (uint32_t)init_result, (uint32_t)handle, (uint32_t)close_result, handle > 0 ? 1u : 0u);
    (void)lan_http_report_text(receipt);
    return handle > 0;
}

static int ps5_audio_init(int audio_configuration, const POPUS_MULTISTREAM_CONFIGURATION opus,
                          void *context, int flags)
{
    char receipt[512];

    (void)context;
    (void)flags;
    memset(&audio_state, 0, sizeof(audio_state));
    audio_state.handle = -1;
    audio_state.open_result = -1;
    audio_state.drain_result = -1;
    audio_state.close_result = -1;
    audio_state.opus_error = OPUS_BAD_ARG;
    audio_state.channels = opus->channelCount;
    audio_state.output_channels =
        opus->channelCount == AUDIO_51_CHANNELS ? AUDIO_MAX_CHANNELS : AUDIO_STEREO_CHANNELS;
    audio_state.samples_per_frame = opus->samplesPerFrame;

    if (opus->sampleRate != 48000 ||
        (opus->channelCount != AUDIO_STEREO_CHANNELS && opus->channelCount != AUDIO_51_CHANNELS) ||
        CHANNEL_COUNT_FROM_AUDIO_CONFIGURATION(audio_configuration) != opus->channelCount ||
        opus->samplesPerFrame <= 0 || opus->samplesPerFrame > (int)AUDIO_DECODE_MAX_FRAMES)
    {
        snprintf(receipt, sizeof(receipt),
                 "Moonlight audio rejected: rate=%d channels=%d frame_samples=%d", opus->sampleRate,
                 opus->channelCount, opus->samplesPerFrame);
        (void)lan_http_report_text(receipt);
        return -1;
    }

    audio_state.decoder = opus_multistream_decoder_create(
        opus->sampleRate, opus->channelCount, opus->streams, opus->coupledStreams,
        (const unsigned char *)opus->mapping, &audio_state.opus_error);
    if (!audio_state.decoder)
        goto failed;

    audio_state.init_result = sceAudioOutInit();
    if (audio_state.init_result != 0 && (uint32_t)audio_state.init_result != AUDIO_OUT_ALREADY_INIT)
        goto failed;

    audio_state.handle = sceAudioOutOpen(
        PS5_AUDIO_USER_SYSTEM, PS5_AUDIO_PORT_MAIN, 0, AUDIO_GRAIN_FRAMES, 48000,
        audio_state.output_channels == AUDIO_MAX_CHANNELS ? PS5_AUDIO_FORMAT_S16_8CH
                                                          : PS5_AUDIO_FORMAT_S16_STEREO);
    audio_state.open_result = audio_state.handle;
    if (audio_state.handle <= 0)
        goto failed;

    snprintf(receipt, sizeof(receipt),
             "Moonlight audio ready: rate=%d channels=%d output_channels=%d streams=%d coupled=%d "
             "frame_samples=%d opus=%d init=%08x handle=%08x grain=%u",
             opus->sampleRate, opus->channelCount, audio_state.output_channels, opus->streams,
             opus->coupledStreams, opus->samplesPerFrame, audio_state.opus_error,
             (uint32_t)audio_state.init_result, (uint32_t)audio_state.handle, AUDIO_GRAIN_FRAMES);
    (void)lan_http_report_text(receipt);
    return 0;

failed:
    snprintf(receipt, sizeof(receipt),
             "Moonlight audio init failed: rate=%d channels=%d frame_samples=%d opus=%d init=%08x "
             "open=%08x",
             opus->sampleRate, opus->channelCount, opus->samplesPerFrame, audio_state.opus_error,
             (uint32_t)audio_state.init_result, (uint32_t)audio_state.open_result);
    (void)lan_http_report_text(receipt);
    if (audio_state.decoder)
    {
        opus_multistream_decoder_destroy(audio_state.decoder);
        audio_state.decoder = NULL;
    }
    audio_state.handle = -1;
    return -1;
}

static void ps5_audio_cleanup(void)
{
    const RTP_AUDIO_STATS *rtp = LiGetRTPAudioStats();

    if (rtp)
        audio_state.rtp = *rtp;
    if (audio_state.handle > 0)
    {
        audio_state.drain_result = sceAudioOutOutput(audio_state.handle, NULL);
        audio_state.close_result = sceAudioOutClose(audio_state.handle);
        audio_state.handle = -1;
    }
    if (audio_state.decoder)
    {
        opus_multistream_decoder_destroy(audio_state.decoder);
        audio_state.decoder = NULL;
    }
}

static void ps5_audio_sample(char *sample_data, int sample_length)
{
    uint64_t now, started, elapsed;
    uint32_t pcm_samples;
    int decode_capacity;
    int decoded;

    if (!audio_state.decoder || audio_state.handle <= 0)
        return;

    now = monotonic_us();
    if (audio_state.packets == 0)
    {
        audio_state.first_packet_us = now;
    }
    else
    {
        elapsed = now - audio_state.last_packet_us;
        audio_state.interval_total_us += elapsed;
        if (audio_state.packets == 1 || elapsed < audio_state.interval_min_us)
            audio_state.interval_min_us = elapsed;
        if (elapsed > audio_state.interval_max_us)
            audio_state.interval_max_us = elapsed;
    }
    audio_state.last_packet_us = now;
    ++audio_state.packets;
    if (!sample_data)
        ++audio_state.plc_packets;
    else
    {
        int packet_samples =
            opus_packet_get_nb_samples((const unsigned char *)sample_data, sample_length, 48000);

        if (packet_samples > 0)
        {
            if (audio_state.packet_samples_min == 0 ||
                packet_samples < (int)audio_state.packet_samples_min)
                audio_state.packet_samples_min = (uint32_t)packet_samples;
            if (packet_samples > (int)audio_state.packet_samples_max)
                audio_state.packet_samples_max = (uint32_t)packet_samples;
            if (packet_samples != audio_state.samples_per_frame)
                ++audio_state.packet_sample_mismatches;
        }
    }
    decode_capacity = sample_data ? AUDIO_DECODE_MAX_FRAMES : audio_state.samples_per_frame;
    started = monotonic_us();
    decoded = opus_multistream_decode(
        audio_state.decoder, sample_data ? (const unsigned char *)sample_data : NULL,
        sample_data ? sample_length : 0, audio_state.decoded, decode_capacity, 0);
    elapsed = monotonic_us() - started;
    audio_state.decode_total_us += elapsed;
    audio_state.decode_timing.add(elapsed);
    if (elapsed > audio_state.decode_max_us)
        audio_state.decode_max_us = elapsed;
    if (decoded <= 0)
    {
        ++audio_state.decode_errors;
        return;
    }

    pcm_samples = (uint32_t)decoded * (uint32_t)audio_state.channels;
    for (uint32_t i = 0; i < pcm_samples; ++i)
    {
        int32_t magnitude = audio_state.decoded[i];

        if (magnitude < 0)
            magnitude = -magnitude;
        if (magnitude != 0)
            ++audio_state.nonzero_samples;
        if ((uint32_t)magnitude > audio_state.peak_sample)
            audio_state.peak_sample = (uint32_t)magnitude;
    }
    audio_state.decoded_frames += (uint32_t)decoded;
    const int pending_ms = LiGetPendingAudioDuration();
    if (pending_ms > 0 && static_cast<uint32_t>(pending_ms) > audio_state.pending_ms_high_water)
        audio_state.pending_ms_high_water = static_cast<uint32_t>(pending_ms);
    if (moonlight::discard_audio_backlog(pending_ms, PROSPEROLIGHT_AUDIO_MAX_BACKLOG_MS))
    {
        // Match Moonlight Qt's decode-then-discard policy, also retiring any PCM
        // we already buffered. Preserve Opus state, channel layout and PLC.
        ++audio_state.catchup_packets;
        audio_state.catchup_frames += static_cast<uint32_t>(decoded) + audio_state.ring_count;
        audio_state.ring_head = audio_state.ring_tail = audio_state.ring_count = 0;
        return;
    }
    audio_ring_push(&audio_state, audio_state.decoded, (uint32_t)decoded);
    if (audio_state.ring_count > audio_state.ring_high_water)
        audio_state.ring_high_water = audio_state.ring_count;
    while (audio_state.ring_count >= AUDIO_GRAIN_FRAMES)
    {
        int result;

        audio_ring_pop(&audio_state, audio_state.output, AUDIO_GRAIN_FRAMES);
        started = monotonic_us();
        result = sceAudioOutOutput(audio_state.handle, audio_state.output);
        elapsed = monotonic_us() - started;
        audio_state.output_total_us += elapsed;
        audio_state.output_timing.add(elapsed);
        if (elapsed > audio_state.output_max_us)
            audio_state.output_max_us = elapsed;
        ++audio_state.output_calls;
        if (result < 0)
        {
            ++audio_state.output_errors;
            break;
        }
    }
}

static AUDIO_RENDERER_CALLBACKS moonlight_audio_callbacks = {
    .init = ps5_audio_init,
    .start = nullptr,
    .stop = nullptr,
    .cleanup = ps5_audio_cleanup,
    .decodeAndPlaySample = ps5_audio_sample,
    .capabilities = CAPABILITY_SUPPORTS_ARBITRARY_AUDIO_DURATION,
};

static int16_t controller_axis(uint8_t value, int inverted)
{
    int32_t axis = ((int32_t)value - 128) * 256;

    if (inverted)
        axis = -axis;
    if (axis > INT16_MAX)
        axis = INT16_MAX;
    if (axis < INT16_MIN)
        axis = INT16_MIN;
    return (int16_t)axis;
}

static int ps5_controller_open(ps5_controller_state_t *state)
{
    for (unsigned attempt = 0; attempt < PS5_PAD_OPEN_ATTEMPTS; ++attempt)
    {
        state->handle = scePadOpen(state->user_id, 0, 0, NULL);
        if (state->handle >= 0)
            return 0;
        if (attempt + 1 < PS5_PAD_OPEN_ATTEMPTS)
            sceKernelUsleep(PS5_PAD_OPEN_RETRY_US);
    }
    return state->handle;
}

static int ps5_controller_init(ps5_controller_state_t *state)
{
    int32_t user_id = -1;

    memset(state, 0, sizeof(*state));
    state->user_id = -1;
    state->handle = -1;
    state->arrival_result = -1;
    state->removal_result = -1;
    state->user_service_result = sceUserServiceInitialize(NULL);
    state->user_result = sceUserServiceGetInitialUser(&user_id);
    state->user_id = user_id;
    if (state->user_result < 0)
        return state->user_result;
    state->pad_init_result = scePadInit();
    const int result = ps5_controller_open(state);
    return state->pad_init_result < 0 && result != 0 ? state->pad_init_result : result;
}

static int ps5_keyboard_has_key(const ps5_keyboard_state_t *state, uint16_t key)
{
    for (size_t index = 0; index < sizeof(state->keys) / sizeof(state->keys[0]); ++index)
    {
        if (state->keys[index] == key)
            return 1;
    }
    return 0;
}

static void ps5_physical_send_key(ps5_physical_input_state_t *state, uint16_t usage, int down,
                                  uint32_t modifiers)
{
    const auto mapping = prosperolight::physical_input::MapKey(usage);

    if (!mapping)
        return;
    const int result = LiSendKeyboardEvent2(
        (short)(UINT16_C(0x8000) | mapping.virtual_key), down ? KEY_ACTION_DOWN : KEY_ACTION_UP,
        (char)prosperolight::physical_input::MoonlightModifiers(modifiers), (char)mapping.flags);
    if (result != 0)
        ++state->keyboard_send_errors;
    else
        ++state->keyboard_events;
}

static void ps5_physical_process_keyboard(ps5_physical_input_state_t *state,
                                          ps5_keyboard_state_t *previous,
                                          const ps5_keyboard_state_t *sample)
{
    ps5_keyboard_state_t neutral = {};
    const ps5_keyboard_state_t *current =
        sample->connected && !sample->intercepted ? sample : &neutral;
    const uint32_t changed_modifiers = previous->modifiers ^ current->modifiers;

    for (uint32_t bit = 1; bit <= prosperolight::physical_input::kRightMeta; bit <<= 1)
    {
        if (changed_modifiers & bit)
            ps5_physical_send_key(state, prosperolight::physical_input::ModifierUsage(bit),
                                  (current->modifiers & bit) != 0, current->modifiers);
    }
    for (size_t index = 0; index < sizeof(previous->keys) / sizeof(previous->keys[0]); ++index)
    {
        const uint16_t key = previous->keys[index];

        if (key != 0 && (key < 224 || key > 231) && !ps5_keyboard_has_key(current, key))
            ps5_physical_send_key(state, key, 0, current->modifiers);
    }
    for (size_t index = 0; index < sizeof(current->keys) / sizeof(current->keys[0]); ++index)
    {
        const uint16_t key = current->keys[index];

        if (key != 0 && (key < 224 || key > 231) && !ps5_keyboard_has_key(previous, key))
            ps5_physical_send_key(state, key, 1, current->modifiers);
    }
    *previous = *current;
}

static void ps5_physical_release_mouse_buttons(ps5_physical_input_state_t *state,
                                               uint32_t *pressed_buttons)
{
    static const int moonlight_buttons[] = {BUTTON_LEFT, BUTTON_RIGHT, BUTTON_MIDDLE, BUTTON_X1,
                                            BUTTON_X2};

    for (uint32_t bit = 1, index = 0; index < 5; bit <<= 1, ++index)
    {
        if ((*pressed_buttons & bit) == 0)
            continue;
        if (LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, moonlight_buttons[index]) != 0)
            ++state->mouse_send_errors;
        else
            ++state->mouse_button_events;
    }
    *pressed_buttons = 0;
}

static void ps5_physical_process_mouse(ps5_physical_input_state_t *state,
                                       uint32_t *previous_buttons, const ps5_mouse_data_t *sample)
{
    static const int moonlight_buttons[] = {BUTTON_LEFT, BUTTON_RIGHT, BUTTON_MIDDLE, BUTTON_X1,
                                            BUTTON_X2};
    const int usable = sample->connected && (sample->buttons & UINT32_C(0x80000000)) == 0;
    const uint32_t current_buttons = usable ? sample->buttons & UINT32_C(0x1f) : 0;

    if (!usable)
    {
        ps5_physical_release_mouse_buttons(state, previous_buttons);
        return;
    }
    if (sample->x_axis != 0 || sample->y_axis != 0)
    {
        if (LiSendMouseMoveEvent(prosperolight::physical_input::ClampMotion(sample->x_axis),
                                 prosperolight::physical_input::ClampMotion(sample->y_axis)) != 0)
            ++state->mouse_send_errors;
        else
            ++state->mouse_motion_events;
    }
    const uint32_t changed_buttons = *previous_buttons ^ current_buttons;
    for (uint32_t bit = 1, index = 0; index < 5; bit <<= 1, ++index)
    {
        if ((changed_buttons & bit) == 0)
            continue;
        if (LiSendMouseButtonEvent((current_buttons & bit) ? BUTTON_ACTION_PRESS
                                                           : BUTTON_ACTION_RELEASE,
                                   moonlight_buttons[index]) != 0)
            ++state->mouse_send_errors;
        else
            ++state->mouse_button_events;
    }
    *previous_buttons = current_buttons;
    if (sample->wheel != 0)
    {
        if (LiSendScrollEvent(prosperolight::physical_input::ClampScroll(sample->wheel)) != 0)
            ++state->mouse_send_errors;
        else
            ++state->mouse_scroll_events;
    }
    if (sample->tilt != 0)
    {
        if (LiSendHScrollEvent(prosperolight::physical_input::ClampScroll(sample->tilt)) != 0)
            ++state->mouse_send_errors;
        else
            ++state->mouse_scroll_events;
    }
}

static int ps5_physical_input_init(ps5_physical_input_state_t *state, int32_t user_id)
{
    ps5_mouse_open_param_t mouse_parameter = {};
    uint64_t keyboard_parameter = 0;

    memset(state, 0, sizeof(*state));
    state->initialization_attempted = 1;
    state->keyboard_module_result = -1;
    state->keyboard_unload_result = -1;
    state->keyboard_init_result = -1;
    state->keyboard_open_result = -1;
    state->keyboard_close_result = -1;
    state->mouse_module_result = -1;
    state->mouse_unload_result = -1;
    state->mouse_init_result = -1;
    state->mouse_open_result = -1;
    state->mouse_close_result = -1;
    for (size_t index = 0;
         index < sizeof(state->keyboard_handles) / sizeof(state->keyboard_handles[0]); ++index)
        state->keyboard_handles[index] = -1;
    for (size_t index = 0; index < sizeof(state->mouse_handles) / sizeof(state->mouse_handles[0]);
         ++index)
        state->mouse_handles[index] = -1;

    state->keyboard_module_result =
        prosperolight::native_modules::Result(prosperolight::native_modules::keyboard);
    if (state->keyboard_module_result >= 0)
        state->keyboard_init_result = sceKeyboardInit();
    if (state->keyboard_init_result >= 0)
    {
        for (size_t index = 0;
             index < sizeof(state->keyboard_handles) / sizeof(state->keyboard_handles[0]); ++index)
        {
            const int32_t handle = sceKeyboardOpen(user_id, 0, (int32_t)index, &keyboard_parameter);
            if (handle < 0)
                continue;
            state->keyboard_handles[index] = handle;
            if (state->keyboard_open_result < 0)
                state->keyboard_open_result = handle;
            ++state->keyboard_handle_count;
        }
    }

    state->mouse_module_result =
        prosperolight::native_modules::Result(prosperolight::native_modules::mouse);
    if (state->mouse_module_result >= 0)
        state->mouse_init_result = sceMouseInit();
    if (state->mouse_init_result >= 0)
    {
        for (size_t index = 0;
             index < sizeof(state->mouse_handles) / sizeof(state->mouse_handles[0]); ++index)
        {
            const int32_t handle = sceMouseOpen(user_id, 0, (int32_t)index, &mouse_parameter);
            if (handle < 0)
                continue;
            state->mouse_handles[index] = handle;
            if (state->mouse_open_result < 0)
                state->mouse_open_result = handle;
            ++state->mouse_handle_count;
        }
    }
    return state->keyboard_handle_count || state->mouse_handle_count ? 0 : -1;
}

static void ps5_physical_input_poll(ps5_physical_input_state_t *state)
{
    for (size_t slot = 0;
         slot < sizeof(state->keyboard_handles) / sizeof(state->keyboard_handles[0]); ++slot)
    {
        if (state->keyboard_handles[slot] < 0)
            continue;
        int count = sceKeyboardRead(state->keyboard_handles[slot], state->keyboard_samples_batch,
                                    (int32_t)(sizeof(state->keyboard_samples_batch) /
                                              sizeof(state->keyboard_samples_batch[0])));

        ++state->keyboard_polls;
        if (count < 0)
            ++state->keyboard_read_errors;
        if (count <= 0)
            continue;
        if ((size_t)count >
            sizeof(state->keyboard_samples_batch) / sizeof(state->keyboard_samples_batch[0]))
            count = (int)(sizeof(state->keyboard_samples_batch) /
                          sizeof(state->keyboard_samples_batch[0]));
        for (int index = 0; index < count; ++index)
            ps5_physical_process_keyboard(state, &state->keyboards[slot],
                                          &state->keyboard_samples_batch[index]);
    }
    for (size_t slot = 0; slot < sizeof(state->mouse_handles) / sizeof(state->mouse_handles[0]);
         ++slot)
    {
        if (state->mouse_handles[slot] < 0)
            continue;
        int count = sceMouseRead(
            state->mouse_handles[slot], state->mouse_samples_batch,
            (int32_t)(sizeof(state->mouse_samples_batch) / sizeof(state->mouse_samples_batch[0])));

        ++state->mouse_polls;
        if (count < 0)
            ++state->mouse_read_errors;
        else
        {
            if ((size_t)count >
                sizeof(state->mouse_samples_batch) / sizeof(state->mouse_samples_batch[0]))
                count = (int)(sizeof(state->mouse_samples_batch) /
                              sizeof(state->mouse_samples_batch[0]));
            state->mouse_samples += (uint32_t)count;
            for (int index = 0; index < count; ++index)
                ps5_physical_process_mouse(state, &state->mouse_buttons[slot],
                                           &state->mouse_samples_batch[index]);
        }
    }
}

static void ps5_physical_input_stop(ps5_physical_input_state_t *state)
{
    const ps5_keyboard_state_t empty_keyboard = {};

    for (size_t index = 0; index < sizeof(state->keyboards) / sizeof(state->keyboards[0]); ++index)
        ps5_physical_process_keyboard(state, &state->keyboards[index], &empty_keyboard);
    for (size_t index = 0; index < sizeof(state->mouse_buttons) / sizeof(state->mouse_buttons[0]);
         ++index)
        ps5_physical_release_mouse_buttons(state, &state->mouse_buttons[index]);
}

static void ps5_physical_input_shutdown(ps5_physical_input_state_t *state)
{
    if (!state->initialization_attempted)
        return;
    for (size_t index = 0;
         index < sizeof(state->keyboard_handles) / sizeof(state->keyboard_handles[0]); ++index)
    {
        if (state->keyboard_handles[index] < 0)
            continue;
        state->keyboard_close_result = sceKeyboardClose(state->keyboard_handles[index]);
        state->keyboard_handles[index] = -1;
    }
    for (size_t index = 0; index < sizeof(state->mouse_handles) / sizeof(state->mouse_handles[0]);
         ++index)
    {
        if (state->mouse_handles[index] < 0)
            continue;
        state->mouse_close_result = sceMouseClose(state->mouse_handles[index]);
        state->mouse_handles[index] = -1;
    }
    if (state->mouse_module_result == 0)
        state->mouse_unload_result = 0; // Process-owned module remains loaded.
    if (state->keyboard_module_result == 0)
        state->keyboard_unload_result = 0; // Process-owned module remains loaded.
    state->initialization_attempted = 0;
}

static ps5_pad_sample_t *ps5_controller_newest_sample(ps5_controller_state_t *state, int count)
{
    ps5_pad_sample_t *newest = &state->sample_batch[0];

    for (int index = 1; index < count; ++index)
    {
        if (state->sample_batch[index].timestamp_us > newest->timestamp_us)
            newest = &state->sample_batch[index];
    }
    return newest;
}

static controller_event_t ps5_controller_map_sample(const ps5_pad_sample_t *sample, int neutral)
{
    controller_event_t event = {};

    if (neutral)
        return event;
    if (sample->buttons & PS5_PAD_BUTTON_UP)
        event.buttons |= UP_FLAG;
    if (sample->buttons & PS5_PAD_BUTTON_DOWN)
        event.buttons |= DOWN_FLAG;
    if (sample->buttons & PS5_PAD_BUTTON_LEFT)
        event.buttons |= LEFT_FLAG;
    if (sample->buttons & PS5_PAD_BUTTON_RIGHT)
        event.buttons |= RIGHT_FLAG;
    if (sample->buttons & PS5_PAD_BUTTON_CROSS)
        event.buttons |= A_FLAG;
    if (sample->buttons & PS5_PAD_BUTTON_CIRCLE)
        event.buttons |= B_FLAG;
    if (sample->buttons & PS5_PAD_BUTTON_SQUARE)
        event.buttons |= X_FLAG;
    if (sample->buttons & PS5_PAD_BUTTON_TRIANGLE)
        event.buttons |= Y_FLAG;
    if (sample->buttons & PS5_PAD_BUTTON_L1)
        event.buttons |= LB_FLAG;
    if (sample->buttons & PS5_PAD_BUTTON_R1)
        event.buttons |= RB_FLAG;
    if (sample->buttons & PS5_PAD_BUTTON_L3)
        event.buttons |= LS_CLK_FLAG;
    if (sample->buttons & PS5_PAD_BUTTON_R3)
        event.buttons |= RS_CLK_FLAG;
    if (sample->buttons & 0x1u)
        event.buttons |= BACK_FLAG;
    if (sample->buttons & PS5_PAD_BUTTON_OPTIONS)
        event.buttons |= PLAY_FLAG;
    if (sample->buttons & PS5_PAD_BUTTON_TOUCH_PAD)
        event.buttons |= TOUCHPAD_FLAG;
    event.left_trigger = sample->left_trigger;
    event.right_trigger = sample->right_trigger;
    event.left_x = controller_axis(sample->left_x, 0);
    event.left_y = controller_axis(sample->left_y, 1);
    event.right_x = controller_axis(sample->right_x, 0);
    event.right_y = controller_axis(sample->right_y, 1);
    return event;
}

static void ps5_controller_send(ps5_controller_state_t *state, const controller_event_t *event)
{
    uint64_t now;
    int result;

    now = monotonic_us();
    if (state->last_event_us != 0 && !memcmp(event, &state->last_event, sizeof(*event)) &&
        now - state->last_event_us < CONTROLLER_KEEPALIVE_US)
        return;
    result = prosperolight::dualsense::SendPrimary(event->buttons, event->left_trigger,
                                                   event->right_trigger, event->left_x,
                                                   event->left_y, event->right_x, event->right_y);
    if (result != 0)
    {
        ++state->send_errors;
        return;
    }
    state->announced = (prosperolight::dualsense::ActiveMask() & 1) != 0;
    if (state->announced)
        state->arrival_result = 0;
    state->last_event = *event;
    state->last_event_us = now;
    ++state->events;
}

typedef struct mouse_button_mapping
{
    uint32_t pad_button;
    int mouse_button;
} mouse_button_mapping_t;

static const mouse_button_mapping_t mouse_button_mappings[] = {
    {PS5_PAD_BUTTON_CROSS, BUTTON_LEFT},    {PS5_PAD_BUTTON_CIRCLE, BUTTON_RIGHT},
    {PS5_PAD_BUTTON_SQUARE, BUTTON_MIDDLE}, {PS5_PAD_BUTTON_L1, BUTTON_X1},
    {PS5_PAD_BUTTON_R1, BUTTON_X2},
};

static void ps5_controller_mouse_buttons(ps5_controller_state_t *state, uint32_t previous,
                                         uint32_t current)
{
    for (size_t index = 0; index < sizeof(mouse_button_mappings) / sizeof(mouse_button_mappings[0]);
         ++index)
    {
        const mouse_button_mapping_t *mapping = &mouse_button_mappings[index];
        const int was_down = (previous & mapping->pad_button) != 0;
        const int is_down = (current & mapping->pad_button) != 0;
        int result;

        if (was_down == is_down)
            continue;
        result = LiSendMouseButtonEvent(is_down ? BUTTON_ACTION_PRESS : BUTTON_ACTION_RELEASE,
                                        mapping->mouse_button);
        if (result != 0)
            ++state->mouse_errors;
        else
            ++state->mouse_button_events;
        if (is_down)
            state->mouse_buttons_down |= mapping->pad_button;
        else
            state->mouse_buttons_down &= ~mapping->pad_button;
    }
}

static void ps5_controller_release_mouse_buttons(ps5_controller_state_t *state)
{
    ps5_controller_mouse_buttons(state, state->mouse_buttons_down, 0);
    state->last_mouse_buttons = 0;
}

static void ps5_controller_mouse_scroll(ps5_controller_state_t *state, uint32_t previous,
                                        uint32_t current)
{
    int result = 0;

    if ((current & PS5_PAD_BUTTON_UP) != 0 && (previous & PS5_PAD_BUTTON_UP) == 0)
        result = LiSendScrollEvent(1);
    else if ((current & PS5_PAD_BUTTON_DOWN) != 0 && (previous & PS5_PAD_BUTTON_DOWN) == 0)
        result = LiSendScrollEvent(-1);
    else if ((current & PS5_PAD_BUTTON_RIGHT) != 0 && (previous & PS5_PAD_BUTTON_RIGHT) == 0)
        result = LiSendHScrollEvent(1);
    else if ((current & PS5_PAD_BUTTON_LEFT) != 0 && (previous & PS5_PAD_BUTTON_LEFT) == 0)
        result = LiSendHScrollEvent(-1);
    else
        return;
    if (result != 0)
        ++state->mouse_errors;
    else
        ++state->mouse_scroll_events;
}

static void ps5_controller_mouse_motion(ps5_controller_state_t *state)
{
    const controller_event_t *event = &state->mouse_event;
    int32_t left_strength, right_strength;
    int16_t raw_x, raw_y, delta_x, delta_y;
    uint64_t now = monotonic_us();

    if (now < state->next_mouse_motion_us)
        return;
    state->next_mouse_motion_us = now + MOONLIGHT_MOUSE_EMULATION_POLL_US;
    left_strength = abs(event->left_x) + abs(event->left_y);
    right_strength = abs(event->right_x) + abs(event->right_y);
    if (left_strength > right_strength)
    {
        raw_x = event->left_x;
        raw_y = (int16_t)-event->left_y;
    }
    else
    {
        raw_x = event->right_x;
        raw_y = (int16_t)-event->right_y;
    }
    delta_x = moonlight_stream_mouse_axis_delta(raw_x);
    delta_y = moonlight_stream_mouse_axis_delta(raw_y);
    if (delta_x == 0 && delta_y == 0)
        return;
    if (LiSendMouseMoveEvent(delta_x, delta_y) != 0)
        ++state->mouse_errors;
    else
        ++state->mouse_motion_events;
}

static void ps5_controller_set_mouse_mode(ps5_controller_state_t *state, int enabled)
{
    const controller_event_t neutral_event = {};

    state->mouse_mode = enabled != 0;
    ++state->mouse_toggles;
    if (state->mouse_mode)
    {
        /* Keep Sunshine's virtual controller alive and release any gamepad
         * state before local mouse emulation takes ownership. */
        ps5_controller_send(state, &neutral_event);
        state->next_mouse_motion_us = monotonic_us() + MOONLIGHT_MOUSE_EMULATION_POLL_US;
        state->mouse_buttons_down = 0;
    }
    else
    {
        ps5_controller_release_mouse_buttons(state);
        memset(&state->mouse_event, 0, sizeof(state->mouse_event));
        /* Force the first restored gamepad sample through the deduplicator. */
        state->last_event_us = 0;
    }
    snprintf(notification.message, sizeof(notification.message),
             "ProsperoLight: %s mode enabled. Touchpad + Square switches to %s.",
             state->mouse_mode ? "Mouse" : "Controller",
             state->mouse_mode ? "controller" : "mouse");
    (void)sceKernelSendNotificationRequest(0, &notification, sizeof(notification), 0);
    (void)lan_http_report_text(notification.message);
}

static int ps5_send_windows_key(uint16_t virtual_key, int shifted)
{
    const short key = (short)(0x8000u | virtual_key);
    const char modifiers = shifted ? MODIFIER_SHIFT : 0;
    int result = LiSendKeyboardEvent(key, KEY_ACTION_DOWN, modifiers);

    if (result == 0)
        result = LiSendKeyboardEvent(key, KEY_ACTION_UP, modifiers);
    return result;
}

static void ps5_controller_set_keyboard_mode(ps5_controller_state_t *state, int enabled)
{
    static const controller_event_t neutral_event = {};

    state->keyboard_mode = enabled != 0;
    if (state->keyboard_mode)
    {
        if (state->mouse_mode)
            ps5_controller_set_mouse_mode(state, 0);
        ps5_controller_send(state, &neutral_event);
    }
    else
        state->last_event_us = 0;
    native_agc_set_keyboard_state(state->keyboard_mode, state->keyboard_selected,
                                  state->keyboard_shifted);
}

static void ps5_controller_activate_keyboard_key(ps5_controller_state_t *state)
{
    const moonlight_keyboard_key &key = moonlight_keyboard_keys[state->keyboard_selected];
    int result = 0;

    switch (key.action)
    {
    case moonlight_keyboard_action::shift:
        state->keyboard_shifted = !state->keyboard_shifted;
        break;
    case moonlight_keyboard_action::space:
    case moonlight_keyboard_action::backspace:
        result = ps5_send_windows_key(key.virtual_key, 0);
        break;
    case moonlight_keyboard_action::enter:
        result = ps5_send_windows_key(key.virtual_key, 0);
        ps5_controller_set_keyboard_mode(state, 0);
        return;
    case moonlight_keyboard_action::close:
        ps5_controller_set_keyboard_mode(state, 0);
        return;
    case moonlight_keyboard_action::key:
        result = ps5_send_windows_key(key.virtual_key, state->keyboard_shifted);
        break;
    }
    if (result != 0)
    {
        snprintf(notification.message, sizeof(notification.message),
                 "ProsperoLight: Could not send keyboard input.");
        (void)sceKernelSendNotificationRequest(0, &notification, sizeof(notification), 0);
        (void)lan_http_report_text(notification.message);
    }
}

static int pressed_edge(uint32_t current, uint32_t previous, uint32_t button)
{
    return (current & button) != 0 && (previous & button) == 0;
}

static void ps5_controller_keyboard_input(ps5_controller_state_t *state, uint32_t current,
                                          uint32_t previous)
{
    uint32_t previous_selected = state->keyboard_selected;
    int previous_shifted = state->keyboard_shifted;

    if (current & PS5_PAD_BUTTON_TOUCH_PAD)
        return;
    if (pressed_edge(current, previous, PS5_PAD_BUTTON_LEFT))
        state->keyboard_selected = moonlight_keyboard_move(state->keyboard_selected, -1, 0);
    else if (pressed_edge(current, previous, PS5_PAD_BUTTON_RIGHT))
        state->keyboard_selected = moonlight_keyboard_move(state->keyboard_selected, 1, 0);
    else if (pressed_edge(current, previous, PS5_PAD_BUTTON_UP))
        state->keyboard_selected = moonlight_keyboard_move(state->keyboard_selected, 0, -1);
    else if (pressed_edge(current, previous, PS5_PAD_BUTTON_DOWN))
        state->keyboard_selected = moonlight_keyboard_move(state->keyboard_selected, 0, 1);
    else if (pressed_edge(current, previous, PS5_PAD_BUTTON_CROSS))
        ps5_controller_activate_keyboard_key(state);
    else if (pressed_edge(current, previous, PS5_PAD_BUTTON_SQUARE))
        (void)ps5_send_windows_key(0x08, 0);
    else if (pressed_edge(current, previous, PS5_PAD_BUTTON_TRIANGLE))
        state->keyboard_shifted = !state->keyboard_shifted;
    else if (pressed_edge(current, previous, PS5_PAD_BUTTON_OPTIONS))
    {
        (void)ps5_send_windows_key(0x0d, 0);
        ps5_controller_set_keyboard_mode(state, 0);
        return;
    }
    else if (pressed_edge(current, previous, PS5_PAD_BUTTON_CIRCLE))
    {
        ps5_controller_set_keyboard_mode(state, 0);
        return;
    }
    if (state->keyboard_mode && (state->keyboard_selected != previous_selected ||
                                 state->keyboard_shifted != previous_shifted))
        native_agc_set_keyboard_state(1, state->keyboard_selected, state->keyboard_shifted);
}

static void ps5_controller_poll(ps5_controller_state_t *state)
{
    static const uint32_t hud_chord = PS5_PAD_BUTTON_TOUCH_PAD | PS5_PAD_BUTTON_R1;
    static const uint32_t keyboard_chord = PS5_PAD_BUTTON_TOUCH_PAD | PS5_PAD_BUTTON_TRIANGLE;
    static const uint32_t mouse_chord = PS5_PAD_BUTTON_TOUCH_PAD | PS5_PAD_BUTTON_SQUARE;
    static const controller_event_t neutral_event = {};
    int count;

    if (state->handle < 0)
        return;
    ++state->polls;
    count = scePadRead(state->handle, state->sample_batch, PS5_PAD_SAMPLE_CAPACITY);
    if (count < 0)
    {
        ++state->read_errors;
        return;
    }
    if (count == 0)
    {
        ++state->empty_reads;
        if (state->mouse_mode)
        {
            ps5_controller_mouse_motion(state);
            ps5_controller_send(state, &neutral_event);
        }
        else
            ps5_controller_send(state, &state->last_event);
        return;
    }
    state->samples += (uint32_t)count;
    if ((uint32_t)count > state->max_batch)
        state->max_batch = (uint32_t)count;

    {
        ps5_pad_sample_t *sample = ps5_controller_newest_sample(state, count);
        controller_event_t event;
        uint32_t raw_buttons = sample->buttons;
        uint32_t mouse_buttons;
        int mouse_toggle;
        int keyboard_toggle;
        int intercepted = (raw_buttons & PS5_PAD_BUTTON_INTERCEPTED) != 0;
        int neutral = !sample->connected || intercepted;

        if (!sample->connected)
            ++state->disconnected_samples;
        if (intercepted)
            ++state->intercepted_samples;
        if (!state->connected_count_valid || sample->connected_count != state->connected_count)
        {
            state->connected_count = sample->connected_count;
            state->connected_count_valid = 1;
            state->last_raw_buttons = 0;
            state->last_event_us = 0;
            ps5_controller_release_mouse_buttons(state);
        }
        prosperolight::dualsense::PrimarySample(
            *sample, neutral || state->mouse_mode || state->keyboard_mode ||
                         moonlight_stream_mouse_toggle_requested(raw_buttons) ||
                         moonlight_stream_keyboard_requested(raw_buttons) ||
                         moonlight_stream_hud_toggle_requested(raw_buttons));
        if (neutral)
            raw_buttons = 0;
        mouse_toggle = moonlight_stream_mouse_toggle_requested(raw_buttons) &&
                       !moonlight_stream_mouse_toggle_requested(state->last_raw_buttons);
        keyboard_toggle = moonlight_stream_keyboard_requested(raw_buttons) &&
                          !moonlight_stream_keyboard_requested(state->last_raw_buttons);

        if (moonlight_stream_disconnect_requested(raw_buttons))
        {
            state->requested_stop = 1;
            return;
        }
        if (moonlight_stream_hud_toggle_requested(raw_buttons) &&
            !moonlight_stream_hud_toggle_requested(state->last_raw_buttons))
            native_agc_set_hud_enabled(!native_agc_hud_enabled());
        if (moonlight_stream_hud_toggle_requested(raw_buttons))
            sample->buttons &= ~hud_chord;
        if (keyboard_toggle)
            ps5_controller_set_keyboard_mode(state, !state->keyboard_mode);
        if (moonlight_stream_keyboard_requested(raw_buttons))
            sample->buttons &= ~keyboard_chord;
        if (moonlight_stream_mouse_toggle_requested(raw_buttons))
            sample->buttons &= ~mouse_chord;

        if (state->keyboard_mode)
        {
            if (!keyboard_toggle)
                ps5_controller_keyboard_input(state, raw_buttons, state->last_raw_buttons);
            state->last_raw_buttons = raw_buttons;
            ps5_controller_send(state, &neutral_event);
            return;
        }

        event = ps5_controller_map_sample(sample, neutral);
        if (!neutral)
            event.buttons =
                prosperolight::dualsense::RemoteShortcuts(sample->buttons, event.buttons);
        mouse_buttons = neutral ? 0 : sample->buttons;
        state->last_raw_buttons = raw_buttons;
        state->observed_raw_buttons |= raw_buttons;
        state->observed_moonlight_buttons |= (uint32_t)event.buttons;
        if (event.buttons || event.left_trigger || event.right_trigger || event.left_x ||
            event.left_y || event.right_x || event.right_y)
            ++state->nonneutral_samples;
        if (state->mouse_mode)
        {
            if (mouse_toggle)
            {
                ps5_controller_set_mouse_mode(state, 0);
                ps5_controller_send(state, &event);
            }
            else
            {
                ps5_controller_mouse_buttons(state, state->last_mouse_buttons, mouse_buttons);
                ps5_controller_mouse_scroll(state, state->last_mouse_buttons, mouse_buttons);
                state->last_mouse_buttons = mouse_buttons;
                state->mouse_event = event;
                ps5_controller_mouse_motion(state);
                ps5_controller_send(state, &neutral_event);
            }
        }
        else
        {
            ps5_controller_send(state, &event);
            if (mouse_toggle)
            {
                state->last_mouse_buttons = mouse_buttons;
                state->mouse_event = event;
                ps5_controller_set_mouse_mode(state, 1);
            }
        }
    }
}

static void ps5_controller_stop(ps5_controller_state_t *state)
{
    ps5_controller_release_mouse_buttons(state);
    state->removal_result = prosperolight::dualsense::Stop();
    if (state->removal_result != 0)
        ++state->send_errors;
    state->announced = 0;
}

static void ps5_controller_shutdown(ps5_controller_state_t *state)
{
    prosperolight::dualsense::Shutdown();
    native_agc_set_keyboard_state(0, 0, 0);
    if (state->handle >= 0)
    {
        (void)scePadClose(state->handle);
        state->handle = -1;
    }
    if (state->user_service_result == 0)
        (void)sceUserServiceTerminate();
}

static void connection_stage_starting(int stage)
{
    connection_loading_state_t *loading =
        std::atomic_load_explicit(&active_connection_loading, std::memory_order_acquire);
    char receipt[256];

    if (loading)
        std::atomic_store_explicit(&loading->connection_pending, 1, std::memory_order_release);
    snprintf(receipt, sizeof(receipt), "Moonlight connection stage %d: %s", stage,
             LiGetStageName(stage));
    (void)lan_http_report_text(receipt);
}

static void connection_stage_complete(int stage)
{
    char receipt[256];

    snprintf(receipt, sizeof(receipt), "Moonlight connection stage complete %d: %s", stage,
             LiGetStageName(stage));
    (void)lan_http_report_text(receipt);
}

static char protocol_error[192]{};

static void connection_log(const char *format, ...)
{
    char message[320];
    char receipt[352];
    va_list arguments;
    size_t length;

    va_start(arguments, format);
    (void)vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    length = strlen(message);
    while (length && (message[length - 1] == '\n' || message[length - 1] == '\r'))
        message[--length] = '\0';
    if (strstr(message, "Incompatible PyroWave bitstream") ||
        strstr(message, "Selected PyroWave profile is unsupported"))
        snprintf(protocol_error, sizeof(protocol_error), "%s", message);
    // The only signal moonlight-common-c gives when it discards its frame queue
    // because decoding fell behind. Rare lines; never per packet.
    if (strstr(message, "Video decode unit queue overflow"))
    {
        std::atomic_fetch_add_explicit(&video_queue_overflows, 1u, std::memory_order_relaxed);
        std::atomic_fetch_add_explicit(&video_recovery_epoch, 1u, std::memory_order_relaxed);
    }
    else if (strstr(message, "Unrecoverable frame"))
        std::atomic_fetch_add_explicit(&video_unrecoverable_frames, 1u, std::memory_order_relaxed);
    snprintf(receipt, sizeof(receipt), "Moonlight[C] %s", message);
    (void)lan_http_report_text(receipt);
}

static void connection_stage_failed(int stage, int error)
{
    char receipt[256];
    connection_error = error;
    connection_terminated = 1;
    std::atomic_store_explicit(&connection_failed_stage, stage, std::memory_order_relaxed);
    snprintf(receipt, sizeof(receipt), "Moonlight connection failed: stage=%d %s error=%08x", stage,
             LiGetStageName(stage), (uint32_t)error);
    (void)lan_http_report_text(receipt);
}

static void connection_started(void)
{
    (void)lan_http_report_text("Moonlight connection started");
}

static void connection_ended(int error)
{
    char receipt[256];
    connection_error = error;
    connection_terminated = 1;
    snprintf(receipt, sizeof(receipt), "Moonlight connection terminated: error=%08x",
             (uint32_t)error);
    (void)lan_http_report_text(receipt);
}

static void connection_set_hdr_mode(bool enabled)
{
    SS_HDR_METADATA metadata = {};
    char receipt[384];
    int metadata_valid = 0;

    std::atomic_store_explicit(&host_hdr_active, enabled ? 1u : 0u, std::memory_order_relaxed);
    std::atomic_fetch_add_explicit(&host_hdr_transitions, 1u, std::memory_order_relaxed);
    if (enabled)
        metadata_valid = LiGetHdrMetadata(&metadata) ? 1 : 0;
#if PROSPEROLIGHT_PYROWAVE
    prosperolight::pyrowave::set_hdr_mode(enabled, metadata_valid ? &metadata : nullptr);
#endif
    snprintf(receipt, sizeof(receipt),
             "Moonlight HDR mode: active=%u transitions=%u metadata=%u max_nits=%u min_1e4_nits=%u "
             "max_cll=%u max_fall=%u",
             enabled ? 1u : 0u,
             std::atomic_load_explicit(&host_hdr_transitions, std::memory_order_relaxed),
             metadata_valid ? 1u : 0u, metadata.maxDisplayLuminance, metadata.minDisplayLuminance,
             metadata.maxContentLightLevel, metadata.maxFrameAverageLightLevel);
    (void)lan_http_report_text(receipt);
}

static CONNECTION_LISTENER_CALLBACKS moonlight_connection_callbacks = {
    .stageStarting = connection_stage_starting,
    .stageComplete = connection_stage_complete,
    .stageFailed = connection_stage_failed,
    .connectionStarted = connection_started,
    .connectionTerminated = connection_ended,
    .logMessage = connection_log,
    .rumble = prosperolight::dualsense::Rumble,
    .connectionStatusUpdate = nullptr,
    .setHdrMode = connection_set_hdr_mode,
    .rumbleTriggers = prosperolight::dualsense::RumbleTriggers,
    .setMotionEventState = prosperolight::dualsense::MotionState,
    .setControllerLED = prosperolight::dualsense::Led,
    .setAdaptiveTriggers = prosperolight::dualsense::AdaptiveTriggers,
    .controllerHaptics = prosperolight::dualsense::Haptics,
};

static void nvhttp_log_sink(const char *message)
{
    (void)lan_http_report_text(message);
}

static int prepare_native_session(client_identity_t *identity, gs_server_t *server,
                                  STREAM_CONFIGURATION *configuration,
                                  const moonlight::ResolvedStreamProfile &profile, int gamepad_mask,
                                  const char *host, uint16_t host_port, const char *app_name,
                                  int requested_app_id, prosperolight::HostPreferences &preferences,
                                  const moonlight_session_overrides_t *overrides)
{
    app_entry_t *apps = NULL;
    app_entry_t *app;
    char target_name[64] = {};
    int target_id = 0;
    int app_count = 0;
    int resume_requested = 0;
    int result;

    gs_log_set_sink(nvhttp_log_sink);
    result = overrides && overrides->enabled
                 ? identity_init_existing(identity, MOONLIGHT_IDENTITY_DIRECTORY)
                 : identity_init(identity, MOONLIGHT_IDENTITY_DIRECTORY);
    if (result != GS_OK)
        return result;
    http_init(identity, 1);
    result = gs_init(server, identity, host, host_port);
    snprintf(notification.message, sizeof(notification.message),
             "Native NVHTTP serverinfo: rc=%08x paired=%u app=%s https=%u codec=%08x error=%s",
             (uint32_t)result, server->paired, server->app_version, server->https_port,
             (uint32_t)server->server_codec_mode_support, gs_error ? gs_error : "");
    (void)lan_http_report_text(notification.message);
    if (result != GS_OK)
        return result;

    if (!server->paired)
    {
        gs_error = "Pair this client from the launcher";
        return GS_WRONG_STATE;
    }
    if (!(server->server_codec_mode_support & profile.capability))
    {
        static char unsupported[192];
        snprintf(unsupported, sizeof(unsupported), "Selected PC does not advertise %s",
                 profile.name);
        gs_error = unsupported;
        return GS_NOT_SUPPORTED_MODE;
    }

    result = gs_applist(server, &apps);
    if (result != GS_OK)
        return result;
    for (app = apps; app; app = app->next)
    {
        ++app_count;
        if ((requested_app_id > 0 && app->id == requested_app_id) ||
            (requested_app_id <= 0 && app->name && !strcmp(app->name, app_name)))
        {
            target_id = app->id;
            snprintf(target_name, sizeof(target_name), "%s", app->name ? app->name : app_name);
        }
    }
    snprintf(notification.message, sizeof(notification.message),
             "Native NVHTTP applist: rc=00000000 apps=%d target=%s id=%d", app_count,
             target_name[0] ? target_name : app_name, target_id);
    (void)lan_http_report_text(notification.message);
    xml_applist_free(apps);
    if (!target_id)
    {
        gs_error = "Requested Sunshine app was not found";
        return GS_INVALID;
    }
    {
        moonlight_config_host_t settings_host{};
        snprintf(settings_host.address, sizeof(settings_host.address), "%s", server->address);
        snprintf(settings_host.unique_id, sizeof(settings_host.unique_id), "%s", server->unique_id);
        settings_host.http_port = server->http_port;
        if (overrides && overrides->enabled)
        {
            preferences = {overrides->extensions != 0, overrides->host_vrr,
                           overrides->display,         overrides->scale,
                           overrides->optimize != 0,   overrides->mute_host != 0,
                           overrides->quit_host != 0};
        }
        else
            preferences = prosperolight::host_preferences(&settings_host);
        if (preferences.extensions && preferences.display == 2 &&
            (server->host_capabilities & 3u) != 3u)
        {
            gs_error = "Host virtual display is unavailable or its driver is not ready";
            return GS_FAILED;
        }
    }
#if PROSPEROLIGHT_TEST_LAB
    if (prosperolight::test_lab::active())
    {
        // Use the existing paired identity. A test must launch a fresh session,
        // never resume a Desktop stream left by another client.
        result = gs_quit_app(server);
        snprintf(notification.message, sizeof(notification.message),
                 "Test Lab paired host cleanup: cancel_rc=%08x error=%s", (uint32_t)result,
                 gs_error ? gs_error : "");
        (void)lan_http_report_text(notification.message);
        if (result != GS_OK)
        {
            prosperolight::test_lab::context().fail("HOST_CLEANUP_REJECTED");
            return result;
        }
        result = gs_refresh_paired_status(server);
        if (result != GS_OK || server->current_game != 0)
        {
            prosperolight::test_lab::context().fail("HOST_CLEANUP_UNCONFIRMED");
            return result != GS_OK ? result : GS_WRONG_STATE;
        }
        prosperolight::test_lab::context().host_cleanup_confirmed();
    }
#endif
    if (server->current_game && server->current_game != target_id)
    {
        if (overrides && overrides->enabled)
        {
            gs_error = "HOST_BUSY: another host app is active";
            return GS_WRONG_STATE;
        }
        result = gs_quit_app(server);
        if (result != GS_OK)
            return result;
        // Starting another app while the PC still holds the first one is refused with no
        // reason given: ask what it runs now and say so here.
        if (gs_refresh_status(server) == GS_OK && server->current_game != 0)
        {
            gs_error = "The PC kept its running app; close it on the PC or restart Sunshine";
            return GS_FAILED;
        }
        server->current_game = 0;
    }
    resume_requested = server->current_game == target_id;
    gamepad_mask = prosperolight::dualsense::ActiveMask() & 0x0f;
    {
        const gs_host_options_t extension_options{
            preferences.extensions, static_cast<int>(preferences.vrr) - 1,
            static_cast<int>(preferences.display) - 1, preferences.scale,
            static_cast<uint16_t>(gamepad_mask)};
        result = gs_start_app(server, configuration, target_id, preferences.optimize,
                              !preferences.mute_host, gamepad_mask, &extension_options);
    }
    snprintf(notification.message, sizeof(notification.message),
             "Native NVHTTP launch: rc=%08x action=%s target=%s id=%d gamepads=%x rtsp=%s error=%s",
             (uint32_t)result, resume_requested ? "resume" : "launch",
             target_name[0] ? target_name : app_name, target_id, gamepad_mask,
             server->rtsp_session_url, gs_error ? gs_error : "");
    (void)lan_http_report_text(notification.message);
    if (result != GS_OK || !server->rtsp_session_url[0])
    {
        if (result == GS_OK)
        {
            gs_error = "Sunshine launch omitted the RTSP session URL";
            result = GS_INVALID;
        }
        return result;
    }
    return GS_OK;
}

int moonlight_stream_run(const moonlight_stream_options_t *options,
                         moonlight_stream_metrics_t *metrics)
{
    if (presentation_faulted)
    {
        if (metrics)
        {
            *metrics = {};
            metrics->result = -5;
            snprintf(metrics->error, sizeof(metrics->error),
                     "GPU presentation timed out. Restart ProsperoLight before streaming again.");
        }
        return -5;
    }
    videodec2_decoder_config_t config;
    ps5_thread_placement_t placement = {};
    videodec2_compute_config_t compute_config = {};
    videodec2_compute_memory_t compute_memory = {};
    native_renderer_state_t renderer = {};
    connection_loading_state_t loading = {};
    ps5_controller_state_t controller{};
    ps5_physical_input_state_t physical_input{};
    client_identity_t client_identity;
    gs_server_t gs_server;
    STREAM_CONFIGURATION stream_config;
    void *compute_queue = NULL;
    int64_t compute_start = -1;
    int64_t direct_memory_limit;
    size_t compute_size = 0;
    int32_t result;
    int32_t present_cleanup_result = -1;
    int32_t delete_result = -1;
    int32_t release_compute_result = -1;
    int32_t unload_result = -1;
    uint64_t live_elapsed_us = 0;
    uint64_t first_frame_wait_start_us = 0;
    uint64_t last_input_poll_us = 0;
    moonlight::TimingHistogram input_intervals;
    int connection_result = -1;
    int connection_active = 0;
    int stream_started = 0;
    int identity_initialized = 0;
    int session_started = 0;
    prosperolight::HostPreferences session_preferences;
    int controller_result = -1;
    int controller_ready = 0;
    int physical_input_ready = 0;
    int first_frame_timed_out = 0;
    int terminated = 0;
    int reported_error = 0;
    int user_stop = 0;
    uint32_t presented = 0;
    uint64_t main_cpu_mask = 0;
    int main_mask_known = 0;
    int main_mask_changed = 0;
    char stream_error[192] = {};
    uint32_t synthetic_motion_events = 0;
    uint32_t synthetic_motion_errors = 0;
    uint64_t synthetic_motion_next_us = 0;
    const char *host = options && options->host && options->host[0] ? options->host : "";
    const uint16_t host_port = options ? options->host_port : 0;
    const char *app_name =
        options && options->app_name && options->app_name[0] ? options->app_name : "Desktop";
    const int app_id = options ? options->app_id : 0;
    const uint32_t bitrate_kbps = options && options->bitrate_kbps ? options->bitrate_kbps : 20000u;
    const uint32_t requested_fps = options ? options->stream_fps : MOONLIGHT_STREAM_FPS_60;
    const uint32_t stream_fps =
        requested_fps >= MOONLIGHT_STREAM_FPS_MIN && requested_fps <= MOONLIGHT_STREAM_FPS_MAX
            ? requested_fps
            : MOONLIGHT_STREAM_FPS_60;
    const uint32_t requested_audio =
        options ? options->audio_configuration : MOONLIGHT_AUDIO_STEREO;
    int audio_configuration = AUDIO_CONFIGURATION_STEREO;
    const auto profile = moonlight::resolve_stream_profile(
        options ? options->video_codec : MOONLIGHT_VIDEO_CODEC_H264,
        options ? options->chroma_sampling : MOONLIGHT_CHROMA_420, options && options->hdr_enabled);
    const bool use_pyrowave = profile.decoder_backend == moonlight::DecoderBackend::PyroWaveRadv;
#if PROSPEROLIGHT_TEST_LAB
    struct TestProbe
    {
        native_renderer_state_t *renderer;
        connection_loading_state_t *loading;
        ps5_controller_state_t *controller;
        bool pyrowave;
        std::atomic<bool> connected{false};
        std::atomic<bool> controller_initialized{false};
    } test_probe{&renderer, &loading, &controller, use_pyrowave};
    if (prosperolight::test_lab::active())
        prosperolight::test_lab::context().attach(
            [](void *data)
            {
                auto &p = *static_cast<TestProbe *>(data);
                prosperolight::test_lab::Observation observed;
                observed.connected = p.connected.load(std::memory_order_acquire);
                if (!observed.connected)
                    return observed;
#if PROSPEROLIGHT_PYROWAVE
                if (p.pyrowave)
                {
                    observed.unique_frames = prosperolight::pyrowave::completed_unique();
                    observed.output_valid = observed.unique_frames != 0;
                    return observed;
                }
#endif
                observed.unique_frames = p.renderer->presented.load(std::memory_order_relaxed);
                observed.output_valid =
                    observed.unique_frames != 0 && p.renderer->present_result.load() == 0;
                return observed;
            },
            [](void *data)
            {
                auto &p = *static_cast<TestProbe *>(data);
                if (p.controller_initialized.load(std::memory_order_acquire))
                    p.controller->requested_stop.store(1);
                http_interrupt();
                // The existing loading worker performs LiInterruptConnection;
                // after connection, the stream owner follows LiStopConnection.
                p.loading->cancel_requested.store(1);
            },
            &test_probe, *options);
#endif
    const uint32_t resolution =
        options ? options->stream_resolution : MOONLIGHT_STREAM_RESOLUTION_1080P;
    const unsigned stream_width = resolution == MOONLIGHT_STREAM_RESOLUTION_2160P   ? 3840
                                  : resolution == MOONLIGHT_STREAM_RESOLUTION_1440P ? 2560
                                                                                    : 1920;
    const unsigned stream_height = resolution == MOONLIGHT_STREAM_RESOLUTION_2160P   ? 2160
                                   : resolution == MOONLIGHT_STREAM_RESOLUTION_1440P ? 1440
                                                                                     : 1080;
    const native_video_mode_t *mode =
        use_pyrowave ? nullptr : find_video_mode(profile.codec, resolution, profile.hdr);
    const bool classic_pipeline =
        !options || options->decoder_pipeline != MOONLIGHT_DECODER_PIPELINE_ADAPTIVE;
    const uint32_t requested_depth = classic_pipeline ? 1u : (uint32_t)DECODER_PIPELINE_DEPTH;
    const uint32_t requested_cores = options && options->decoder_cores
                                         ? options->decoder_cores
                                         : MOONLIGHT_DECODER_CORES_DEFAULT;
    const uint32_t vsync_enabled = !options || options->vsync_enabled ? 1u : 0u;
    // Request slice parallelism; the best count depends on codec and workload.
    const uint32_t stream_slices = VIDEO_SLICES_PER_FRAME;

    renderer.owned_resources = make_decoder_resources();
    renderer.owned_input_start = renderer.owned_frame_start = -1;
    controller.user_service_result = -1;
    controller.user_result = -1;
    controller.pad_init_result = -1;
    controller.user_id = -1;
    controller.handle = -1;
    controller.arrival_result = -1;
    controller.removal_result = -1;
    physical_input.keyboard_module_result = -1;
    physical_input.keyboard_unload_result = -1;
    physical_input.keyboard_init_result = -1;
    physical_input.keyboard_open_result = -1;
    physical_input.mouse_module_result = -1;
    physical_input.mouse_unload_result = -1;
    physical_input.mouse_init_result = -1;
    physical_input.mouse_open_result = -1;
    physical_input.keyboard_close_result = -1;
    physical_input.mouse_close_result = -1;
    native_agc_set_tv_safe_area(!options ||
                                options->display_area == MOONLIGHT_DISPLAY_AREA_TV_SAFE);

    if (metrics)
        memset(metrics, 0, sizeof(*metrics));

    if (!host[0] || (!use_pyrowave && !mode))
        return -1;
#if !PROSPEROLIGHT_PYROWAVE
    if (use_pyrowave)
    {
        if (metrics)
            snprintf(metrics->error, sizeof(metrics->error),
                     "PyroWave backend is not included in this build");
        return -1;
    }
#endif
    snprintf(notification.message, sizeof(notification.message),
             "Stream profile: codec=%u format=%s resolution=%ux%u fps=%u bitrate=%u kbps chroma=%s "
             "depth=%u range=%s decoder=%s bitstream=%s",
             profile.codec, profile.format_name, stream_width, stream_height, stream_fps,
             bitrate_kbps, profile.chroma == MOONLIGHT_CHROMA_444 ? "444" : "420",
             profile.bit_depth, profile.hdr ? "HDR10" : "SDR", use_pyrowave ? "RADV" : "VideoDec2",
             use_pyrowave ? PYROWAVE_BITSTREAM_ID : "n/a");
    (void)lan_http_report_text(notification.message);
    protocol_error[0] = 0;
#if PROSPEROLIGHT_PYROWAVE
    if (use_pyrowave)
        prosperolight::pyrowave::clear_error();
#endif
    lan_http_report_set_host(host);
    // The loading renderer opens VideoOut before decoder/connection setup.
    // Apply this session's policy before that first open, not after negotiation.
    stream_presentation_mode = options && options->session_overrides.enabled
                                   ? options->session_overrides.pacing
                                   : moonlight::presentation_mode();
    stream_vrr_profile = options && options->session_overrides.enabled
                             ? options->session_overrides.vrr_profile
                             : moonlight::vrr_profile();
#if PROSPEROLIGHT_PYROWAVE
    ps5_stream_output_policy(stream_presentation_mode);
#endif
    native_agc_set_vrr(stream_presentation_mode == 2);
    native_agc_set_unpaced_90_fps_unpeg(stream_presentation_mode == 0);
    native_agc_set_vsync((int)vsync_enabled);
    snprintf(notification.message, sizeof(notification.message),
             "Moonlight output policy: mode=%u vrr_requested=%u vsync_requested=%u",
             stream_presentation_mode, stream_presentation_mode == 2 ? 1u : 0u, vsync_enabled);
    (void)lan_http_report_text(notification.message);
    if (requested_audio == MOONLIGHT_AUDIO_51_SURROUND && ps5_audio_surround_available())
        audio_configuration = AUDIO_CONFIGURATION_51_SURROUND;
    else if (requested_audio == MOONLIGHT_AUDIO_51_SURROUND)
        (void)lan_http_report_text("Moonlight 5.1 unavailable; falling back to stereo");

    direct_memory_limit = sceKernelGetDirectMemorySize();
    result = connecting_screen_begin(options ? options->connecting : NULL, profile.hdr,
                                     stream_width, stream_height, stream_fps, direct_memory_limit);
    if (result != 0)
        goto done;
    connecting_screen_stage(connecting_screen.progress, 0.45f);
    result = start_connection_loading(&loading, NULL);
    if (result != 0)
        goto done;

    snprintf(notification.message, sizeof(notification.message),
             "Native zero-copy stage 1: mode=%s fps=%u bitrate=%u kbps input_slot=%x", profile.name,
             stream_fps, bitrate_kbps, INPUT_SLOT_BYTES);
    (void)lan_http_report_text(notification.message);
    sceSystemServiceHideSplashScreen();
    if (use_pyrowave)
    {
        renderer.stream_fps = stream_fps;
        renderer.process_cpu_mask = moonlight::kTitleCpuMask;
        renderer.layout = moonlight::plan_thread_layout(renderer.process_cpu_mask, 0);
        stop_connection_loading();
        goto configure_stream;
    }
    result = prosperolight::native_modules::Result(prosperolight::native_modules::video_decoder);
    snprintf(notification.message, sizeof(notification.message),
             "Native zero-copy stage 2: sysmodule207=%08x", (uint32_t)result);
    (void)lan_http_report_text(notification.message);
    if (result != 0)
    {
        snprintf(stream_error, sizeof(stream_error),
                 "Local video decoder module failed to load (0x%08x). Restart ProsperoLight.",
                 (uint32_t)result);
        goto done;
    }

    compute_memory.size = sizeof(compute_memory);
    result = sceVideodec2QueryComputeMemoryInfo(&compute_memory);
    compute_size = align_16k((size_t)compute_memory.cpu_gpu_size);
    snprintf(notification.message, sizeof(notification.message),
             "Native compute query: rc=%08x shared=%llx mapped=%zx", (uint32_t)result,
             (unsigned long long)compute_memory.cpu_gpu_size, compute_size);
    (void)lan_http_report_text(notification.message);
    if (result != 0)
        goto done;

    result = allocate_direct(compute_size, 0x33, direct_memory_limit, &compute_start,
                             &compute_memory.cpu_gpu);
    if (result == 0)
    {
        compute_memory.cpu_gpu_size = compute_size;
        compute_config.size = sizeof(compute_config);
        compute_config.pipe_id = 0;
        compute_config.queue_id = 0;
        result = sceVideodec2AllocateComputeQueue(&compute_config, &compute_memory, &compute_queue);
    }
    snprintf(notification.message, sizeof(notification.message),
             "Native compute queue: rc=%08x memory=%p/%zx queue=%p", (uint32_t)result,
             compute_memory.cpu_gpu, compute_size, compute_queue);
    (void)lan_http_report_text(notification.message);
    if (result != 0)
        goto done;

    memset(&config, 0, sizeof(config));
    config.size = sizeof(config);
    config.resource_type = 1;
    config.codec_type = mode->codec_type;
    config.profile = mode->profile;
    config.max_level = decoder_max_level(mode, stream_fps);
    config.max_width = (int32_t)mode->max_width;
    config.max_height = (int32_t)mode->max_height;
    // DPB, dimensions and level are populated from the first validated SPS.
    config.max_dpb_frames = 0;
    config.compute_queue = (uint64_t)compute_queue;
    config.cpu_priority = DECODER_CPU_PRIORITY;
    config.optimize_progressive = 1;

    renderer.pipeline_mode =
        classic_pipeline ? MOONLIGHT_DECODER_PIPELINE_CLASSIC : MOONLIGHT_DECODER_PIPELINE_ADAPTIVE;
    renderer.requested_depth = requested_depth;
    renderer.process_cpu_mask = moonlight::kTitleCpuMask;
    renderer.requested_cpu_mask =
        moonlight::decoder_cpu_mask(requested_cores, renderer.process_cpu_mask);
    config.pipeline_depth = requested_depth;
    config.cpu_affinity = renderer.requested_cpu_mask;
    renderer.decoder_config = config;
    renderer.pipeline_depth = requested_depth;
    renderer.decoder_cpu_mask = renderer.requested_cpu_mask;
    renderer.decoder_cores = requested_cores;
    renderer.layout = moonlight::plan_thread_layout(
        renderer.process_cpu_mask, renderer.requested_cpu_mask | moonlight::kClassicDecoderCpuMask);
    renderer.mode = mode;
    renderer.stream_fps = stream_fps;
    renderer.input_size = INPUT_SLOT_BYTES;
    renderer.direct_limit = direct_memory_limit;
    renderer.stream_aware = true;
    renderer.stream_policy.allow_reconfiguration = MOONLIGHT_EXPERIMENTAL_DECODER_CONTROLS != 0;
    renderer.lifecycle.wait(0);
    renderer.slices_requested = stream_slices;
    renderer.vsync_requested = vsync_enabled;
    renderer_sync_init(&renderer);
    snprintf(notification.message, sizeof(notification.message),
             "Moonlight pipeline: mode=%s depth=%u/%u drain=%u cores=%u affinity=%llx/%llx "
             "attempts=%u receive=%llx decode=%llx present=%llx other=%llx slices=%u vsync=%u",
             classic_pipeline ? "classic" : "adaptive", renderer.pipeline_depth, requested_depth,
             renderer.drain_enabled ? 1u : 0u, renderer.decoder_cores,
             (unsigned long long)renderer.decoder_cpu_mask,
             (unsigned long long)renderer.requested_cpu_mask, renderer.create_attempts,
             (unsigned long long)renderer.layout.receive,
             (unsigned long long)renderer.layout.decode,
             (unsigned long long)renderer.layout.present, (unsigned long long)renderer.layout.other,
             stream_slices, vsync_enabled);
    (void)lan_http_report_text(notification.message);

    if (std::atomic_load_explicit(&loading.cancel_requested, std::memory_order_relaxed))
    {
        if (std::atomic_load_explicit(&loading.timed_out, std::memory_order_relaxed))
        {
            gs_error = "Decoder setup timed out";
            result = GS_IO_ERROR;
        }
        else
            result = 0;
        goto done;
    }
    stop_connection_loading();

configure_stream:
    LiInitializeStreamConfiguration(&stream_config);
    stream_config.width = (int)stream_width;
    stream_config.height = (int)stream_height;
    stream_config.fps = (int)stream_fps;
    stream_config.bitrate = (int)bitrate_kbps;
    stream_config.packetSize = 1392;
    stream_config.streamingRemotely = STREAM_CFG_LOCAL;
    stream_config.audioConfiguration = audio_configuration;
    stream_config.supportedVideoFormats = profile.video_format;
    stream_config.colorSpace = profile.hdr ? COLORSPACE_REC_2020 : COLORSPACE_REC_709;
    stream_config.colorRange = COLOR_RANGE_LIMITED;
    stream_config.pyrowaveCompression = 0;
    stream_config.encryptionFlags = ENCFLG_NONE;
    controller_result = ps5_controller_init(&controller);
#if PROSPEROLIGHT_TEST_LAB
    test_probe.controller_initialized.store(true, std::memory_order_release);
#endif
    controller_ready = controller_result == 0;
    prosperolight::dualsense::Init(controller.user_id, controller.handle);
    moonlight_connection_callbacks.controllerHaptics =
        prosperolight::waveform::AnyReady() ? prosperolight::dualsense::Haptics : nullptr;
    if (controller_ready && controller.user_id >= 0)
        physical_input_ready = ps5_physical_input_init(&physical_input, controller.user_id) == 0;
    // Discovery in Init() has already populated the connected controller mask.
    snprintf(notification.message, sizeof(notification.message),
             "Moonlight controller init: ready=%d user_service=%08x user=%08x pad_init=%08x "
             "handle=%08x launch_mask=%x",
             controller_ready, (uint32_t)controller.user_service_result,
             (uint32_t)controller.user_result, (uint32_t)controller.pad_init_result,
             (uint32_t)controller.handle, prosperolight::dualsense::ActiveMask());
    (void)lan_http_report_text(notification.message);
    if (!controller_ready)
    {
#if PROSPEROLIGHT_TEST_LAB
        if (prosperolight::test_lab::active())
            prosperolight::test_lab::context().fail("NO_CONTROLLER");
#endif
        gs_error = "PS5 controller ownership was unavailable; retry the stream";
        result = controller_result;
        goto done;
    }
    snprintf(notification.message, sizeof(notification.message),
             "Moonlight physical input init: ready=%d keyboard_module=%08x keyboard_init=%08x "
             "keyboard_open=%08x handles=%u mouse_module=%08x mouse_init=%08x mouse_open=%08x "
             "handles=%u",
             physical_input_ready, (uint32_t)physical_input.keyboard_module_result,
             (uint32_t)physical_input.keyboard_init_result,
             (uint32_t)physical_input.keyboard_open_result, physical_input.keyboard_handle_count,
             (uint32_t)physical_input.mouse_module_result,
             (uint32_t)physical_input.mouse_init_result, (uint32_t)physical_input.mouse_open_result,
             physical_input.mouse_handle_count);
    (void)lan_http_report_text(notification.message);
    // The decoder is ready; next Sunshine starts the app.
    connecting_screen_stage(0.45f, 0.70f);
    result = start_connection_loading(&loading, controller_ready ? &controller : NULL);
    if (result != 0)
        goto done;
    renderer.client_refresh_x100 =
        moonlight::client_refresh_x100(stream_fps, connecting_screen.output_refresh_x100);
    stream_config.clientRefreshRateX100 = (int)renderer.client_refresh_x100;
    identity_initialized = 1;
    result = prepare_native_session(&client_identity, &gs_server, &stream_config, profile,
                                    controller_ready ? prosperolight::dualsense::ActiveMask() : 0,
                                    host, host_port, app_name, app_id, session_preferences,
                                    options ? &options->session_overrides : nullptr);
    if (result != GS_OK)
        goto done;
    session_started = 1;
    connecting_screen_stage(0.70f, 0.88f);
    if (std::atomic_load_explicit(&loading.cancel_requested, std::memory_order_relaxed))
    {
        if (std::atomic_load_explicit(&loading.timed_out, std::memory_order_relaxed))
        {
            gs_error = "Sunshine connection setup timed out";
            result = GS_IO_ERROR;
        }
        else
            result = 0;
        goto done;
    }

    protocol_error[0] = 0;
    connection_terminated = 0;
    connection_error = 0;
    std::atomic_store_explicit(&host_hdr_active, 0u, std::memory_order_relaxed);
    std::atomic_store_explicit(&host_hdr_transitions, 0u, std::memory_order_relaxed);
    std::atomic_store_explicit(&loading.connection_pending, 1, std::memory_order_release);
    ps5_network_metrics_begin(PROSPEROLIGHT_PERFORMANCE_DETAIL);
    std::atomic_store_explicit(&video_recovery_epoch, 0u, std::memory_order_relaxed);
    std::atomic_store_explicit(&video_queue_overflows, 0u, std::memory_order_relaxed);
    std::atomic_store_explicit(&video_unrecoverable_frames, 0u, std::memory_order_relaxed);
    std::atomic_store_explicit(&connection_failed_stage, 0, std::memory_order_relaxed);
    if (!use_pyrowave)
        prepare_video_callbacks(stream_slices, mode->codec_type == 1u);
#if PROSPEROLIGHT_PYROWAVE
    if (use_pyrowave)
    {
        // RADV must take sole ownership before its swapchain opens VideoOut.
        connecting_screen_stop();
        if (native_agc_present_shutdown() != 0)
        {
            result = -1;
            goto done;
        }
        connecting_screen_release();
        prosperolight::pyrowave::prepare_callbacks(fail_stream, vsync_enabled != 0,
                                                   !options || options->display_area ==
                                                                   MOONLIGHT_DISPLAY_AREA_TV_SAFE,
                                                   stream_presentation_mode, stream_vrr_profile);
    }
#endif
    stream_pacer.reset(renderer.stream_fps);
    renderer.mailbox.capacity = stream_presentation_mode ? 2u : 1u;
    LOGI("Moonlight pacing: mode=%u requested_fps=%u ready_capacity=%u source_clock=1",
         stream_presentation_mode, renderer.stream_fps, renderer.mailbox.capacity);
    // Keep every stream thread off the decoder's CPUs. New threads inherit the
    // creator's mask, so narrow this thread first; moonlight-common-c's thread
    // hook then gives the video receive thread a CPU of its own.
    placement.receive = renderer.layout.receive;
    placement.audio = renderer.layout.other;
    placement.other = renderer.layout.other;
    ps5_thread_placement_configure(&placement);
    main_mask_known = ps5_thread_affinity_get(&main_cpu_mask) == 0 && main_cpu_mask != 0;
    renderer.main_placement_result = -1;
    if (main_mask_known && renderer.layout.other)
    {
        renderer.main_placement_result = ps5_thread_affinity_set(renderer.layout.other);
        main_mask_changed = renderer.main_placement_result == 0;
    }
    renderer.delivery_session_floor_us = PltGetMicroseconds();
    connection_result = LiStartConnection(
        &gs_server.server_info, &stream_config, &moonlight_connection_callbacks,
#if PROSPEROLIGHT_PYROWAVE
        use_pyrowave ? prosperolight::pyrowave::callbacks() : &moonlight_video_callbacks,
#else
        &moonlight_video_callbacks,
#endif
        &moonlight_audio_callbacks, &renderer, 0, NULL, 0);
    std::atomic_store_explicit(&loading.connection_pending, 0, std::memory_order_release);
    stop_connection_loading();
    if (connection_result != 0)
    {
        if (controller.requested_stop)
            result = 0;
        else if (std::atomic_load_explicit(&loading.timed_out, std::memory_order_relaxed))
        {
            gs_error = "Sunshine connection setup timed out";
            result = GS_IO_ERROR;
        }
        else
            result = connection_result;
        goto done;
    }
    connection_active = 1;
#if PROSPEROLIGHT_TEST_LAB
    test_probe.connected.store(true, std::memory_order_release);
#endif
    stream_started = 1;
    // Connected: what is left is the first picture.
    connecting_screen_stage(0.88f, 0.97f);
    first_frame_wait_start_us = monotonic_us();
    if (options && options->synthetic_motion)
        synthetic_motion_next_us = monotonic_us();

    while (!connection_terminated && !controller.requested_stop)
    {
#if PROSPEROLIGHT_STREAM_SELF_TEST_FPS != 0
        // Development-only autostart must finish through normal stream teardown
        // so the receipt is complete before the external title-close controller.
        if (monotonic_us() - first_frame_wait_start_us >= UINT64_C(90000000))
            break;
#endif
        const uint64_t input_poll_us = monotonic_us();
        if (last_input_poll_us)
            input_intervals.add(input_poll_us - last_input_poll_us);
        last_input_poll_us = input_poll_us;
        if (options && options->synthetic_motion)
        {
            const uint64_t now = monotonic_us();
            if (now >= synthetic_motion_next_us)
            {
                const short x = (short)((synthetic_motion_events * 32u) % stream_width);
                const short y = (short)(stream_height / 2u);
                if (LiSendMousePositionEvent(x, y, (short)stream_width, (short)stream_height) != 0)
                    ++synthetic_motion_errors;
                ++synthetic_motion_events;
                synthetic_motion_next_us += UINT64_C(16667);
                if (now > synthetic_motion_next_us + UINT64_C(16667))
                    synthetic_motion_next_us = now + UINT64_C(16667);
            }
        }
        if (controller_ready)
            ps5_controller_poll(&controller);
        const uint16_t pads_before = prosperolight::dualsense::ActiveMask();
        prosperolight::dualsense::Poll();
        // A controller that joins or leaves during the stream is announced.
        const uint16_t pads_changed = pads_before ^ prosperolight::dualsense::ActiveMask();
        for (unsigned pad = 1; pads_changed && pad < prosperolight::dualsense::MaxControllers;
             ++pad)
            if (pads_changed & (1u << pad))
            {
                snprintf(notification.message, sizeof(notification.message),
                         "ProsperoLight: Controller %u %s.", pad + 1u,
                         (pads_before & (1u << pad)) ? "disconnected" : "connected");
                (void)sceKernelSendNotificationRequest(0, &notification, sizeof(notification), 0);
                (void)lan_http_report_text(notification.message);
            }
        const unsigned pad_actions = prosperolight::dualsense::TakeLocalActions();
        if (pad_actions & prosperolight::dualsense::ToggleStatistics)
            native_agc_set_hud_enabled(!native_agc_hud_enabled());
        if (pad_actions & prosperolight::dualsense::StopStream)
            controller.requested_stop = 1;
        if (physical_input_ready)
            ps5_physical_input_poll(&physical_input);
#if PROSPEROLIGHT_PYROWAVE
        if (use_pyrowave)
            renderer.presented = (uint32_t)prosperolight::pyrowave::presented();
#endif
        if (std::atomic_load_explicit(&renderer.presented, std::memory_order_relaxed) == 0 &&
            monotonic_us() - first_frame_wait_start_us >= FIRST_VIDEO_FRAME_TIMEOUT_US)
        {
            first_frame_timed_out = 1;
            gs_error = "Sunshine connected, but no video frame arrived";
            break;
        }
        sceKernelUsleep(moonlight::input_poll_delay(input_poll_us, monotonic_us(), INPUT_POLL_US));
    }

    terminated = std::atomic_load_explicit(&connection_terminated, std::memory_order_relaxed);
    reported_error = std::atomic_load_explicit(&connection_error, std::memory_order_relaxed);
    user_stop = std::atomic_load_explicit(&controller.requested_stop, std::memory_order_relaxed);
    result = first_frame_timed_out               ? GS_IO_ERROR
             : terminated && reported_error != 0 ? reported_error
                                                 : 0;
    if (controller_ready)
        ps5_controller_stop(&controller);
    if (physical_input_ready)
        ps5_physical_input_stop(&physical_input);
    LiStopConnection();
    connection_active = 0;
    // Renderer/audio aggregates are single-writer, so snapshot only after join.
    if (result != 0 && !first_frame_timed_out)
    {
        if (renderer.lifecycle.state == moonlight::video::State::Failed)
            gs_error =
                renderer.lifecycle.reason == moonlight::video::Error::Capacity
                    ? "Video decoder capacity does not support this stream"
                : renderer.lifecycle.reason == moonlight::video::Error::Unsupported
                    ? "The incoming stream has unsupported decoder parameters"
                    : "Video decoder initialization or recovery failed; see decoder diagnostics";
        else if (renderer.decoder_lost)
            gs_error = "The video decoder could not be restarted";
        else if (result ==
                 std::atomic_load_explicit(&renderer.present_result, std::memory_order_relaxed))
            gs_error = "GPU presentation stopped responding";
    }
    presented = renderer.presented.load();
    live_elapsed_us = renderer.last_present_us > renderer.first_present_us
                          ? renderer.last_present_us - renderer.first_present_us
                          : 0;
    if (!use_pyrowave)
    {
        snprintf(
            notification.message, sizeof(notification.message),
            "Moonlight live result: rc=%08x connection=%08x terminated=%d user_stop=%d error=%08x "
            "access_units=%u presented=%u fragments=%u bytes=%zx frame_span_us=%llu fps_x100=%llu "
            "source=%p",
            (uint32_t)result, (uint32_t)connection_result, terminated, user_stop,
            (uint32_t)reported_error, renderer.access_units, presented, renderer.fragments,
            renderer.stream_bytes, (unsigned long long)live_elapsed_us,
            (unsigned long long)(live_elapsed_us && presented > 1
                                     ? (uint64_t)(presented - 1) * UINT64_C(100000000) /
                                           live_elapsed_us
                                     : 0),
            renderer.frame_memory);
        (void)lan_http_report_text(notification.message);
        snprintf(notification.message, sizeof(notification.message),
                 "Moonlight synthetic motion: enabled=%u events=%u errors=%u",
                 options && options->synthetic_motion ? 1u : 0u, synthetic_motion_events,
                 synthetic_motion_errors);
        (void)lan_http_report_text(notification.message);
        snprintf(notification.message, sizeof(notification.message),
                 "Moonlight live timing: copy_calls=%u copy_avg_us=%llu copy_max_us=%llu "
                 "decode_calls=%u "
                 "decode_avg_us=%llu decode_max_us=%llu flush_calls=%u flush_avg_us=%llu "
                 "flush_max_us=%llu "
                 "present_calls=%u present_avg_us=%llu present_max_us=%llu",
                 renderer.access_units,
                 (unsigned long long)(renderer.access_units
                                          ? renderer.copy_total_us / renderer.access_units
                                          : 0),
                 (unsigned long long)renderer.copy_max_us, renderer.decode_calls,
                 (unsigned long long)(renderer.decode_calls
                                          ? renderer.decode_total_us / renderer.decode_calls
                                          : 0),
                 (unsigned long long)renderer.decode_max_us, renderer.flush_calls,
                 (unsigned long long)(renderer.flush_calls
                                          ? renderer.flush_total_us / renderer.flush_calls
                                          : 0),
                 (unsigned long long)renderer.flush_max_us, presented,
                 (unsigned long long)(presented ? renderer.present_total_us / presented : 0),
                 (unsigned long long)renderer.present_max_us);
        (void)lan_http_report_text(notification.message);
        snprintf(notification.message, sizeof(notification.message),
                 "Moonlight live latency: calls=%u callback_to_decode_avg_us=%llu min_us=%llu "
                 "max_us=%llu callback_to_flip_avg_us=%llu min_us=%llu max_us=%llu pending=%u",
                 renderer.latency_calls,
                 (unsigned long long)(renderer.ready_calls ? renderer.callback_to_decode_total_us /
                                                                 renderer.ready_calls
                                                           : 0),
                 (unsigned long long)renderer.callback_to_decode_min_us,
                 (unsigned long long)renderer.callback_to_decode_max_us,
                 (unsigned long long)(renderer.latency_calls ? renderer.callback_to_flip_total_us /
                                                                   renderer.latency_calls
                                                             : 0),
                 (unsigned long long)renderer.callback_to_flip_min_us,
                 (unsigned long long)renderer.callback_to_flip_max_us, renderer.submission_count);
        (void)lan_http_report_text(notification.message);
        {
            const ps5_thread_placement_stats_t placed = ps5_thread_placement_stats();

            snprintf(notification.message, sizeof(notification.message),
                     "Moonlight live pipeline: depth=%u drain=%u drains=%u drain_faults=%u "
                     "recreations=%u refreshes=%u overflows=%u decoded=%u not_displayed=%u "
                     "network_gaps=%llu decoder_gaps=%llu present_errors=%u placed=%u/%u "
                     "receive=%llx decode=%d present=%d main=%d vsync=%d flip_events=%d",
                     renderer.pipeline_depth, renderer.drain_enabled ? 1u : 0u,
                     renderer.drain_calls, renderer.drain_faults, renderer.decoder_recreations,
                     renderer.decoder_refreshes,
                     std::atomic_load_explicit(&video_queue_overflows, std::memory_order_relaxed),
                     renderer.decoded, renderer.not_displayed.load(),
                     (unsigned long long)renderer.drops.network,
                     (unsigned long long)renderer.drops.decoder, renderer.present_errors,
                     placed.applied, placed.applied + placed.failed,
                     (unsigned long long)placed.receive_verified, renderer.decode_placement_result,
                     renderer.present_placement_result, renderer.main_placement_result,
                     native_agc_vsync_active(), native_agc_flip_events_active());
            (void)lan_http_report_text(notification.message);
        }
    }

done:
#if PROSPEROLIGHT_TEST_LAB
    if (prosperolight::test_lab::active() && result == GS_NOT_SUPPORTED_MODE)
        prosperolight::test_lab::context().fail("UNSUPPORTED_MODE");
    if (prosperolight::test_lab::active())
        prosperolight::test_lab::context().join();
#endif
#if PROSPEROLIGHT_PYROWAVE
    if (result != 0 && use_pyrowave)
    {
        prosperolight::pyrowave::copy_error(stream_error, sizeof(stream_error));
        if (stream_error[0])
        {
            snprintf(protocol_error, sizeof(protocol_error), "%s", stream_error);
            gs_error = protocol_error;
        }
        else if (protocol_error[0])
            gs_error = protocol_error;
    }
#endif
    if (result != 0 && !controller.requested_stop)
    {
        if (std::atomic_load_explicit(&loading.timed_out, std::memory_order_relaxed))
            snprintf(stream_error, sizeof(stream_error),
                     "Connection timed out before streaming started. Returned safely; refresh the "
                     "PC and retry.");
        else if (first_frame_timed_out)
            snprintf(stream_error, sizeof(stream_error),
                     "Sunshine connected but no video arrived. The session was closed; retry or "
                     "choose another codec.");
        else if (gs_error && gs_error[0])
            snprintf(stream_error, sizeof(stream_error), "Stream failed: %s", gs_error);
        else if (stream_started)
            // The stream ran and then lost its host: not a connection failure.
            snprintf(stream_error, sizeof(stream_error),
                     "Stream ended: the connection to Sunshine was lost (error %d). Reconnect to "
                     "continue.",
                     (int)result);
        else
        {
            // No text from the host protocol: name the stage and the code, so a
            // report from the notification alone is actionable.
            const int stage =
                std::atomic_load_explicit(&connection_failed_stage, std::memory_order_relaxed);
            snprintf(stream_error, sizeof(stream_error),
                     "Stream failed: Sunshine did not complete the connection (%s, error %d)",
                     stage > 0 ? LiGetStageName(stage) : "client setup", (int)result);
        }
    }
    if (result != 0)
    {
        // One bounded line for a klog capture; no host identity or payload.
        char line[160];

        snprintf(line, sizeof(line),
                 "[ProsperoLight] stream result=%d started=%d stage=%d user_stop=%d units=%u\n",
                 (int)result, stream_started,
                 std::atomic_load_explicit(&connection_failed_stage, std::memory_order_relaxed),
                 controller.requested_stop.load(), renderer.access_units);
        (void)sceKernelDebugOutText(0, line);
    }
    if (connection_active)
    {
        if (controller_ready)
            ps5_controller_stop(&controller);
        if (physical_input_ready)
            ps5_physical_input_stop(&physical_input);
        LiStopConnection();
    }
    stop_connection_loading();
    connecting_screen_stop();
    http_clear_interrupt();
    snprintf(
        notification.message, sizeof(notification.message),
        "Moonlight audio result: init=%08x open=%08x opus=%d channels=%d output_channels=%d "
        "frame_samples=%d "
        "packets=%u plc=%u packet_samples=%u-%u mismatches=%u callback_span_us=%llu "
        "callback_hz_x100=%llu interval_avg_us=%llu interval_min_us=%llu interval_max_us=%llu "
        "decode_errors=%u decoded_frames=%llu nonzero_samples=%llu peak=%u ring=%u overruns=%u "
        "dropped=%llu output_calls=%u output_errors=%u decode_avg_us=%llu decode_max_us=%llu "
        "output_avg_us=%llu output_max_us=%llu rtp_audio=%u rtp_fec=%u recovered=%u failed=%u "
        "oos=%u invalid=%u fec_invalid=%u drain=%08x close=%08x",
        (uint32_t)audio_state.init_result, (uint32_t)audio_state.open_result,
        audio_state.opus_error, audio_state.channels, audio_state.output_channels,
        audio_state.samples_per_frame, audio_state.packets, audio_state.plc_packets,
        audio_state.packet_samples_min, audio_state.packet_samples_max,
        audio_state.packet_sample_mismatches,
        (unsigned long long)(audio_state.last_packet_us > audio_state.first_packet_us
                                 ? audio_state.last_packet_us - audio_state.first_packet_us
                                 : 0),
        (unsigned long long)(audio_state.last_packet_us > audio_state.first_packet_us &&
                                     audio_state.packets > 1
                                 ? (uint64_t)(audio_state.packets - 1) * UINT64_C(100000000) /
                                       (audio_state.last_packet_us - audio_state.first_packet_us)
                                 : 0),
        (unsigned long long)(audio_state.packets > 1
                                 ? audio_state.interval_total_us / (audio_state.packets - 1)
                                 : 0),
        (unsigned long long)audio_state.interval_min_us,
        (unsigned long long)audio_state.interval_max_us, audio_state.decode_errors,
        (unsigned long long)audio_state.decoded_frames,
        (unsigned long long)audio_state.nonzero_samples, audio_state.peak_sample,
        audio_state.ring_count, audio_state.overruns,
        (unsigned long long)audio_state.dropped_frames, audio_state.output_calls,
        audio_state.output_errors,
        (unsigned long long)(audio_state.packets ? audio_state.decode_total_us / audio_state.packets
                                                 : 0),
        (unsigned long long)audio_state.decode_max_us,
        (unsigned long long)(audio_state.output_calls
                                 ? audio_state.output_total_us / audio_state.output_calls
                                 : 0),
        (unsigned long long)audio_state.output_max_us, audio_state.rtp.packetCountAudio,
        audio_state.rtp.packetCountFec, audio_state.rtp.packetCountFecRecovered,
        audio_state.rtp.packetCountFecFailed, audio_state.rtp.packetCountOOS,
        audio_state.rtp.packetCountInvalid, audio_state.rtp.packetCountFecInvalid,
        (uint32_t)audio_state.drain_result, (uint32_t)audio_state.close_result);
    (void)lan_http_report_text(notification.message);
    snprintf(
        notification.message, sizeof(notification.message),
        "Moonlight controller result: ready=%d user_service=%08x user=%08x pad_init=%08x "
        "handle=%08x arrival=%08x removal=%08x polls=%u samples=%u empty=%u max_batch=%u "
        "generation=%u events=%u read_errors=%u send_errors=%u disconnected=%u intercepted=%u "
        "nonneutral=%u raw=%08x mapped=%08x last=%08x triggers=%u,%u axes=%d,%d,%d,%d "
        "mouse_mode=%d toggles=%u motion=%u buttons=%u scroll=%u mouse_errors=%u",
        controller_ready, (uint32_t)controller.user_service_result,
        (uint32_t)controller.user_result, (uint32_t)controller.pad_init_result,
        (uint32_t)controller.handle, (uint32_t)controller.arrival_result,
        (uint32_t)controller.removal_result, controller.polls, controller.samples,
        controller.empty_reads, controller.max_batch, controller.connected_count, controller.events,
        controller.read_errors, controller.send_errors, controller.disconnected_samples,
        controller.intercepted_samples, controller.nonneutral_samples,
        controller.observed_raw_buttons, controller.observed_moonlight_buttons,
        controller.last_raw_buttons, controller.last_event.left_trigger,
        controller.last_event.right_trigger, controller.last_event.left_x,
        controller.last_event.left_y, controller.last_event.right_x, controller.last_event.right_y,
        controller.mouse_mode, controller.mouse_toggles, controller.mouse_motion_events,
        controller.mouse_button_events, controller.mouse_scroll_events, controller.mouse_errors);
    (void)lan_http_report_text(notification.message);
    snprintf(notification.message, sizeof(notification.message),
             "Moonlight decode profile: first_us=%llu over_budget=%llu slices=%u-%u "
             "slice_samples=%u IDR_calls=%llu IDR_avg_us=%llu IDR_p99_upper_us=%llu "
             "P_calls=%llu P_avg_us=%llu P_p99_upper_us=%llu other_mask=%llx",
             (unsigned long long)renderer.decode_first_us,
             (unsigned long long)renderer.decode_over_budget, renderer.observed_slices_min,
             renderer.observed_slices_max, renderer.layout_samples,
             (unsigned long long)renderer.idr_decode_timing.count,
             (unsigned long long)(renderer.idr_decode_timing.count
                                      ? renderer.idr_decode_timing.total_us /
                                            renderer.idr_decode_timing.count
                                      : 0),
             (unsigned long long)renderer.idr_decode_timing.percentile(99),
             (unsigned long long)renderer.inter_decode_timing.count,
             (unsigned long long)(renderer.inter_decode_timing.count
                                      ? renderer.inter_decode_timing.total_us /
                                            renderer.inter_decode_timing.count
                                      : 0),
             (unsigned long long)renderer.inter_decode_timing.percentile(99),
             (unsigned long long)renderer.layout.other);
    (void)lan_http_report_text(notification.message);
    ps5_physical_input_shutdown(&physical_input);
    snprintf(notification.message, sizeof(notification.message),
             "Moonlight physical input result: ready=%d keyboard_module=%08x open=%08x handles=%u "
             "polls=%u events=%u read_errors=%u send_errors=%u mouse_module=%08x open=%08x "
             "handles=%u polls=%u samples=%u motion=%u buttons=%u scroll=%u read_errors=%u "
             "send_errors=%u keyboard_close=%08x mouse_close=%08x keyboard_unload=%08x "
             "mouse_unload=%08x",
             physical_input_ready, (uint32_t)physical_input.keyboard_module_result,
             (uint32_t)physical_input.keyboard_open_result, physical_input.keyboard_handle_count,
             physical_input.keyboard_polls, physical_input.keyboard_events,
             physical_input.keyboard_read_errors, physical_input.keyboard_send_errors,
             (uint32_t)physical_input.mouse_module_result,
             (uint32_t)physical_input.mouse_open_result, physical_input.mouse_handle_count,
             physical_input.mouse_polls, physical_input.mouse_samples,
             physical_input.mouse_motion_events, physical_input.mouse_button_events,
             physical_input.mouse_scroll_events, physical_input.mouse_read_errors,
             physical_input.mouse_send_errors, (uint32_t)physical_input.keyboard_close_result,
             (uint32_t)physical_input.mouse_close_result,
             (uint32_t)physical_input.keyboard_unload_result,
             (uint32_t)physical_input.mouse_unload_result);
    (void)lan_http_report_text(notification.message);
    ps5_controller_shutdown(&controller);
    bool quit_host_on_stop = session_preferences.quit_host;
#if PROSPEROLIGHT_TEST_LAB
    // A lab owns a bounded host session even when the saved Quit Host option is off.
    quit_host_on_stop = quit_host_on_stop || prosperolight::test_lab::active();
#endif
    if (session_started && (!controller.requested_stop || quit_host_on_stop))
    {
        http_set_timeout_ms(2000);
        int quit_result = gs_quit_app(&gs_server);
#if PROSPEROLIGHT_TEST_LAB
        if (prosperolight::test_lab::active())
        {
            // Restoring per-app encoder overrides can delay serverinfo after
            // an acknowledged cancel. Allow the normal bounded HTTP budget
            // for confirmation only; never retry cancellation or assume exit.
            http_set_timeout_ms(7000);
            const int refresh_result =
                quit_result == GS_OK ? gs_refresh_paired_status(&gs_server) : GS_WRONG_STATE;
            prosperolight::test_lab::context().host_exit_cleanup_result(
                quit_result == GS_OK && refresh_result == GS_OK && gs_server.current_game == 0);
        }
#endif
        snprintf(notification.message, sizeof(notification.message),
                 "Native NVHTTP cancel: rc=%08x error=%s", (uint32_t)quit_result,
                 gs_error ? gs_error : "");
        (void)lan_http_report_text(notification.message);
    }
    if (identity_initialized)
    {
        identity_free(&client_identity);
        gs_log_set_sink(NULL);
    }
    if (active_renderer)
    {
        moonlight_video_callbacks.stop();
        moonlight_video_callbacks.cleanup();
    }
#if PROSPEROLIGHT_PYROWAVE
    if (use_pyrowave)
        prosperolight::pyrowave::cleanup();
#endif
    LOGI("Moonlight pacing result: mode=%u period_us=%llu reserve_us=%llu submissions=%llu "
         "misses=%llu resets=%llu wait_total_us=%llu late_max_us=%llu spacing_error_max_us=%llu",
         stream_presentation_mode, (unsigned long long)stream_pacer.stats.period_us,
         (unsigned long long)stream_pacer.stats.reserve_us,
         (unsigned long long)stream_pacer.stats.submissions,
         (unsigned long long)stream_pacer.stats.misses,
         (unsigned long long)stream_pacer.stats.resets,
         (unsigned long long)stream_pacer.stats.wait_total_us,
         (unsigned long long)stream_pacer.stats.late_max_us,
         (unsigned long long)stream_pacer.stats.spacing_error_max_us);
    controller_summary = prosperolight::dualsense::GetStatistics();
    save_performance_summary(renderer, input_intervals, options, result);
    if (renderer.access_units)
    {
        log_performance_windows(renderer.stream_fps);
        save_frame_trace(stream_presentation_mode);
    }
    ps5_thread_placement_clear();
    if (main_mask_changed)
        (void)ps5_thread_affinity_set(main_cpu_mask);
    // Both workers have joined: retire the last flip before touching memory.
    const int source_idle_result = native_agc_finish_frame();
    present_cleanup_result = native_agc_present_shutdown();
    // A decoder that was never created, or that a fallback already deleted,
    // leaves nothing to delete.
    if (source_idle_result == 0)
        delete_result = renderer.decoder ? sceVideodec2DeleteDecoder(renderer.decoder) : 0;
    renderer_sync_destroy(&renderer);
    if (source_idle_result == 0 && delete_result == 0)
    {
        connecting_screen_release();
        release_direct(renderer.frame_memory, renderer.owned_frame_start,
                       renderer.owned_frame_bytes);
        release_direct(renderer.input_memory, renderer.owned_input_start,
                       renderer.owned_input_bytes);
        release_decoder_resources(&renderer.owned_resources);
        renderer.lifecycle.state = moonlight::video::State::Stopped;
        if (compute_queue)
            release_compute_result = sceVideodec2ReleaseComputeQueue(compute_queue);
        release_direct(compute_memory.cpu_gpu, compute_start, compute_size);
        unload_result = 0; // Native modules are retained across streams.
    }
    else
    {
        // ponytail: retain one faulted session's native allocations until process
        // exit; require restart rather than inventing in-process GPU recovery.
        presentation_faulted = true;
        result = source_idle_result ? source_idle_result : delete_result;
        snprintf(stream_error, sizeof(stream_error),
                 "Video cleanup failed. Restart ProsperoLight before streaming again.");
    }
    snprintf(
        notification.message, sizeof(notification.message),
        "Native zero-copy cleanup: rc=%08x present=%08x delete=%08x compute=%08x unload=%08x done",
        (uint32_t)result, (uint32_t)present_cleanup_result, (uint32_t)delete_result,
        (uint32_t)release_compute_result, (uint32_t)unload_result);
    (void)lan_http_report_text(notification.message);
    if (stream_error[0])
    {
        snprintf(notification.message, sizeof(notification.message), "ProsperoLight: %s",
                 stream_error);
        (void)sceKernelSendNotificationRequest(0, &notification, sizeof(notification), 0);
    }
    if (metrics)
    {
        metrics->result = result;
        metrics->presented_frames = renderer.presented;
        metrics->access_units = renderer.access_units;
        metrics->pending_frames = renderer.submission_count;
        metrics->audio_packets = audio_state.packets;
        metrics->audio_overruns = audio_state.overruns;
        metrics->controller_polls = controller.polls;
        metrics->controller_errors = controller.read_errors + controller.send_errors;
        metrics->hdr_active =
            std::atomic_load_explicit(&host_hdr_active, std::memory_order_relaxed);
        metrics->hdr_transitions =
            std::atomic_load_explicit(&host_hdr_transitions, std::memory_order_relaxed);
        metrics->callback_to_flip_average_us =
            renderer.latency_calls ? renderer.callback_to_flip_total_us / renderer.latency_calls
                                   : 0;
        snprintf(metrics->error, sizeof(metrics->error), "%s", stream_error);
    }
#if PROSPEROLIGHT_TEST_LAB
    if (prosperolight::test_lab::active() && result == 0 &&
        (source_idle_result != 0 || present_cleanup_result != 0 || delete_result != 0))
        result = source_idle_result       ? source_idle_result
                 : present_cleanup_result ? present_cleanup_result
                                          : delete_result;
#endif
    return result;
}
