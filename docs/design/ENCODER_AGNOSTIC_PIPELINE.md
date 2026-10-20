# Encoder-independent native video policy

This extends the SPS/PPS-aware VideoDec2 owner from PR61. Encoder names are
never configuration keys. The NVENC-compatible default remains depth1, the
requested CPU mask, existing slice negotiation and the same AGC surface ownership.
No FFmpeg decoder, pixel conversion, additional frame queue or PyroWave change.

## Source review (2026-10-20)

| Client/source | Mechanism and PS5 decision |
| --- | --- |
| [Qt ffmpeg.cpp](https://github.com/moonlight-stream/moonlight-qt/blob/dc92b6d2f361064d3e9e53ce5de92f611db77220/app/streaming/video/ffmpeg.cpp) / session.cpp | Hardware-first backend probing; optional software fallback; LOW_DELAY; slice-threaded software decode and corresponding host capabilities. Keep capabilities before negotiation separate from SPS facts after negotiation. Do not introduce FFmpeg or frame threading on PS5. |
| [Android helper](https://github.com/moonlight-stream/moonlight-android/blob/b48494cb96bff23d8886c4775cc4f39a1075495d/app/src/main/java/com/limelight/binding/video/MediaCodecHelper.java) / renderer / decoder-errata.txt | Performance points, per-decoder low-latency options, bounded restart/flush and decoder-specific SPS workarounds. Capability success is not throughput proof. Do not import Android vendor keys or rewrite a valid stream's DPB to1. |
| [iOS renderer](https://github.com/moonlight-stream/moonlight-ios/blob/02dc9780496eeeac6d01c8bbdccb8b6fe71ef28a/Limelight/Stream/VideoDecoderRenderer.m) | Current code uses AVSampleBufferDisplayLayer, CM format descriptions built from parameter sets, display-link pacing and layer recreation on failure. Retain complete fresh native bootstrap and safe parameter generations; no Apple display-layer abstraction on PS5. |
| [Switch FFmpeg decoder](https://github.com/XITRIX/Moonlight-Switch/blob/a75fee223914f36ec26b29739d95331307960fa3/app/src/streaming/ffmpeg/FFmpegVideoDecoder.cpp) / Deko helpers | Backend-specific hw formats/ownership, explicit fallback and slice-threading support checks. Deko requires NVTEGRA hardware decode; fallback/transfer behavior is platform-dependent. Preserve native VideoDec2-to-AGC ownership rather than copying desktop frame-transfer paths. |
| [Embedded FFmpeg](https://github.com/moonlight-stream/moonlight-embedded/blob/f32e415aea6797d261d6b470dcf8bf18727341c2/src/video/ffmpeg.c) / video.h / VAAPI | Limelight callbacks separate platform-specific decoding/rendering; software slice threading and hardware single-thread behavior differ. Our native owner remains that platform boundary. |

The checked-in Limelight interface exposes initial bitrate and slices/capabilities
through StreamConfig/RTSP and LiRequestIdrFrame for refresh. No verified generic
live bitrate/resolution/FPS setter was found in the pinned public API or the
installed Vibepollo2.0.1 path. Do not invent a host-specific control protocol or
silently lower user settings. The host applies requested slices where supported;
actual NAL slices must still be observed. Capture GPU identity is not encoder identity.

## Evidence and rejected shortcuts

The external Intel report shows functional VAAPI/QSV H.2641440 decoding, not
sustained60fps for both: VAAPI decode6155us fits a16.7ms budget and the tester
reports host production around51fps; QSV decode19786us exceeds that budget.
Actual slices, payload bitrate and source production were not supplied for QSV.
The encoder brand alone therefore cannot explain or select a configuration.

Existing project research (PERFORMANCE_ROUND_3/4) found a large one-slice cost,
no demonstrated benefit from widening the CPU mask, and depth3 output-ready latency
around33.7ms versus5.5ms at depth1 with unchanged60fps throughput. Increasing
threads/depth is not a general optimization. A client cannot split arbitrary
compressed slices safely. Host slice/WPP cooperation may be required.

## Characterization and diagnostics

Supported SPS parsing remains authoritative for codec/profile/level, coded size,
bit depth, DPB/reference and reorder requirements. Optional bounded PPS-prefix
inspection records H.264 CABAC and HEVC tiles/WPP without altering validity,
allocation or recovery acceptance. Unknown prefix syntax stays unknown. H.264
slice types and sampled actual slice counts are observed; HEVC slice count is
not a substitute for WPP/tiles. AU byte distributions and actual byte rates are
independent of the configured bitrate ceiling.

Two-second decode-owner snapshots reuse existing counters. Presenter values read
by the policy are atomic; single-writer histograms are not read across threads.
Queue conservation estimates arrivals only when no local queue discard occurred.
It is not host capture FPS. Network loss plus local pressure remains ambiguous.
Low source FPS with no backlog and matching delivery is healthy. A slow Decode
call alone, especially with asynchronous depth, does not prove output latency.

Test Lab adds measured-phase host sender counter bounds, observed AU arrival,
decode output and unique presentation rates, queue delay, AU bytes and
ready-to-flip timing. It labels a suspected stage with explicit evidence and
uncertainty. Host-limited requires independent source-rate evidence; capture
versus encoding remains unresolved without corresponding timestamps. Existing
verdict thresholds are unchanged.

## Initial planning

Use the existing SPS plan, clamp the requested native depth to available input/
frame slots, and retain the requested CPU mask with existing native Query/Create
fallback. The separate native output margin is still an operational assumption,
not a modification of the standard DPB formula. No speculative core expansion,
HEVC-specific H.264 slice rule, or new buffering is introduced.

## Validation budget and host setup

Final hardware scope is one software H.2641080p60 run and one known-good NVENC
regression through Test Lab, after implementation and one final SDK build.
Only an important observed uncertainty can justify another run. Mac VideoToolbox
is optional; an existing local Sunshine installation was found, but pairing/
permission work is not a prerequisite. Independent Intel review remains pending
for the new policy; do not contact the contributor during implementation.

Windows has an i5-14600KF and RTX5070Ti, with no enumerated Intel iGPU. Vibepollo
2.0.1 supports per-app encoder overrides. A separate "ProsperoLight x264 Lab"
profile selects software/libx264, veryfast, zerolatency and min_threads4; all
original app objects and list indices were verified unchanged after restoring
the host's automatically sorted list order. Global NVENC settings are unchanged.
Host log selection, not advertised readiness, must confirm the actual encoder.
