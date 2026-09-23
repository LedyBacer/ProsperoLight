# Round 2: performance experiments

Next iteration: [Round 3 — sustained 4K120, latency and efficiency](PERFORMANCE_ROUND_3.md)
contains the proposed follow-up work; it is not an implemented release feature.

Status: **bounded presentation overlap promoted for `01.000.060`** after the
wired 1080p120 and 4K120 user comparisons below. The user approved publication;
repeatability and the full codec/HDR/audio matrix remain follow-up work, not
completed qualification. Other candidates remain disabled. Do not compare these
builds using Chiaki video: use the TV directly and the local session summary.

## What is implemented

1. **Measurement and correctness first.** One-second, local-clock callback and
   completed-presentation rate windows replace lifetime FPS in the overlay.
   Enqueue-to-callback delay now excludes copy/decode/presentation. Decoded and
   presented samples have separate denominators, so skipped pictures no longer
   inflate the decode-ready average. Sampled queue high-water marks and bounded
   timing histograms are collected without allocations or per-frame file I/O.
   Aggregates are read only after the relevant stream workers have joined.
   First-packet receive → complete-frame enqueue is measured separately from
   enqueue → callback, so packet delivery/reassembly delay is not called decode
   time. Missing or inconsistent timestamps are counted, not recorded as zero.
2. **FEC SIMD candidate.** Use nanors' existing SSSE3/AVX2 implementations, selected
   by CPUID. AVX2 additionally requires XSAVE, OSXSAVE, AVX, and enabled XMM/YMM
   state in XCR0. Unsupported CPUs fall back to scalar; AVX-512 and GFNI stay off.
   This avoids requiring compiler-rt's unavailable CPU-feature globals.
3. **Opus and audio catch-up candidates, independently selectable.** Opus can use
   its existing x86 SSE/SSE2/SSE4.1 paths; SSE4.1 keeps runtime detection. AVX2 is
   deliberately excluded because this pinned Opus detector does not check XCR0.
   The audio catch-up candidate decodes every compressed packet, including PLC,
   but discards PCM when upstream audio exceeds 30 ms. It also clears the local
   pending PCM ring, preserving decoder history and stereo/5.1 channel mapping.
4. **Bounded presentation overlap (release default).** Decode the next picture while one
   previous native flip is outstanding. Before reuse, wait for any source still
   owned by that flip; before writing shared command/constant/overlay memory,
   retire the previous submission. The first picture and connecting animation
   remain synchronous. There is no new render thread, no extra copy, no deeper
   Videodec2 pipeline, and at most one outstanding native submission. Presentation
   counters advance on observed completion, not submission. A flip timeout retains
   the still-owned allocations and requires an app restart before another stream.
5. **Pacing experiment.** Compare 200 µs versus the existing 500 µs HFR flip polling
   interval. Keep the approximate 100 ms HFR wait budget independent of that
   interval. This tests wakeup overhead versus completion-observation delay; it
   does not establish a faster scanout or replace the wait with an unverified API.
6. **Fractional-refresh negotiation.** Snapshot the actual VideoOut refresh after
   the first connecting frame, before its animation worker starts, and set the
   SDP refresh hint before either a launch or resume. A 120 FPS request on 119.88 Hz sends
   `clientRefreshRateX100=11988`; 60 FPS on that output sends `5994`. A 90 FPS
   request remains `9000` on 119.88 Hz (or becomes `8991` on an 89.91 Hz output).
   Unknown, slower, or non-integral output rates retain the requested nominal
   rate. This hint does not change the user's FPS, resolution, bitrate, or codec,
   and does not guarantee that every host honors fractional refresh.
   HTTP launch/resume still requests the nominal mode; the fractional hint is
   carried by Moonlight Common's subsequent RTSP/SDP negotiation. Sunshine checks
   that this hint stays within 1% of the requested streaming FPS.

Stale presentation is skipped only if a newer compressed frame is waiting. The
decoder still processes reference frames, and a 100 ms progress guard prevents
an endless drop loop. This correction and instrumentation are also in the
instrumented baseline. It is therefore not identical to the published release.

## Build and host checks

Run in Linux/WSL from the repository root. These commands do **not** upload,
launch a title, acquire a console lock, or start Remote Play.
Use GNU Make 4.3 or newer; grouped archive targets prevent concurrent rebuilds
from overwriting the same dependency directory when using `make -j`.

```sh
make test
make test-stream-performance
make lint
make performance-candidates
```

The candidate builder creates a timestamped directory under `results/`, with a
complete `PPSA99002/` folder, `options.txt`, file-level `SHA256SUMS`, and a build log
for each variant. It ends by rebuilding the release defaults and checking that
its `eboot.bin` matches `04-presentation-overlap`. No version/tag/release is
changed. The explicitly named `00-baseline` continues to disable overlap for A/B
comparisons; it is no longer the ordinary build default.

| Hardware comparison order | Folder | Changed build options |
| --- | --- | --- |
| 0 | `00-baseline` | All experimental options off; 500 µs polling |
| 1 | `01-fec-simd` | `FEC_SIMD=1` |
| 2 | `02-opus-simd` | `OPUS_SIMD=1` |
| 3 | `03-audio-catchup` | `AUDIO_MAX_BACKLOG_MS=30` |
| 4 | `04-presentation-overlap` | `PRESENT_OVERLAP=1` |
| 5 | `05-flip-poll-200us` | `FLIP_POLL_US=200` |
| Last, only after individual passes | `06-combined` | FEC + Opus + catch-up + overlap; 500 µs polling |

Release defaults are `PRESENT_OVERLAP=1` and `FLIP_POLL_US=500`; FEC SIMD, Opus
SIMD, audio catch-up, LAN telemetry and streaming self-tests remain off.
Dependency-option changes
invalidate the active streaming archives before packaging; a subsequent ordinary
`make app` restores the default dependency options. Do not enable all experiments
in the public workflow based on host results alone.

The existing developer-only `STREAM_SELF_TEST_FPS=120` autostart can be used for
headless smoke tests on an already-paired host. It forces HEVC SDR at 80 Mbps and
the selected `STREAM_SELF_TEST_RESOLUTION`, starts the first advertised app, and
now disconnects through normal teardown after 90 seconds. Leave this option at
zero for user-test folders and release builds. A static desktop smoke test proves
neither gaming performance nor channel correctness; retain the workload matrix
below for qualification.

Host tests exercise:

- One-second FPS windows, histogram percentiles/overflow, audio threshold, and
  stale-video progress behavior; reassembly timestamp validation and the
  60/90/120 FPS × nominal/fractional/unknown output-refresh matrix.
- The actual native source-reuse/retirement guard against mocked VideoOut,
  including timeout/retry behavior. This is **not GPU execution validation**.
- The actual session JSON serializer with short writes, failed writes, and a
  failed close; an incomplete report must not replace the last completed one.
- Exact nanors parity and recovery at 1, 63, 64, 1392, 1440, and 4096 bytes, with
  one through four lost shards, using scalar and locally supported SIMD dispatch.
- The pinned Opus upstream tests for both builds and an identical packet corpus
  decoded as stereo and 5.1, at 5/10 ms packet duration, including packet-loss
  concealment. Floating-point PCM is not bit-exact: the interop check requires
  at least 75 dB signal-to-difference energy and at most 32 signed-16-bit LSB
  peak difference. These are test tolerances, not listening-test claims.

## Session receipt and interpretation

After an orderly stream teardown, the app writes:

```text
/download0/moonlight/performance-last.json
```

This is the title's private data path. Resolve its backing path for the deployed
title when collecting it; it is not the shared `/data/homebrew` app directory.
The temporary file is renamed only after the complete write and close succeed.
The next completed stream replaces it; collect it after each comparison. A crash
or failed write may leave the previous receipt. Check the options and counters
before attributing it to a run. No receipt is created without a decoded frame.

The receipt includes stream settings, actual output dimensions/refresh, the
negotiated `client_refresh_x100` hint, `reassembly_invalid_samples`, selected
FEC dispatch, experiment flags, frame/drop counts, sampled upstream video/audio
queue high-water marks, local audio ring high-water, audio catch-up counts/errors,
and count/mean/p95/p99/max for:

- Copy and decoder-call duration.
- First-packet receive → complete-frame enqueue (`receive_to_enqueue`). This
  includes delivery spacing, reordering/FEC and depacketization work; it is not
  a pure FEC CPU timer or a one-way network-latency measurement.
- Upstream enqueue → renderer callback, and callback → decoded picture ready.
- Upstream enqueue → **CPU-observed** flip completion and intervals between those
  observations, including the longest observed interval.
- Input polling intervals, Opus decoding, and AudioOut call duration.

Percentiles are conservative 0.5 ms histogram bucket upper bounds; values beyond
127.5 ms use the exact session maximum for that bucket. Queue peaks are sampled,
not exhaustive. The overlay's incoming FPS counts callbacks serviced by the
decoder, **not an independent network-receive-thread counter**. Output cadence
and frame age are CPU observations; overlapping decoding can delay observation
of a flip that already occurred. They are not GPU timestamps or input-to-photon
latency. True end-to-end latency still needs an external camera/photodiode test
or independently validated clock correlation. UI/input histograms contain no
key values, passwords, host addresses, or streamed content.
Histograms cover the entire session, including startup/warm-up; the summary
does not currently provide a separate steady-state sampling window.

## Release checkpoint — 01.000.060

On 2026-09-10 UTC, the user approved shipping the isolated presentation-overlap
path and deferring other performance enhancements. The following comparison was
collected from PS5 firmware 6.02 over Ethernet, viewing the TV directly. Both
runs requested 3840×2160, 120 FPS, HEVC SDR, 80 Mbps and stereo, with native
3840×2160 output at 119.88 Hz and a refresh hint of 11988. The intended workload
was the same Hollow Knight area; exact scene and host engine FPS were not
independently measured.

| Whole-session measurement | Instrumented baseline (overlap off) | Overlap on |
| --- | ---: | ---: |
| Decoded / presented frames | 16,769 / 12,686 | 14,058 / 14,058 |
| Stale presentation skips | 4,083 (24.3%) | 0 (0%) |
| Sampled pending video queue peak | 5 frames | 0 frames |
| Receive-to-enqueue mean | 0.387 ms | 0.411 ms |
| Enqueue-to-callback mean | 5.468 ms | 0.016 ms |
| Enqueue-to-callback p99 upper / max | 22.999 / 46.895 ms | 0.499 / 7.014 ms |
| Decoder-call mean | 6.869 ms | 6.934 ms |
| Decoder-call p99 upper / max | 9.499 / 11.179 ms | 9.499 / 11.388 ms |
| CPU-observed completion interval mean | 12.878 ms | 9.884 ms |
| CPU-observed completion interval p99 upper / max | 75.499 / 117.217 ms | 31.499 / 83.063 ms |
| Enqueue-to-CPU-observed completion mean | 12.560 ms | 16.854 ms |
| Enqueue-to-CPU-observed completion p99 upper | 24.999 ms | 35.999 ms |
| Input polling interval mean | 4.130 ms | 4.134 ms |
| Audio backlog / local PCM ring peak | 15 ms / 480 frames | 15 ms / 480 frames |

Both runs completed with result zero, no reported network frame gaps or invalid
reassembly timestamps, and no audio decode/output errors or ring overruns.
The user reported lots of stuttering before and much smoother playback after.
Mean callback queue wait fell about 99.7%; hardware decoder-call time was
essentially unchanged. This supports a queueing/presentation improvement rather
than a decoder speedup. A separate 1080p120 pair also felt smooth in both modes:
11,143/11,143 and 17,048/17,048 decoded/presented frames, with no stale skips;
mean callback queue wait changed from 2.440 to 0.042 ms.

**Limits:** this is one 4K pair, not three alternating repeats or qualification
of every codec, HDR, audio, firmware or network configuration. The instrumented
baseline includes the new measurement and stale-drop logic and is not the exact
public `01.000.055` binary. Histograms include startup and menus. The approximate
observed spans were 163.357 and 138.939 seconds; their derived whole-session rates
of 77.65 and 101.17 FPS are not steady-state gameplay rates or proof of locked
120 FPS. They should not be advertised as a controlled FPS percentage gain.

CPU-observed frame age increased: overlap can observe the previous completion
after the next decode, and the baseline excludes its 4,083 skipped pictures from
the age distribution. These are not GPU or scanout timestamps. The larger tail
must not be dismissed, but neither lower nor higher input-to-photon latency is
established by it. No promise is made about the reported Wi-Fi/high-bitrate
slideshow threshold; bitrate and network conditions still need individual tuning.

The small local JSON receipts were hash-verified before their containing private
data images were removed. SHA-256: baseline
`fc346f283663c3d6c5d28f204b5fda7e49aa3d4a592770f84e05aea4d636d418`;
overlap `85d70d41c83008706e2524af99eff4eb0353c327df06553717f3ede6b302d326`.
Exact-title teardown, runtime-layer release and WSL service health passed.
No pairing files or streamed content are published with this report.

## Further hardware qualification

The initial smoke checks below do not qualify an optimization. Verify the
instrumented baseline, receipt creation, and repeatability. Use the console lock
protocol applicable to that PS5, WSL for transfers, and a folder deployment. Do
not replace the public package or pairing identity just to run an experiment.

For each comparison: wired Ethernet, identical host workload/display mode,
resolution/codec/HDR/bitrate, and TV port/settings. Stop the host game, choose the
desired stream settings, then start the game fresh. Warm up for 30 seconds, run
the same scene for 60 seconds, and collect at least three baseline/candidate
pairs. Alternate their order to expose thermal/host-load bias. Include:

1. 1080p120 and 4K120 motion, plus 4K60 as a lower-pressure control.
2. HEVC SDR first, then H.264 and HDR. Check all edges and the entire framebuffer;
   1440p should still scale to a complete 4K output.
3. Stereo and 5.1 channel identification, reconnect, and Windows login transitions.
4. Overlay on/off, USB mouse/keyboard, controller, all shortcuts, repeated return
   to UI/reconnect, and clean title shutdown.
5. For FEC, an isolated controlled-loss stream/replay as well as a clean LAN.
   Do not introduce packet loss globally on the user's normal network.
6. For audio catch-up, a bounded stall followed by recovery: backlog must shrink,
   playback must not stay delayed, and clicks/channel faults must be checked by
   listening. Discarding PCM is expected only when above the chosen budget.

Retain a candidate only if repeatable frame-age/queue/interval or CPU improvements
outweigh extra drops/wakeups, with no visual/audio/input/lifecycle regression.
Average FPS alone is insufficient. On any native timeout, stop that run, preserve
the receipt/log, and restart the app; do not keep streaming through a failed fence.
Only combine candidates that passed independently. Rerun the full supported
resolution × FPS × codec/HDR matrix before claiming broad qualification; the
limited `01.000.060` release evidence is stated separately above.

### Initial validation — 2026-09-06

Host validation passed: 26 GoogleTests, 43 integration tests, the actual-source
presentation-lifetime and JSON-writer guards, both Opus upstream suites (5/5
each), exact scalar/SIMD FEC parity/recovery, and lint. All seven native candidate
folders built and the final ordinary build matched the frozen baseline.

One headless baseline stream ran for 90 seconds on firmware 6.02. The receipt
confirmed HEVC SDR, 1920×1080, a 120 FPS request, 80 Mbps, and 119.88 Hz output.
It completed with result zero, 2,849 pictures presented, no reported network
frame gaps or stale drops, 1.359 ms mean decode time, and 5.939 ms mean
enqueue-to-CPU-observed-flip time. No audio packets were decoded. The delivered
cadence was approximately 32 FPS, so this is a lifecycle/measurement smoke pass,
**not** 120-FPS gaming, audio, end-to-end latency, or optimization qualification.
Exact-title teardown and service health passed; the original console files were
restored from verified backups and only the run's own lock token was released.

The isolated FEC SIMD smoke test also completed with result zero, selecting AVX2
and presenting 2,861 pictures without reported frame gaps or stale drops. This
was **not a performance win**: mean decode time was 5.568 ms and observed frame
age was 10.168 ms, versus 1.359/5.939 ms in the first baseline. The source workload
and environment were not controlled enough to attribute the difference to SIMD.
Keep FEC disabled pending repeatable A/B comparisons and controlled-loss recovery
tests. No audio packets were decoded in either smoke test, so neither validates
Opus SIMD, audio catch-up, or surround playback.

An identical baseline repeat then presented 2,863 pictures with result zero and
no reported gaps/drops. Its mean decode/observed-frame-age timings were
5.516/10.087 ms, almost identical to the FEC run. The elevated timings therefore
were not unique to FEC. These three approximately-32-FPS sessions demonstrate
measurement/lifecycle operation, but neither a FEC speedup nor a FEC-specific
regression. All three cycles restored the original console files and passed
postflight health after exact-title teardown. Other experiments remain host-only
until a repeatable motion-and-audio workload is available.

These early local samples were subsequently removed during project disk cleanup;
the historical summary above remains. Do not publish download-data images: they
also contain private pairing material.

### Xbox-inspired follow-up — 2026-09-08 (offline)

Added receive-to-enqueue measurement and fractional-refresh negotiation, using
the existing histogram and reported VideoOut status rather than another worker
or queue. Host checks passed: 28 GoogleTests, 43 integration tests, actual-source
presentation-lifetime/JSON-writer guards, lint and Clang static analysis.

Fresh baseline and isolated presentation-overlap folders are prepared under
`results/xbox-followup-20260909T005452Z-PWnghM/`, with build logs, file-level
checksums and a comparison checklist. Streaming autostart is disabled in both.
Both native builds passed; the ordinary output was rebuilt as the baseline and
matched its frozen executable. Other experimental options remain disabled.
The user deferred console interaction: neither folder has been uploaded or
hardware-tested, and no improvement is claimed from these offline checks.

## Deferred until evidence supports them

- Display-aware deadline pacing: first compare the existing one-flip overlap
  path in repeatable alternating tests against the instrumented baseline.
  Xbox's DXGI/vblank APIs and its queue sizes are not PS5 contracts. Do not add
  an extra frame queue or render worker merely to reproduce a settings label.
- Deeper decode queues, a new render worker, and multi-submission GPU pipelining:
  unnecessary for the one-flip overlap experiment; previous deeper decoder tests
  increased live latency.
- Event-driven VideoOut waits: require verified notification ABI and completion
  semantics; do not infer them from a PC/Android implementation.
- Socket poll/epoll allocation, receive-buffer sizing, and cached AGC state/flush
  ranges: profile after the above trials instead of changing them simultaneously.
- Encoder slice/WPP changes: retain four slices, then compare controlled host
  encodes if decoder timing is still the bottleneck. More slices can cost bitrate;
  the host encoder may not expose the same controls as a software benchmark.
- Automatic bitrate/resolution changes: preserve the user's explicit settings.

## References

- [Moonlight Qt frame pacer](https://github.com/moonlight-stream/moonlight-qt/blob/master/app/streaming/video/ffmpeg-renderers/pacer/pacer.cpp)
- [Moonlight Qt SDL audio backlog policy](https://github.com/moonlight-stream/moonlight-qt/blob/master/app/streaming/audio/renderers/sdlaud.cpp)
- [Moonlight Xbox reference revision](https://github.com/TheElixZammuto/moonlight-xbox/tree/50c02fd0e7232bbae187d46d08f0df5a022a1f45): fractional-refresh hints, stage timing, and display-aware scheduling inform these experiments; Direct3D/DXGI code is not ported.
- [Sunshine RTSP refresh-hint validation](https://github.com/LizardByte/Sunshine/blob/master/src/rtsp.cpp)
- [Opus build configuration](https://github.com/xiph/opus/blob/main/CMakeLists.txt)
- [PS5 hardware video decoding research](https://github.com/blackbearreloaded/ps5-hardware-video-decoding-research)

The Qt references guide queue/drop policy, not PS5-specific GPU lifetime, Android
power-management choices, or undocumented platform capabilities.
