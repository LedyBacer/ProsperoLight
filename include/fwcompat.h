/* ps5-native-app-boilerplate / ProsperoLight - Firmware startup experiment.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/* Diagnostic experiment only; production defaults preserve the existing startup. */
#pragma once
#ifndef FWCOMPAT_DIAGNOSTICS
#define FWCOMPAT_DIAGNOSTICS 0
#endif
#ifndef FWCOMPAT_SKIP_DECODER_WARMUP
#define FWCOMPAT_SKIP_DECODER_WARMUP 0
#endif
#ifndef FWCOMPAT_SKIP_VIDEO_MODULE_PRELOAD
#define FWCOMPAT_SKIP_VIDEO_MODULE_PRELOAD 0
#endif
#ifndef FWCOMPAT_DISABLE_SHADER_CACHE
#define FWCOMPAT_DISABLE_SHADER_CACHE 0
#endif
#ifndef FWCOMPAT_FORCE_1080P
#define FWCOMPAT_FORCE_1080P 0
#endif
// Bit 0: compute query; bit 1: the original three decoder queries.
#ifndef FWCOMPAT_WARMUP_MASK
#define FWCOMPAT_WARMUP_MASK 3
#endif
#ifndef FWCOMPAT_AGC_FIRST
#define FWCOMPAT_AGC_FIRST 0
#endif
// Experimental FW 3.x VideoOut: never call the newer VRR Unpeg/Peg API.
#ifndef FWCOMPAT_LEGACY_VIDEOOUT
#define FWCOMPAT_LEGACY_VIDEOOUT 0
#endif
// Runtime firmware-aware VRR API policy. HFR remains independently selectable.
#ifndef FWCOMPAT_AUTO_VIDEOOUT
#define FWCOMPAT_AUTO_VIDEOOUT 0
#endif
#if FWCOMPAT_LEGACY_VIDEOOUT && FWCOMPAT_AUTO_VIDEOOUT
#error Forced legacy and automatic VideoOut policies are mutually exclusive
#endif
#if FWCOMPAT_AUTO_VIDEOOUT && (!FWCOMPAT_DIAGNOSTICS || !FWCOMPAT_AGC_FIRST)
#error Automatic VideoOut requires a diagnostic AGC-first build
#endif
#if FWCOMPAT_LEGACY_VIDEOOUT && (!FWCOMPAT_DIAGNOSTICS || !FWCOMPAT_AGC_FIRST)
#error Legacy VideoOut requires a diagnostic AGC-first build
#endif
#if FWCOMPAT_WARMUP_MASK < 0 || FWCOMPAT_WARMUP_MASK > 3
#error Invalid diagnostic warmup mask
#endif
#if !FWCOMPAT_DIAGNOSTICS && (FWCOMPAT_WARMUP_MASK != 3 || FWCOMPAT_AGC_FIRST)
#error AGC experiment options require diagnostics
#endif
// Codec bits: H.264 SDR, HEVC SDR, HEVC Main10 HDR. Original mask retained.
#ifndef FWCOMPAT_CODEC_MASK
#define FWCOMPAT_CODEC_MASK 7
#endif
#ifndef FWCOMPAT_QUERY_SNAPSHOTS
#define FWCOMPAT_QUERY_SNAPSHOTS 0
#endif
#ifndef FWCOMPAT_COMPUTE_LAST
#define FWCOMPAT_COMPUTE_LAST 0
#endif
#if FWCOMPAT_CODEC_MASK < 0 || FWCOMPAT_CODEC_MASK > 7
#error Invalid codec query mask
#endif
#if !FWCOMPAT_DIAGNOSTICS &&                                                                       \
    (FWCOMPAT_CODEC_MASK != 7 || FWCOMPAT_QUERY_SNAPSHOTS || FWCOMPAT_COMPUTE_LAST)
#error Query experiments require diagnostics
#endif
#if FWCOMPAT_QUERY_SNAPSHOTS && !FWCOMPAT_AGC_FIRST
#error Query snapshots require successful early AGC initialization
#endif
#define FWCOMPAT_LAUNCHER_ONLY                                                                     \
    (FWCOMPAT_SKIP_VIDEO_MODULE_PRELOAD ||                                                         \
     (FWCOMPAT_DIAGNOSTICS && (FWCOMPAT_WARMUP_MASK != 3 || FWCOMPAT_CODEC_MASK != 7 ||            \
                               FWCOMPAT_QUERY_SNAPSHOTS || FWCOMPAT_COMPUTE_LAST)))
#ifndef FWCOMPAT_VARIANT_ID
#define FWCOMPAT_VARIANT_ID production
#endif
#define FWCOMPAT_STRING_(x) #x
#define FWCOMPAT_STRING(x) FWCOMPAT_STRING_(x)
#if !FWCOMPAT_DIAGNOSTICS &&                                                                       \
    (FWCOMPAT_SKIP_DECODER_WARMUP || FWCOMPAT_SKIP_VIDEO_MODULE_PRELOAD ||                         \
     FWCOMPAT_DISABLE_SHADER_CACHE || FWCOMPAT_FORCE_1080P)
#error Firmware compatibility switches require diagnostic mode
#endif
#ifdef __cplusplus
extern "C"
{
#endif
#if FWCOMPAT_DIAGNOSTICS
    void fwcompat_checkpoint(const char *stage, long long value);
    void fwcompat_operation(const char *stage, int begin, long long value);
    void fwcompat_start(void);
    void fwcompat_decoder_query(unsigned index, int begin, int result);
#if FWCOMPAT_AGC_FIRST
    int ps5_agc_fwcompat_early_init(void);
    void ps5_agc_fwcompat_after_warmup(void);
    void ps5_agc_fwcompat_query_snapshot(unsigned index);
#endif
    void fwcompat_gpu_failure(void);
    int fwcompat_gpu_failed(void);
    void fwcompat_attach(int fd, const char *folder);
#define FWCOMPAT_POINT(stage, value) fwcompat_checkpoint(stage, value)
#else
#define FWCOMPAT_POINT(stage, value) ((void)0)
#define fwcompat_start() ((void)0)
#define fwcompat_attach(fd, folder) ((void)0)
#endif
#ifdef __cplusplus
}
#if FWCOMPAT_DIAGNOSTICS
#include <type_traits>
#include <cstdint>
namespace fwcompat
{
template <class F> auto call(const char *stage, F fn)
{
    fwcompat_operation(stage, 1, 0);
    if constexpr (std::is_void_v<decltype(fn())>)
    {
        fn();
        fwcompat_operation(stage, 0, 0);
    }
    else
    {
        auto result = fn();
        fwcompat_operation(stage, 0, (long long)(intptr_t)result);
        return result;
    }
}
} // namespace fwcompat
#define FWCOMPAT_CALL(stage, ...) ::fwcompat::call(stage, [&] { return (__VA_ARGS__); })
#endif
#endif
