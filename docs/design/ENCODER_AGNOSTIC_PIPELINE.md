# Encoder-independent native video policy

[Final engineering review and hardware evidence](ENCODER_AGNOSTIC_VALIDATION.md).

This extends the SPS/PPS-aware VideoDec2 owner from PR61. Encoder names are
never configuration keys. The NVENC-compatible default remains depth1, the
requested CPU mask, existing slice negotiation and the same AGC surface ownership.
No FFmpeg decoder, pixel conversion, additional frame queue or PyroWave change.
Automatic depth reconfiguration is disabled by the existing
`MOONLIGHT_EXPERIMENTAL_DECODER_CONTROLS=0` gate until a real native depth
transition is validated. Diagnostics and bounded retained-instance IDR resync
remain available. The summary exposes `stream_policy.reconfiguration_enabled`.

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

## Bounded runtime actions

The pure RuntimePolicy consumes two-second windows. At least two consecutive
active pressure windows (>=4s) with backlog and native service/buffered-output
pressure are required. One slow IDR, sparse desktop, source FPS below target,
network-loss ambiguity or a fault/recovery never independently triggers tuning.

For depth>1, zero-reorder streams with measured buffered-output latency may try
depth1 once. This is a latency trial, not a promise of higher throughput. It
waits for an actual IDR and fresh matching SPS/PPS, queries the candidate before
retiring resources, and uses the existing single-owner presenter/GPU join barrier.
If Query refuses it, the old instance stays. If creation fails after retirement,
the same bounded creation path tries the prior accepted depth. A usable old-instance
IDR without fresh headers cancels the optional trial rather than treating native
knowledge as bootstrap for a new instance. New/reset decoders still require fresh
complete parameter sets.

After startup exclusion, comparable source-rate/payload windows must demonstrate
lower queue+output-ready age, no delivery loss and no queue growth to keep the
candidate. A worse candidate, or no demonstrated benefit within20s after successful
configuration, requests one safe-boundary rollback; then depth is locked for the
session. The previous mode can only be restored within native Query/Create limits.
Stream-generation changes invalidate comparison. No repeated trial/rollback cycle.

At depth1, sustained queue age above max(100ms,4 frame budgets) can request one
IDR to discard obsolete queued AUs, using the retained native instance and its
actually submitted sets. This is latency recovery, not additional throughput.
No native recreation or memory growth is needed. The session budget is one
latency resync, one depth trial and one rollback; a persistent deficit is reported
as exceeding observed throughput, with no bitrate/resolution/FPS reduction.
The existing packet-independent transition deadline, stop/cancel and native error
budgets remain in effect. Policy counters distinguish attempts, rejection, kept
candidate, rollback requests and policy-discarded AUs.

Optional depth changes preserve the **effective** validated DPB capacity, including
any larger capacity accepted by native recovery; they never shrink back to the
initial SPS minimum as a side effect. Native rejection and comparison uncertainty
are observable rather than silently declared performance improvements.

Native faults invalidate trial evaluation and lock further depth tuning for that
session; parameter identity includes both SPS and PPS generations. A changed PPS
cannot be compared against an older CABAC/tile/WPP baseline. Depth-only trials
also retain the already accepted effective CPU affinity rather than retrying a
larger previously rejected requested mask.

## Final review corrections

The 20s absolute trial lifetime is checked before measurement eligibility; the
first 6s remains excluded from comparisons. Packet loss, fewer than 16 submitted
frames, invalid windows and an uncomparable payload/rate cannot extend the
lifetime or prove improvement. Sampler resets still reach deadline enforcement.
During a source pause the decoder owner can remain blocked normally; the first
safe observation after resumed input schedules at most one rollback. Existing
IDR/fresh-bootstrap requirements, native-fault invalidation and transition/stop
budgets remain unchanged. An unavailable/incomplete safe boundary does not
justify replaying cached sets or repeatedly recreating the decoder.

Test Lab preserves `observed_arrival_fps` and `actual_compressed_mbps` as JSON
null when any trace row lacks a valid translated arrival timestamp. It records
availability and invalid-row count, marks input completeness false, and returns
`insufficient-evidence` for the full pipeline. Submission/output/presentation
rates and an explicitly scoped `client_delivery` observation remain useful.
Independent host counters do not substitute for missing client arrival evidence.
Zero observed arrivals is reported numerically only with valid timestamp
evidence, and cannot make queued submissions trivially prove a healthy pipeline.
The added diagnosis fields have no arithmetic consumers outside this module;
JSON report/export paths preserve null. Acceptance thresholds are unchanged.

The existing Test Lab mechanisms expose capacity-error probes and requested
initial modes, not a controlled native depth trial plus rollback. In the earlier
native tests the depth1 healthy path never exercised recreation; native acceptance
of a deeper starting mode cannot be assumed. Rather than add a new injection
protocol/test framework in this correction pass, automatic depth trials remain
behind the existing experimental gate. Deterministic policy and native-owner
mocks are retained. A real native transition remains necessary before enabling
this gate in production; no new PS5 launch is claimed here.
