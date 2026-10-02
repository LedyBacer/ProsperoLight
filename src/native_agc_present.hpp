/*
 * ps5-native-app-boilerplate - ProsperoLight component.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef MLGPU_NATIVE_AGC_PRESENT_HPP
#define MLGPU_NATIVE_AGC_PRESENT_HPP

#include <stddef.h>
#include <stdint.h>
#include "moonlight_performance.hpp"

struct NativeAgcPerformance
{
    moonlight::TimingHistogram prepare, cache_flush, submit, overlay;
    moonlight::TimingHistogram gpu_render; // GPU_TIMESTAMPS builds only
    uint64_t flip_queries{}, flip_sleeps{}, flip_timeouts{};
    uint64_t flip_event_wakeups{}, flip_event_errors{}, vsync_fallbacks{};
    uint64_t gpu_samples_invalid{};
};

// Reset only after the loading owner stops; read after the video worker joins.
void native_agc_reset_performance();
const NativeAgcPerformance &native_agc_performance();

typedef struct native_agc_metrics
{
    uint32_t video_codec;
    uint32_t incoming_fps_x100;  // host send rate, including frames never decoded
    uint32_t decoded_fps_x100;   // decoder outputs
    uint32_t rendering_fps_x100; // completed flips, filled by the presentation owner
    uint32_t network_drop_percent_x100;
    uint32_t decoder_drop_percent_x100; // discarded by queue overflow or refresh
    uint32_t not_displayed;             // decoded but superseded before display
    uint32_t rtt_ms;
    uint32_t rtt_variance_ms;
    uint32_t rtt_valid;
    uint32_t host_min_tenths_ms;
    uint32_t host_max_tenths_ms;
    uint32_t host_average_tenths_ms;
    uint32_t decoder_load_permille;
    uint64_t decode_average_us; // last full one-second window
    uint64_t decode_p95_us;
    uint64_t queue_delay_average_us;
    uint64_t queue_delay_max_us;
    uint32_t pending_frames;
    uint32_t slices_requested;
    uint32_t slices_observed;
    uint32_t pipeline_depth;
    uint32_t decoder_cores;
    uint32_t vsync_enabled;
    uint64_t decoder_cpu_mask;
} native_agc_metrics_t;

int native_agc_present_nv12(const void *source, size_t source_bytes, uint32_t pitch,
                            uint32_t surface_height, uint32_t visible_width,
                            uint32_t visible_height, uint32_t requested_fps,
                            const native_agc_metrics_t *metrics);
int native_agc_present_main10(const void *source, size_t source_bytes, uint32_t pitch,
                              uint32_t surface_height, uint32_t visible_width,
                              uint32_t visible_height, uint32_t requested_fps,
                              const native_agc_metrics_t *metrics);
int native_agc_present_loading(void *surface, size_t surface_bytes, uint32_t phase, int hdr,
                               uint32_t output_source_width, uint32_t output_source_height,
                               uint32_t requested_fps);
int native_agc_wait_source_idle(const void *source);
int native_agc_finish_frame(void);
// Query only on the presentation owner thread, or after the stream worker joins.
void native_agc_output_status(uint32_t *width, uint32_t *height, uint32_t *refresh_x100);
void native_agc_set_hud_enabled(int enabled);
int native_agc_hud_enabled(void);
void native_agc_set_keyboard_state(int enabled, uint32_t selected, int shifted);
void native_agc_set_tv_safe_area(int enabled);
// V-Sync off flips at the next hsync (tearing). Applies to the next submission;
// a rejected immediate flip falls back to V-Sync for the rest of the process.
void native_agc_set_vsync(int enabled);
int native_agc_vsync_active(void);
int native_agc_flip_events_active(void);
int native_agc_present_shutdown(void);
/* The launcher's OpenGL runtime initialises AGC for the process when it draws
   its first frame. Call this once it has: the stream's presenter then uses
   that initialisation and does not ask for a second one. */
void native_agc_note_initialized(void);

#endif
