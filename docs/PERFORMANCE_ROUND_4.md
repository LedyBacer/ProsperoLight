# Performance round 4 — decode pipeline and presentation rework

Status: **development `01.000.064`.** Host-tested, with a first console check on
two short desktop sessions (see [First console evidence](#first-console-evidence));
performance under gameplay load is not measured yet. Every hardware-dependent
behaviour has a fallback to the `01.000.062` path, listed under
[Fallbacks](#fallbacks).

## Why

Round 3 receipts show that HEVC 4K120 is bound by decoding, not by the network
or by presentation. At depth one, each access unit cost about 3.5–3.9 ms plus
45–52 µs per kilobyte with eight slices, against an 8.34 ms budget at 119.88 Hz.
Once decoding fell behind:

1. frames waited in moonlight-common-c's 15-frame queue;
2. the queue overflowed, discarding everything and requesting a keyframe;
3. the overlay counted the discarded frames as network drops;
4. the stale-frame rule presented one frame every 100 ms.

## What changed

| Area | Change |
| --- | --- |
| Frame delivery | moonlight-common-c runs as a pull renderer. A decode worker takes frames from its queue; a presentation worker owns AGC. Decoding no longer waits for a flip. |
| Presentation | The newest decoded picture replaces any picture not yet taken, so every flip shows the latest frame. The time-based stale rule is gone. |
| Decoder depth | **Adaptive** (default) creates Videodec2 at depth 3. While frames are queued, pictures overlap in the pipeline; when nothing is queued, the pending pictures are flushed immediately, so latency matches depth one. **Classic** is depth one, as in `01.000.062`. |
| Decoder CPUs | Videodec2 workers use 3, 4 or 5 whole physical cores (masks `0x3f`, `0xff`, `0x3ff`; default 5). `0x3f` was three cores, not six: SMT siblings are adjacent. |
| Thread placement | Stream threads stay off the decoder's CPUs. The video receive thread gets a CPU of its own, the decode worker the highest remaining CPU, the presentation worker the next one. Each placement is read back and counted. |
| Slices | 8 slices per frame above 1080p, 4 at 1080p. The observed count is sampled every 60 frames. |
| Flip wait | Flip events wake the waiter; flip status still decides completion. No vblank waits. A stale or failing event is always followed by one short sleep, so the wait can neither spin nor time out early. |
| V-Sync | New setting. Off requests an immediate (tearing) flip for stream frames; if the first such flip is rejected, V-Sync is restored for the process. |
| Overlay | Network, decoded and displayed frame rates are separate. Frame gaps are split into network loss and decoder backlog. Decode time is the last full second (mean, p95, load). |
| Receipts | `performance-last.json` is schema 3; `performance-frames.csv` is schema 2 and written in batches. The summary and five-second windows are also sent to klog. |
| Dependencies | moonlight-common-c is updated to `f900dd4`. The FEC CPU feature probe is cached. |
| Launcher | Bitrate presets gain 50, 60 and 70 Mbps. The Settings page holds ten rows. |

## Settings

| Setting | Values | Notes |
| --- | --- | --- |
| V-Sync | On / Off | Off tears. |
| Decoder pipeline | Adaptive / Classic | Classic is the escape hatch if Adaptive shows artifacts. |
| Decoder CPU cores | 5 / 4 / 3 | 3 reproduces the `01.000.062` reservation. |

The configuration file is version 6. Versions 2–5 migrate with V-Sync on,
Adaptive and 5 cores.

## Build variables

| Variable | Default | Meaning |
| --- | --- | --- |
| `VIDEO_SLICES_PER_FRAME` | 8 | Slices above 1080p (1080p uses at most 4). |
| `DECODER_PIPELINE_DEPTH` | 3 | Adaptive depth, 1–3. |
| `INPUT_POLL_US` | 2000 | Input poll target. |
| `GPU_TIMESTAMPS` | 0 | Opt-in GPU render timing (`gpu_render` in the summary). |
| `CATCHUP_QUEUE_FRAMES` | 0 | Opt-in: a queue of this many frames for 250 ms requests a keyframe. |
| `REFERENCE_FRAME_INVALIDATION` | 0 | Opt-in: advertise RFI and use a six-frame DPB. |

`DECODER_CPU_AFFINITY`, `PRESENT_EVERY_N` and `FRAME_PACING` are removed; the
first is now the cores setting, the others no longer apply.

Release-equivalent build:

```sh
make app FEC_SIMD=1 OPUS_SIMD=1 PERFORMANCE_DETAIL=1 FLIP_POLL_US=200
```

## Fallbacks

| If this happens | Then |
| --- | --- |
| Videodec2 refuses the depth or the worker mask at creation | Creation retries with the classic mask, then with lower depths, down to depth one with `0x3f`. `decoder_create_attempts` records the walk. |
| A mid-stream flush fails or returns no picture | The decoder is rebuilt at depth one on the same memory and resumes on a keyframe (`drain_faults`, `decoder_recreations`). |
| Three decode errors at depth above one without 600 clean pictures between them | Same rebuild at depth one. |
| The rebuild fails | The stream ends with "The video decoder could not be restarted". |
| Flip events cannot be registered | The flip wait polls, as before (`flip_events_active` is 0). |
| An immediate flip is rejected | V-Sync is used for the rest of the process (`vsync_fallbacks`). |
| A thread cannot be placed | It keeps its inherited mask (`placement_failed`, `*_placement_result`). |
| Presentation fails three times in a row | The stream ends with "GPU presentation stopped responding". |

## First console evidence

Two desktop sessions on development `01.000.063` (HEVC SDR, 3840x2160 at
120 FPS, 123 and 421 frames, few frames per second), read from the saved
schema 3 summaries:

- Videodec2 accepted depth 3 with the five-core mask `0x3ff` on the first
  attempt.
- At depth 3, `sceVideodec2Decode` returns without a picture in about 0.1 ms.
  Every picture was then obtained by the mid-stream flush, which took 5.1 and
  6.3 ms on average. Across 544 frames there was no decoder error, no failed
  flush and no decoder rebuild.
- Every thread placement was applied and read back, including the receive
  thread on its own CPU.
- Flip events were delivered once per presented frame, with no flip timeout.
- Eight slices were observed.

These sessions never fell behind, so the overlapped path has not run yet, and
the frames were small: the timings say nothing about gameplay.

One defect surfaced: only the first stream of a process connected.
moonlight-common-c writes a placeholder into the empty submit slot of the
caller's callback struct, then refuses a pull renderer that has a submit
callback. The slot is now cleared before every connection.

## Not yet verified on hardware

- Overlapped decoding while frames are queued, and any change in decode time,
  frame rate or latency under gameplay load.
- That the picture stays correct after a mid-stream HEVC flush. No decoder
  error occurred, but corruption without an error would not trigger a
  fallback: switch to Classic if artifacts appear.
- A second and later stream in one process (fixed after the first test).
- Classic, the 4- and 3-core masks, and the immediate flip mode.

## What to collect from a test

- The overlay lines "Decoder", "Decode (last second)", "Frame rate" and both
  "Frames dropped" lines.
- `/download0/moonlight/performance-last.json` and `performance-frames.csv`, or
  the `[ProsperoLight perf]` klog records (see
  [Troubleshooting](TROUBLESHOOTING.md#collecting-performance-metrics-through-klog)).
- Compare Adaptive against Classic, and 5 cores against 3, at the same
  resolution, frame rate and bitrate.
