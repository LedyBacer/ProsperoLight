# Native heavy-stream investigation

Base: main `40f4a3d` (PR66). Correctness and throughput are separate goals.
No faster native execution is established by a successful build, a mock, or a
short Decode submission call. Current production defaults remain unchanged.

## Evidence and two hypotheses

The independent PR65 archive is available locally. All its measurement receipts
are INCONCLUSIVE. The following are observed diagnostics, not new qualified
performance results; host log rates are not precisely phase-aligned to PS5 totals.

| Workload | Host sender / presentation | Native call, whole-session mean | Queue, whole-session mean | Local pressure / structure |
| --- | --- | --- | --- | --- |
| QSV H.264 4K60 | ~51–52 / ~41.5–41.7 FPS | 23.13–23.18 ms; p99 upper ~32 ms | 138–143 ms | 18–21 overflows; 383–401 frame-ID gaps; one observed slice; depth 1 |
| QSV HEVC 4K60 | ~42 / ~34–39 FPS | 25.32–28.52 ms | 158–168 ms | 7–19 overflows; one slice; depth 1; WPP/tile facts unavailable in this archive |
| QSV H.264 1440p120 | incomplete runs | 10.62–10.92 ms | 2.8–3.5 ms | exceeds 8.33 ms budget; stale deadline fixed in PR66; not a 120 FPS capacity proof |
| VAAPI H.264 1440p60 | host-limited; ~51 FPS in phase summary | 6.00–6.19 ms | 23–26 us | zero overflows and frame gaps; one slice; separate logs report 9 references / capacity 10 |

`receive_us` rows cover dequeued AUs, not everything transmitted. Do not repeat
the archive summary's stronger “all low FPS is host-limited” conclusion. PR66
adds separately scoped upstream counters; existing archive data cannot acquire
them retrospectively. Frame IDs alone do not identify a loss cause.

Only two primary hypotheses:

1. **Bounded depth two may improve completed throughput for heavy single-slice
   H.264.** Compare the same synthetic workload at depth one and two. Reject a
   lower submission time without more completed frames or acceptable ready/flip
   latency. Do not enable PR65 automatic depth transitions from this experiment.
2. **Earlier dependency-safe resync bounds overload episodes.** When service
   cannot sustain incoming AUs, do not repeatedly decode an old queue until its
   15-AU overflow. Bound resync attempts; persistent pressure ends explicitly.
   This is latency containment, never an increase in decoder capacity.

## Prior research and ruled-out shortcuts

Reviewed the project performance rounds 3/4, guide, encoder-agnostic design and
VideoDec2 lifecycle. The old research URL is unavailable; the accessible source
is [drakmor's public research](https://github.com/drakmor/ps5-hardware-video-decoding-research),
reviewed at `7030ee4a10e0973e90895c62e607c79841611140`.

- H.264 1080p: 1 / 4 / 8 slices measured 4.761 / 2.268 / 2.069 ms. This
  supports partition sensitivity, not the same speedup at 4K.
- Controlled HEVC 4K, identical bytes: depth 1 ~47.94 FPS versus depth 3
  ~128.39 FPS; ready latency 20.719 versus 22.993 ms. WPP at depth 3 improved
  that separate batch by 36.2%. This is a reason to characterize HEVC PPS,
  not a H.264 optimization or a new guarantee for QSV.
- Light live HEVC: depth 3 held throughput around 60 FPS but increased ready
  latency 5.472 -> 33.710 ms and callback-to-flip 16.698 -> 50.059 ms.
- Prior product eight-slice overlap did not prove a throughput gain. Wider
  decoder CPU masks did not establish a general benefit.
- The native API exposes pipeline depth, bounded pools, DPB, affinity/priority
  and progressive optimization. No public knob splits a single CABAC slice into
  independent slices. CPU affinity does not prove where all native work runs.
- Gather averaged ~5 us in prior live controls. A decoded-pixel copy, AGC/VideoOut
  rewrite, larger queues, higher thread priorities, or reduced DPB is unjustified.
- Keep depth/pools <= existing bounds (3 native, 5 input slots, protected output
  slots); preserve accepted DPB and the full drain barrier. Never interleave a
  Decode into an unfinished multi-output Flush drain.

## Pipeline and overload candidate

RTP -> receive/FEC/assembly -> bounded 15-AU Limelight queue -> pull decode owner
-> gather into fixed input slots -> Decode/complete drain -> correlated output
slots -> newest-ready mailbox -> AGC/VideoOut. No new decoded-frame queue.
VideoDec2-to-AGC stays zero-copy; retirement/reset owns all old allocations.

`NATIVE_MAX_QUEUE_MS=80` is an explicit **developer candidate**. An active Test
Lab profile may set `capture.native_queue_limit_ms` to 0 or 20–250 for a
session-only control/candidate comparison on the same binary; it never persists. Production is
`0` until native evidence supports promotion. With the candidate enabled:

- Inspect a valid Limelight enqueue timestamp before native submission. Start
  only on a ready owner that has produced output; skip startup/mandatory recovery.
- At >=80 ms age with >=2 remaining queued AUs, or >=12 queued AUs before the
  15-AU hard capacity, return DR_NEED_IDR once for that episode.
- Existing Limelight completion flushes queued dependents and requests IDR.
  Mark native reset and mandatory fresh matching SPS/PPS/IDR before any next
  submit. Missing IDR remains bounded by the existing 10s mandatory deadline.
- Two resync attempts are allowed. Repeated pressure ends with lifecycle
  Throughput / -35 and Test Lab NATIVE_THROUGHPUT_LIMIT, rather than an endless
  IDR loop. Rearm only after 30s of continuously observed healthy delivery,
  with low age, <=1 pending AU and no clock/source/invalid-timestamp gaps.
- Do not change resolution, bitrate, FPS, codec, saved settings, decoder depth,
  CPU mask or AGC behavior. Existing optional frame-count catch-up is not combined
  with this candidate. PyroWave never enters the native decode owner.

The threshold bounds decisions at decode-owner observations, **not time spent
inside a blocking firmware call**. A stalled native call or receive burst can
still fill the transport queue before the owner wakes. No zero-overflow promise,
network-loss attribution, or physical input-to-photon claim is made. Queue
pressure proves a local backlog, not by itself the native hardware root cause.

Summary `native_overload` records gate/limit, cumulative resyncs/stops, budget
and observed peak/action age. No per-packet allocation, I/O or extra hot-path
clock reads; timestamps already captured by the owner are reused.

## Small reproducible controls

`tools/native-workload.py` generates only `testsrc2`; it cannot capture a desktop.
It creates a new output directory, refuses overwrite and records FFmpeg version,
exact commands/SHA256, actual bitrate, AU offsets/bytes/PTS/IDR/slices, profile,
level, references/DPB/POC, CABAC or WPP/tiles where parsed. Unknown fields are
null. FFmpeg trace_headers supplies header facts and full software decode checks
frame count/types. No PS5 timing is inferred from that software decode.

```sh
python3 tools/native-workload.py --output build/native-h264 --codec h264 --frames 60
python3 tools/native-workload.py --output build/native-hevc --codec hevc --frames 30
```

Local FFmpeg 8.1.1 controls at 2560x1440, 60 FPS, QP22, ref request 4:

| Pair | Actual structure | Payload |
| --- | --- | --- |
| H.264 / 60 AUs | High, level 5.1, CABAC, 4 refs / DPB4, POC type2, no B; verified 1 versus 4 slices | 38.946 / 39.062 Mbps |
| HEVC / 30 AUs | Main, level 5.0, buffering5, reorder0, no tiles; verified WPP0 versus WPP1 | 48.575 / 48.629 Mbps |

These are matched source/QP controls, not identical compressed bytes across
partitions. Reuse one exact SHA when comparing decoder depth. Manifest files are
local evidence; regenerated bytes require their own SHA, even with the same recipe.

Existing Test Lab has live synthetic stimulus and native depth controls, but no
host-independent AU replay entry point. Research repository examples are memory/
timing models, not a runnable product replay tool. This change does not smuggle
pre-encoded data through a new streaming protocol or claim live encode bytes are
identical between runs. Native replay remains a later small isolated developer
extension if live controlled input cannot answer the selected hypothesis.

## Host cooperation

Current [Sunshine video.cpp](https://github.com/LizardByte/Sunshine/blob/3494b23e20b13d27b88fa08b97d07cb6d917f919/src/video.cpp)
sets hardware `ctx->slices` from the request; software uses at least min_threads.
It also has SINGLE_SLICE_ONLY capability gating. That declaration is not proof
of the exact Intel driver's output or of Punktfunk's native VPL implementation.
QSV in this Sunshine revision uses async_depth1, low_delay_brc and low_power.
Its documented qsv_coder can select CAVLC versus CABAC, with a compression/quality
tradeoff; this client never changes it automatically.

The standard client slice request is already sent. There is no established
standard Moonlight command here for forcing HEVC WPP/tiles or driver-specific
partitioning. An actionable host follow-up must log requested/accepted/observed
partitions, check actual backend/driver capability, and configure supported
H.264 slices or HEVC WPP without silently changing quality. The independent host
was Punktfunk 0.45 canary; a Sunshine FFmpeg option is not automatically its API.

## Validation and native experiment record

Focused tests and SDK/CI results, exact build IDs, native runs and measured
before/after values will be recorded here before a performance claim or promotion.
The healthy fast path remains production-gated from this candidate. The external
Intel contributor is not contacted. Target native budget: 2–4 short runs.
