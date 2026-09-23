# Round 3: sustained 4K120, lower latency, less CPU overhead

Status: **six client-side work areas implemented for development; new paths
still require hardware qualification**. Plan reviewed against release
`01.000.060`; implementation checkpoint updated 2026-09-22. Earlier probe
sessions and the failed one-slice experiment are evidence, not qualification
of the new asynchronous decoder or pacing modes.

## Six-area implementation checkpoint

These are build-time development controls, not extra end-user settings. No
Vibepollo settings are changed. The safe default remains four slices, depth one,
all presentations, no added pacing wait, worker affinity `0x3f` / priority `700`.

| Area | Implemented | Control / qualification boundary |
| --- | --- | --- |
| 1. Decoder parallelism | Request 1–8 slices and sample actual Annex-B VCL counts without retaining pictures | `VIDEO_SLICES_PER_FRAME=4`; compare 8 separately. A request is not proof the host honored it. WPP/tile negotiation is not fabricated or enabled. |
| 2. Worker scheduling | Pass bounded affinity/priority choices through the existing Videodec2 config and native query/create validation; persist requested values in reports | `DECODER_CPU_AFFINITY=0x3f`, `DECODER_CPU_PRIORITY=700`. Restricted to subsets of the already-used six-core mask and numeric priorities 700–767; this is a conservative experiment envelope, not a claim of the platform's full supported range. No real-time priority escalation or extra worker pool. |
| 3. Bounded async decode | Depth two retains delayed output instead of flushing every AU; depth-sized ownership budget, extra protected source slots, FIFO trace association, reset-on-error, allocation retention on failed decoder deletion | `DECODER_PIPELINE_DEPTH=1` or `2`. No B-frame/reordering support is claimed. `decode` is call wall time; `callback_to_ready` includes the pipeline delay. Depth two is opt-in, not automatically faster. |
| 4. Decoder/render contention | Publish immutable shader/resource data once; per-frame flush shrinks from 64 KiB to the 22 KiB mutable register/constant/command bank, with existing completion guards intact | `PRESENT_EVERY_N=1`; diagnostic 2/4 decimates only presentation, never predictive decoding, to isolate shared render pressure. Decimation is not a full-rate performance win. |
| 5. Receive efficiency | Stack storage for common polls of up to eight sockets; preserve heap fallback and cleanup; packet/byte/error and requested/actual receive-buffer counters | Detailed counters require `PERFORMANCE_DETAIL=1`. Counts cover the title's UDP receives, not video-only traffic or physical packet loss; no per-packet logs. Existing CPU-gated FEC/Opus SIMD remains selectable. |
| 6. Pacing/input latency | Optional one-deadline pacing with fractional refresh accounting, bounded waits and immediate abandonment of backlog; poll cadence subtracts work time and yields on overrun | `FRAME_PACING=0/1`, `INPUT_POLL_US=1000..4000` (default 4000). New development candidate uses 2000. Pacing is local arrival smoothing, not a hardware scanout timestamp or proof of lower input-to-photon latency. |

The one-slice run was a regression: 2,456 stale presentations out of 3,680 AUs
(66.74%), mean decoder-call time about 13 ms and mean queue delay 55.9 ms.
The previous four-slice session averaged about 6.64 ms decoder-call time and
9 microseconds queue delay. Sessions were not matched captures, but the result
is sufficient not to promote one slice. Four slices were restored.

The next conservative build combines the actual receive/flush/input reductions
with the existing SIMD and 200-us flip-poll candidate. It does **not** enable
all experimental policies at once:

```sh
make test
make lint
make app FEC_SIMD=1 OPUS_SIMD=1 PERFORMANCE_DETAIL=1 FLIP_POLL_US=200 INPUT_POLL_US=2000
```

For isolated follow-ups append only one of `VIDEO_SLICES_PER_FRAME=8`,
`DECODER_PIPELINE_DEPTH=2`, `DECODER_CPU_AFFINITY=0x3e`,
`DECODER_CPU_PRIORITY=720`, `PRESENT_EVERY_N=2`, or `FRAME_PACING=1`.
An accepted config is not proof of better scheduling; compare the same scene,
arrival rate, actual slice count, ready latency, decode tails, queues and audio.
Keep the separate four-slice rollback artifact. Upload only while the app is
closed, through WSL under the exact-token lock; do not start a stream remotely.

Offline checks exercise the real delayed-output callback, pool wrap, error/reset
and never-ready bounds; socket stack/heap/error cleanup; fractional pacing,
input wait bounds, presentation ownership and JSON serialization. Native builds
verify linkage, not PS5 speed. No gain in decoder execution time, sustained
120 unique frames/s, or advantage over Xbox is claimed by this checkpoint.

This continues [Round 2](PERFORMANCE_ROUND_2.md). Keep its bounded presentation
overlap, ownership checks and measurement helpers. No rendering-backend rewrite
is proposed. The objective is more consistently delivered frames and lower
input-to-photon latency at the **same image quality**, not a better-looking FPS
counter. Outperforming Moonlight Xbox is an aspiration to test, not an established
result or a promise that software can overcome every hardware difference.

## Offline preparation checkpoint

The first slice adds a shared LLVM 18 selection for the app and streaming
dependencies, cache invalidation when compiler/source/options change, and a
`build/build-provenance.json` receipt with compiler/input/executable hashes.
GitHub Actions installs the matching LLVM development tools and archives the
receipt separately, without changing the three public release assets. CI has
not been triggered for these local changes. Matching local/CI binary output
still needs verification; selecting one LLVM major alone is not that proof.

`PERFORMANCE_DETAIL=1` adds nine bounded timing histograms to the existing
post-session report. Ordinary builds default to `0`; additional timing samples
are then absent (count zero), not measured zero-duration events. JSON schema 2
keeps the ten earlier timing names and adds:

| Field | Measured boundary |
| --- | --- |
| `source_slot_wait` | Call protecting the next decoder source slot, including immediate returns |
| `flush` | Videodec2 flush calls, separate from decode calls |
| `present_call` | Whole native presenter API call; contains several of the stages below |
| `completion_wait` | Stream owner's outstanding-flip completion call, including failures |
| `host_processing` | Host-provided processing latency, converted to microseconds; missing values omitted |
| `agc_prepare` | CPU construction of render state/commands before final cache flushing |
| `agc_cache_flush` | Per-frame mutable shader-state/command range flush, not initial allocation clearing |
| `agc_submit` | Driver submission plus suspend-point call; **not** GPU execution time |
| `overlay_refresh` | Keyboard/HUD rasterization and its cache flush, excluding the preceding ownership wait |

The report also includes compressed `stream_bytes`, `flush_calls` and wait-loop
flip-query/sleep/timeout counts. The counters are not all VideoOut queries in
the app. Presenter statistics reset after the loading owner stops and are read
after the stream worker joins. No new queue, worker, per-frame I/O or change to
decoding, cache-flush policy or frame pacing is introduced by instrumentation.

Build the first isolated comparison set, without contacting a console:

```sh
make test
make lint
make performance-round3-candidates
```

| Candidate | Detailed timing | Flip polling | Overlap |
| --- | --- | --- | --- |
| `00-control` | Off | 500 microseconds | On |
| `01-stage-timings` | On | 500 microseconds | On |
| `02-poll-200us` | On | 200 microseconds | On |

Each frozen folder has its build receipt, options and file checksums. The builder
finishes with another ordinary control build and checks its executable against
`00-control`. These are local development builds with the existing title/version;
they do not replace the exact published `.060` baseline. First compare published
`.060` with the new control, then instrumentation off/on, then polling.

This is **not completion of every measurement in phase 0**. The reports still
cover whole sessions. Automatic warm-up/window separation, packet-arrival/FEC
instrumentation, CPU-time accounting and keyframe/size correlation remain
follow-ups. Do not describe the current aggregates as isolated steady gameplay
or physical display latency. The later six-area checkpoint above implements
decoder and pacing controls; hardware measurements are still required.

## What the evidence says

The first paired Hollow Knight test used Ethernet, HEVC SDR, 4K120 requested,
80 Mbps and stereo, with native 3840x2160 output at 119.88 Hz.

| Measurement | Before overlap | With overlap, promoted for .060 |
| --- | ---: | ---: |
| Mean enqueue-to-callback delay | 5.468 ms | 0.016 ms |
| Decoded pictures skipped as stale | 4,083 / 16,769 (24.35%) | 0 / 14,058 |
| Mean decoder-call duration | 6.869 ms | 6.934 ms |
| Decoder-call p99 histogram upper bound | 9.499 ms | 9.499 ms |
| CPU-observed flip-interval p99 upper bound | 75.499 ms | 31.499 ms |
| User report | Lots of stuttering | Much smoother |

At 119.88 Hz, a frame interval is **8.342 ms**. Average decoder-call duration
already occupies about 83% of that interval, and its tail exceeds the interval.
The improvement removed serial presentation backpressure; it did not materially
accelerate decoding. Further queue trimming alone has little average headroom
in this particular 80 Mbps run.

These were whole-session aggregates, including loading and menus, from one pair
with different durations. Source FPS and exact scene equivalence were not
independently verified. They establish neither sustained 120 unique gameplay
frames/s nor a universal performance gain. Observed frame age increased from
12.560 to 16.854 ms: completion can be observed after the next decode, and the
baseline excludes skipped frames. This needs better timing, not dismissal or a
claim about input lag. Zero reported network frame gaps also does not exclude
FEC-recovered loss or packet bursts.

Other useful measurements in the overlap receipt:

- Compressed-frame gathering averaged **0.005 ms**: eliminating this necessary
  copy is not a promising first optimization.
- Opus decoding averaged **0.042 ms**, with a 15 ms sampled upstream audio peak:
  stereo decoding was not the demonstrated video bottleneck.
- Input polling averaged **4.134 ms** between polls: a separate possible
  responsiveness improvement, not evidence of a video-throughput problem.

The earlier native fixed-source tests presented 600/600 frames at approximately
119.88 FPS on firmware 6.02 and 12.70. That proves the presentation capability,
not arbitrary live-stream decoding throughput. See the public
[hardware benchmark definitions and results](https://github.com/blackbearreloaded/ps5-hardware-video-decoding-research/blob/main/docs/benchmarks.md).

## Priorities and execution order

| Order | Work | Reason / promotion condition |
| --- | --- | --- |
| 0 | Reproducible baseline and stage timing | Required to distinguish real gains from different binaries, source timing or measurement boundaries |
| 1 | Trim existing presentation work | Small, local experiments; retain only measurable CPU or pacing improvements |
| 2 | Reduce decode-ready tails and tune encoder layout | Largest demonstrated remaining pressure on the 8.342 ms budget |
| 3 | Investigate the high-bitrate receive path | Targets the reported 80–100+ Mbps cliff; may be a different bottleneck from our wired run |
| 4 | Evaluate pacing policy | Balance freshness and smoothness without accumulating old frames |
| 5 | Audio and input efficiency | Improve responsiveness and resilience without starving video |
| 6 | Repeated qualification and matched Xbox comparison | Establish which improvements generalize and which claims we can publish |

### 0. Freeze the baseline and measure the missing stages

The published CI executable and the locally tested overlap executable have
different hashes. Local dependency compilation resolves to Clang 21, while the
app wrapper uses Clang 18; cross-host reproducibility was not established.
This does not demonstrate a release regression, but it is an uncontrolled
variable. Explicitly pin the dependency compiler as well as the app compiler,
record SDK/dependency revisions and build options, and test the exact CI-built
baseline before attributing new gains. Verify repeat builds or document any
remaining non-performance binary differences.

Extend the existing bounded histograms and post-session JSON, not a new
telemetry service:

- Separate decoder-call duration, any subsequent flush, and matched-frame
  output-ready latency. Flush is currently logged separately but lacks its own
  JSON timing distribution. Record frame type and compressed size buckets to
  determine whether keyframes or large frames explain spikes.
- Separate source-slot waits, AGC command construction/cache maintenance,
  submission, and completion-wait duration. Count polls and thread CPU time
  where supported; a blocking call's wall time is not CPU execution time.
- Distinguish packet/frame arrival rate from decoder callback service rate.
  Retain received, decoded, skipped, submitted and completed denominators.
- Record host processing latency, actual incoming bitrate, requested versus
  observed refresh, effective socket-buffer sizes, FEC recovery activity and
  queue age/peaks. Cross-machine timestamps must not be subtracted without a
  validated clock relationship.
- Add bounded steady-state windows after warm-up and a small one-second summary
  ring. Keep whole-session totals too. No per-frame disk writes, unlimited logs,
  screen capture, keystrokes, credentials or host addresses in reports.
- Use validated VideoOut/GPU completion timestamps only if their meaning and
  API are established. Until then label CPU observations honestly; use external
  display measurements for input-to-photon claims.

Run an instrumentation-on/off control to quantify observer overhead. All Round 3
candidates start from `.060` with `PRESENT_OVERLAP=1`; legacy Round 2 candidates
with overlap disabled are not equivalent baselines for testing a new flag.

### 1. Trim presentation overhead without changing the architecture

Review targets: [`native_agc_present.cpp`](../src/native_agc_present.cpp),
especially `render_frame`, `flush_gpu_data`, `wait_for_marker` and HUD refresh.

1. Run the existing **500 vs 200 microsecond flip-poll** experiment with overlap
   enabled in both builds. Measure CPU/wakeups as well as completion-observation
   delay. Shorter polling is not automatically better. Consider event-driven
   completion only after verifying a suitable native API and its shutdown
   semantics; retain the known-safe wait as fallback.
2. Measure and then cache immutable register/default-state preparation. The
   current path queries/scans defaults and flushes the 64 KiB static shader/state
   range per frame. Separate one-time initialization from genuinely dirty
   constants/descriptors. Flush only proven required ranges, preserving all
   source, command-buffer and GPU-visibility ownership rules.
3. Measure HUD off/on at 4K120. It currently rerasterizes on a 60-frame cadence;
   evaluate updating when the metrics snapshot changes, with a time-based cap.
   Preserve prompt keyboard and notification updates. Do not hide the overlay
   to claim a performance gain.

The decoded video already stays in GPU-accessible surfaces; there is no full
decoded-frame CPU copy to remove. Initial framebuffer clearing is startup work,
not a per-frame 4K clear. Use a fixed-source presentation control, decode-only
control and live combined run to distinguish CPU preparation from actual GPU
work or decoder/render contention.

### 2. Reduce decode-ready time, not just API submission time

Review targets: [`moonlight_stream.cpp`](../src/moonlight_stream.cpp),
`native_submit_decode_unit` and Videodec2 configuration.

- Reuse a short, explicitly approved deterministic test stream to compare
  decoding with and without presentation, then validate against live Sunshine.
  Do not retain recordings of a user's desktop by default.
- Test **1 / 2 / 4 / 8 slices**, where the host encoder supports them. Four is
  already our advertised default; older 1080p results favored four over one.
  Verify actual bitstream layout rather than assuming the capability request
  was honored. Measure ready-time tails, host encode time, actual bytes and
  image quality, separately for H.264, HEVC Main and Main10.
- Inspect WPP, tiles, reference structure and low-delay encoder behavior.
  Earlier controlled HEVC research found a substantial WPP benefit, but that
  does not establish a configurable WPP switch in the user's hardware encoder.
  Only test settings actually supported by the host; no automatic quality or
  bitrate reduction to improve the score.
- Profile worker scheduling before changing `cpu_affinity=0x3f` or
  `cpu_priority=700`. Verify platform semantics and available resources first.
  Any bounded alternative must preserve network, audio, input and UI service;
  do not copy Xbox affinity masks or raise every thread's priority.
- Keep **decoder pipeline depth one** as the latency baseline. A bounded
  depth-two experiment is a later option only if the preceding work cannot
  sustain the source rate. Three surface slots are not a depth-three decoder.
  Do not reduce reference-buffer limits without validating stream requirements.

Why depth is not an easy win: prior live HEVC 4K60 research reduced API submission
from 5.464 to 0.333 ms at depth three, but output-ready latency rose from 5.472 to
33.710 ms, with essentially unchanged 60 FPS throughput. A deeper queue must
improve useful delivery and latency, not merely the number labeled "decode".
Source: [pipeline-depth and live-stream benchmarks](https://github.com/blackbearreloaded/ps5-hardware-video-decoding-research/blob/main/docs/benchmarks.md).

### 3. Explain and improve the high-bitrate cliff

Review targets: [`ps5_sockets.c`](../platform/ps5/ps5_sockets.c) and the existing
Moonlight Common receive/FEC path. Our current wired 80 Mbps evidence does not
explain the Wi-Fi reporter's threshold; test that separately.

1. Measure packet bursts, effective receive buffers, socket drops, recovered
   packets, FEC time and receive-thread CPU. Nominal 1 Gbps Ethernet or 100%
   Wi-Fi signal does not establish sustainable application throughput.
2. Verify the active receive branch. `ps5_socket_poll` currently allocates an
   event array and creates/registers/destroys an epoll instance per call, but
   Moonlight's successful `SO_RCVTIMEO` path avoids per-packet polling. Reuse a
   connection-owned waiter or fixed small event buffer only if the relevant
   branch/call frequency makes this material. Test timeout, cancellation,
   descriptor reuse and reconnect behavior.
3. Profile packet allocation/release. Consider bounded buffer recycling or
   bounded burst draining only when allocator/syscall cost is demonstrated.
   Do not introduce a global allocator or an unbounded receive/decode queue.
4. Test the already-prepared **FEC SIMD** candidate in isolation, including
   deterministic recovery cases and controlled loss/burst conditions. Retain
   CPU/OS feature gates and scalar fallback. Clean wired frame-gap counters
   alone cannot establish either its benefit or irrelevance.

Start with measured 80 and 100 Mbps workloads, then 60/120/150 Mbps as useful
controls. A bitrate cap is not actual traffic: include a repeatable moving,
high-detail scene. Keep Wi-Fi results separate from Ethernet and do not change
the household network or disable encryption as an optimization.

### 4. Borrow pacing policy, not Xbox-specific plumbing

Xbox separates immediate/freshness-oriented presentation from display-locked
cadence with a bounded decoded-frame queue. Qt also bounds presentation queues
and drops stale work. Those policies are useful references; their threads,
queue sizes and DXGI timing workarounds are not PS5 prescriptions.
Sources: [Xbox Pacer](https://github.com/TheElixZammuto/moonlight-xbox/blob/50c02fd0e7232bbae187d46d08f0df5a022a1f45/Streaming/Pacer.cpp),
[Qt pacer](https://github.com/moonlight-stream/moonlight-qt/blob/14c26d8c7de8fd9cbde67dac8b47601faad46c6c/app/streaming/video/ffmpeg-renderers/pacer/pacer.cpp).

Keep current low-latency overlap as the control. Experiment with an optional
display-locked policy using source timestamps and actual refresh only after
stage timing is trustworthy. Bound age and outstanding work; skip stale
presentation, not reference pictures needed by the decoder. "All incoming
frames" must not mean indefinitely accumulating old frames.

90 FPS on a fixed 119.88 Hz output necessarily has uneven repeat cadence unless
a matching output rate or suitable VRR path is established. Do not port Xbox's
120 Hz timing workaround onto the already measured native PS5 clock.

Xbox advertises direct submission and measures decode duration around FFmpeg
packet submission/frame retrieval. That function does not establish the same
completion boundary as our synchronous Videodec2 path. Blindly enabling direct
submit here could occupy the receive thread for roughly seven milliseconds.
Keep the existing receiver/decoder separation unless a genuinely nonblocking,
bounded replacement is proved. Source:
[Xbox decoder](https://github.com/TheElixZammuto/moonlight-xbox/blob/50c02fd0e7232bbae187d46d08f0df5a022a1f45/Streaming/FFmpegDecoder.cpp).

### 5. Audio and input: targeted secondary work

- Reuse the existing Opus SIMD candidate; test stereo and 5.1 independently.
  Also time sample scanning, PCM copying and ring management outside the Opus
  call. The current 3 ms AudioOut call may largely be normal blocking pacing,
  not three milliseconds of CPU work.
- Test the existing 30 ms audio catch-up candidate only against reproducible
  backlog. Continue decoding/PLC before discarding stale PCM, preserve channel
  mapping, and assess A/V sync and audible recovery. Do not silently add a
  larger audio buffer to disguise stalls.
- Compare current 4 ms input polling with 2 ms, then 1 ms only if justified.
  Measure event freshness, input-to-photon latency and CPU/wakeup cost under
  4K120 load. Preserve controller/mouse switching, USB keyboard and shortcuts.

## Experiments, acceptance and Xbox comparison

Offline first: reuse `make test`, `make test-stream-performance`, `make lint`
and the candidate/receipt machinery. Add only focused checks for changed
ownership, queue limits, socket lifetimes or timing logic. No console is needed
to prepare reproducible builds, deterministic inputs and measurement schemas.

Hardware becomes necessary for actual decoder/AGC/cache visibility, VideoOut
timing, scheduling and HDMI/audio/input validation. Future runs must use WSL,
the shared lock's atomic acquisition/exact-token release protocol, bounded
console windows and teardown/health checks. No Chiaki/Remote Play during
performance qualification; use the TV directly. This plan itself uses no console.

Test sequence:

1. Start with the exact `.060` control: 4K120, HEVC SDR, 80 Mbps, stereo,
   Ethernet. Complete Windows login first, close the previous host app and
   start the game fresh with the intended settings. Verify actual source rate
   and encoded bitrate. Keep a 4K60 control.
2. Compare one change at a time: at least three alternating baseline/candidate
   pairs, each with 30 seconds warm-up and 120 seconds measured steady workload.
   Keep game, scene, host encoder/preset, display mode and background load fixed.
3. Repeat promising candidates at 100 Mbps, then stressful content/bitrates.
   Qualify H.264, HEVC, HDR, stereo/5.1, 1080p/1440p, 60/90 FPS, keyboard/mouse,
   repeated launch/return and a 15–30 minute soak before combining changes.
4. Repeat on firmware 6.02 and 12.70 when available; recruit the 9.60 reporter
   for a separate Wi-Fi/Vibepollo run with exact versions and equivalent settings.

Initial engineering targets, to refine after the matched baseline:

- Track unique completed frames relative to measured source delivery, not
  requested FPS or TV refresh. On a stable approximately 119.88 FPS source,
  aim for under 0.1% client-attributed missed frames and no client-attributed
  stalls over 50 ms during the steady window; report intervals and repeats too.
- Aim to bring decode-ready p99 below 8.342 ms, ideally around 7 ms for margin.
  This is a stretch target, not a measured capability for every codec/scene.
- Promote a candidate only when gains repeat beyond run-to-run noise: for
  example, a material p99/stall reduction, lower measured input latency, or
  lower CPU/wakeup cost without worse pacing. Do not require every optimization
  to increase FPS if the source is already fully delivered.
- Reject visual corruption, lost edges/HDR, increased A/V drift, input loss,
  unbounded memory/queue growth, unsafe teardown or improved averages hiding
  worse tails. Combine only individually qualified changes and retest.

A credible Xbox comparison needs the same PC, game/trace, actual stream format,
encoder layout, bitrate, network and equivalent TV/HDMI processing. Record the
Xbox model, client commit/version and pacing mode. Report both matched-format
results and each client's best normal configuration rather than silently
changing one side's quality. Measure p50/p95/p99 latency, frame pacing, stale
frames and stability; do not compare the two overlay "decode ms" values alone.
The Xbox source reference for this review is `50c02fd0e7232bbae187d46d08f0df5a022a1f45`.

Use the same external input-to-photon method for both devices. A 240 FPS camera
has 4.17 ms temporal steps and cannot substantiate a one-millisecond advantage;
use faster capture or appropriate measurement hardware for that claim. No
matched Xbox run is available in the current evidence. Publish a win only for
the tested configurations, with uncertainty and failed cases included.

## Additional user report: apparent 80 Mbps threshold

A further user reports that lowering bitrate to approximately 80 Mbps restored
smooth 1440p120 playback. Their message says "80Gbps"; 80 Mbps is the likely
intended value, pending confirmation. Client version, codec, HDR, connection
type, actual bitrate and measurements are unknown. This is a useful symptom,
not evidence of a universal PS5 bandwidth or decoder limit.

Bitrate changes the compressed data budget, not the requested resolution or
frame rate. At an actual encoded 80 Mbps and 120 FPS the average budget is about
83 kB per frame; at 100 Mbps it is 104 kB, and at 150 Mbps it is 156 kB. Individual
frames can be much larger. Additional traffic includes repair packets, headers
and audio; measure actual wire traffic because negotiated bitrate budgeting
can account for some overhead already.

After the matched control is established, run a focused 80/100/150 Mbps sweep
at 1440p120 with a fixed scene, codec and fresh host session. If the failure
boundary lies between 80 and 100, add a 90 Mbps case. Repeat the first failing
bitrate at 1440p60 and 4K120, changing only one setting at a time. Record actual
encoder FPS/bitrate, network/FEC activity, queue depth, decode-ready tails and
presentation completion. Current whole-session telemetry does not yet provide
all these network measurements; do not infer their absence from zero gaps.

Verify negotiated link speeds across the host, console and intervening wired
links, not just advertised adapter capability. A 100 Mbps segment is one
possible explanation for an approximately 80 Mbps usable video budget with
overhead; it is not established for either reporter. Also distinguish Wi-Fi
airtime/retries, packet-processing/FEC pressure, encoder bursts and decoder or
presentation saturation. At 120 FPS each frame has only about 8.33 ms of budget.

The same reporter requests an option to disable UI sounds. Track this as a
separate usability enhancement; it is not a proposed streaming-performance fix.

## What not to build yet

No new graphics backend, speculative AV1 path, deep decoder queue, extra render
worker, custom global allocator or continuous remote telemetry service. No
platform privilege changes. Do not spend this round removing a 5-microsecond
compressed-frame copy or lowering image quality to win a benchmark.

The next implementation slice should be **baseline provenance + missing stage
timing**, followed by the existing polling A/B and measured AGC hot-path cleanup.
Then use that evidence to choose the decoder/encoder or network experiment with
the largest demonstrated cost. Stop pursuing a hypothesis when its controlled
test shows no worthwhile benefit.

## Retained evidence

### Vibepollo follow-up: negotiation and pacing source audit

**Hardware outcome: reject the one-slice candidate.** At HEVC SDR 4K120,
80 Mbps, the user reported unusable slowness. Saved telemetry confirmed one
requested slice. Whole-session mean decode rose from 6.635 ms in the prior
four-slice run to 13.000 ms; queue mean rose from 9 us to 55.887 ms, pending
high water reached 15, and 2456/3680 frames (66.7%) were stale-discarded.
Host processing means were similar (13.742 vs 13.588 ms), not evidence of a
host improvement. Sessions differ in duration/content, but the regression is
large and consistent with the user's observation. Fewer slices are not a valid
optimization for this PS5 configuration. Four remains the default; do not copy
desktop hardware-decoder assumptions onto the PS5 decoder. The prior four-slice
executable was restored and verified via WSL FTP without launching it.

Read-only audit of Vibepollo `1.18.4-stable.4` and Moonlight Qt commit
`032529d782242e3833e0b3b147dbbf96e878e3ca`, after the combined client candidate
still delivered approximately 104 FPS. No host settings or console state changed.

The next isolated client experiment should compare **one versus four encoder
slices per frame**. ProsperoLight defaults to four in `src/moonlight_stream.cpp`.
Build the isolated candidate with `make app VIDEO_SLICES_PER_FRAME=1 FEC_SIMD=1
OPUS_SIMD=1 PERFORMANCE_DETAIL=1 FLIP_POLL_US=200` (one shell command).
The report records `requested_slices_per_frame`; this is the requested count,
not independent confirmation of the encoded bitstream's slice count. The build
provenance records the override. The default remains four pending comparison.
Moonlight Qt's FFmpeg decoder adds up to four slices for software decoding,
not unconditionally for hardware decoding; backend capabilities can override
the default. Moonlight-common-c requests one when no slice count is specified.
Vibepollo passes the requested count to NVENC for both H.264 and HEVC.
This is a concrete negotiation difference, not proof that one slice is faster
on PS5. Fewer slices can change compression and decoder parallelism in opposing
directions. Keep codec, bitrate, host configuration and all other client flags
identical. Do not promote a new default without hardware evidence.

Sources: [Qt decoder capability selection](https://github.com/moonlight-stream/moonlight-qt/blob/032529d782242e3833e0b3b147dbbf96e878e3ca/app/streaming/video/ffmpeg.cpp#L88),
[Vibepollo NVENC slice configuration](https://github.com/Nonary/vibepollo/blob/1.18.4-stable.4/src/nvenc/nvenc_base.cpp#L483).

Other findings and limits:

- Resolution/FPS request is 3840x2160x120, packet size 1392, refresh hint
  11988. The host accepts the refresh hint because it rounds to the requested
  120 FPS. No accidental 60-FPS request was found. The 120/119.88 difference
  alone is only 0.1%, not the observed approximately 13% arrival-rate deficit;
  its interaction with capture scheduling remains a separate hypothesis.
- The NVENC path submits a picture then waits for its completion before
  returning the bitstream. An async encoder flag therefore does not prove
  multiple pictures are in flight through this call. Host processing time
  includes more than encoding and cannot be used as pure NVENC duration.
- Capture pacing can restart after a late wake or a zero-timeout snapshot miss.
  The host has debug counters distinguishing these cases and an optional
  phase-preserving policy. Existing info-level logs do not expose those counts,
  so the recordings cannot establish which mechanism dominates.
- Outgoing frame timestamps are adjusted near an encoder timing grid. PTS
  intervals are not a raw capture-event trace. Agreement between PTS and arrival
  rate supports the upstream-cadence hypothesis but does not isolate capture,
  encoder cost or receive scheduling. Zero frame-number gaps also cannot prove
  that capture never skipped a display update.
- The host's 480/1 activity-admission report is a different quantity from the
  negotiated stream rate. Do not change the PS5 output or request 480 FPS to
  compensate. No host configuration changes are authorized for this experiment.

Sources: [RTSP refresh validation](https://github.com/Nonary/vibepollo/blob/1.18.4-stable.4/src/rtsp.cpp#L1665),
[NVENC completion wait](https://github.com/Nonary/vibepollo/blob/1.18.4-stable.4/src/nvenc/nvenc_base.cpp#L986),
[capture pacing](https://github.com/Nonary/vibepollo/blob/1.18.4-stable.4/src/platform/windows/display_base.cpp#L365),
[outgoing timestamp adjustment](https://github.com/Nonary/vibepollo/blob/1.18.4-stable.4/src/video.cpp#L5207).

### Client-only Vibepollo candidate (development 01.000.061)

Build with `make app FEC_SIMD=1 OPUS_SIMD=1 PERFORMANCE_DETAIL=1 FLIP_POLL_US=200`.
This reuses the CPU-feature-checked packet recovery and Opus paths, and reduces
the existing HFR completion polling sleep from 500 to 200 microseconds. It may
increase polling CPU usage. Host settings, bitrate, image quality, presentation
overlap and audio-backlog policy are unchanged. This is a combined candidate,
not an experiment that attributes any gain to one individual switch.

Offline recovery of an 8+4-shard, 1392-byte packet group produced identical
bytes in scalar and AVX2 modes. Three host-run medians were 15.171–15.611 us
scalar versus 1.001–1.013 us AVX2. This measures recovery only on the development
PC, not PS5 decode time or streaming FPS. Stereo and 5.1 Opus comparisons passed
the existing PCM error bounds. Hardware performance and compatibility remain
to be tested; no public defaults have been promoted.

The preceding HEVC gameplay trace delivered roughly 106.5 FPS in its steady
window, with little client queueing or prior-flip waiting. These client changes
do not establish a fix for that arrival/source cadence shortfall. Keep Vibepollo
unchanged for the comparison. Test the same fresh game at HEVC SDR 4K120,
80 Mbps over Ethernet: 30 seconds warm-up, then two minutes of gameplay.
Return to the main UI and close the app before collecting the JSON and CSV so
the UFS2 indirect-block metadata is flushed. Do not start another stream before
collection, since it replaces the report.

### Vibepollo cadence probe (development 01.000.061)

With `PERFORMANCE_DETAIL=1`, the client records at most 32,768 frame records
in a fixed memory buffer (approximately 273 seconds at 120 FPS). It saves
`/download0/moonlight/performance-frames.csv` after stream teardown, alongside
the summary JSON. No file writes or allocations are added to streaming callbacks.
Playback policy and host settings are unchanged. The CSV header records omitted
samples when full. Each new stream replaces the prior report; collect before
another stream. Both files can be extracted read-only from the title's UFS2
`download0.dat` if FTP cannot access the live sandbox.

Records include frame number, compressed bytes, queued frames, receive/enqueue
timestamps, callback entry, source PTS, decode/ready duration, prior-flip wait,
submission and observed completion. Receive/enqueue/callback-network timestamps
use the upstream local clock; callback/submission/completion use CLOCK_MONOTONIC.
Source PTS belongs to a third timeline: compare successive PTS intervals, never
subtract PTS directly from local timestamps. Zero receive/enqueue timestamps
are missing data. Outcome 0 is incomplete, 1 submitted, 2 stale-discarded,
3 deliberately presentation-decimated for contention diagnosis.
Completion 0 means no recorded completion; completion observations are NOT
hardware scanout timestamps. Frame association follows the existing submission
FIFO, including delayed decoder output. `ready_us` includes copy/decode/flush
and source-slot waiting; `decode_us` measures only the decode call.

For the next run: restart the uploaded client, use 4K120 HEVC SDR / 80 Mbps,
launch the game fresh, warm up for 30 seconds and play the same area for two
minutes. Return using Select+L1 and collect both files before changing codec.
Analyze only the steady window and correlate arrival/PTS intervals, frame sizes,
decode tails, backlog and previous-flip waits; exclude startup/teardown. Repeat
with H.264 afterward. The probe itself has overhead and requires comparison
against the preceding detail-enabled build before drawing performance claims.

Local September 22 reports show no HEVC stale drops but H.264 accumulated up to
12 queued frames and dropped 758/8569 presentations. These are whole-session
observations, not proof of a Vibepollo bug or a universal bitrate ceiling.

- [Round 2 release checkpoint](PERFORMANCE_ROUND_2.md): tracked summary and limits.
- Local paired receipts: `results/xbox-followup-20260909T005452Z-PWnghM/`,
  `user-00-baseline-20260909-233223/SUMMARY.md` and
  `user-04-presentation-overlap-20260909-234747/SUMMARY.md` plus their JSON files.
- Local publication receipt: `results/release-01.000.060/VERIFIED.md`.
  Tested local executable: `8f8f6986654d2d18a31cb54f3e832c1175be5f7847e70e381574bbc19ceb6e80`;
  published CI executable: `9943818231ca0569e3c755ba4400a479421df2cf9f5e30398efd77b686bd1a2c`.
- Local `results/` receipts are not GitHub documentation dependencies; the
  essential comparison, hashes and caveats are retained above and in Round 2.
