# Performance round 4 — decode and presentation threads, measured bitrate limits

Status: **experimental beta `01.000.070`.** The architecture below ran on a
console at 4K 120 FPS HEVC SDR in four sessions. The released defaults (Classic
pipeline, three decoder cores) use the `01.000.062` decoder configuration; the
Adaptive pipeline is opt-in and its overlapped path is still unmeasured.

## Why

Round 3 receipts showed that HEVC 4K120 is bound by decoding, not by the
network or by presentation. Once decoding fell behind in `01.000.062`:

1. frames waited in moonlight-common-c's 15-frame queue;
2. the queue overflowed, discarding everything and requesting a keyframe;
3. the overlay counted the discarded frames as network drops;
4. the stale-frame rule presented one frame every 100 ms.

This round removes steps 3 and 4 and measures where step 1 begins. It does not
make the decoder faster.

## What changed

| Area | Change |
| --- | --- |
| Frame delivery | moonlight-common-c runs as a pull renderer. A decode worker takes frames from its queue; a presentation worker owns AGC. Decoding no longer waits for a flip. |
| Presentation | The newest decoded picture replaces any picture not yet taken, so every flip shows the latest frame. The time-based stale rule is gone. |
| Thread placement | Stream threads stay off the decoder's CPUs. The video receive thread gets a CPU of its own, the decode worker the highest remaining CPU, the presentation worker the next one. Each placement is read back and counted. |
| Flip wait | Flip events wake the waiter; flip status still decides completion. No vblank waits. An event wait that does not find the flip complete is always followed by one short sleep, so the wait can neither spin nor time out early. |
| V-Sync | New setting. Off requests an immediate (tearing) flip for stream frames; a rejected or unreported immediate flip restores V-Sync for the process. |
| Decoder pipeline | New setting. **Classic** (default) decodes one frame at a time at Videodec2 depth one. **Adaptive** (experimental) creates Videodec2 at depth 3, overlaps pictures while frames are queued and flushes when the queue is empty. |
| Decoder CPUs | New setting: 3 (default), 4 or 5 whole physical cores (masks `0x3f`, `0xff`, `0x3ff`). `0x3f` was three cores, not six: SMT siblings are adjacent. |
| Slices | 8 slices per frame at every resolution. The observed count is sampled every 60 frames. |
| Overlay | Network, decoded and displayed frame rates are separate. Frame gaps are split into network loss and decoder backlog. Decode time is the last full second (mean, p95, load). |
| Receipts | `performance-last.json` is schema 3; `performance-frames.csv` is schema 2 and written in batches. The summary and five-second windows are also sent to klog. |
| Errors | A stream that was running and lost its host reports "the connection to Sunshine was lost"; a connection that never started names the stage and the code. |
| Dependencies | moonlight-common-c is updated to `f900dd4`. The FEC CPU feature probe is cached. |
| Launcher | Bitrate presets gain 50, 60 and 70 Mbps. The Settings page holds ten rows. |

The configuration file is version 6. Versions 2–5 migrate with V-Sync on,
Classic and 3 cores.

## Console evidence

One PS5, one NVENC host, HEVC SDR, 3840x2160 at 120 FPS, eight slices observed
in every session.

| Session | Frames | Payload | Result |
| --- | --- | --- | --- |
| Desktop, twice | 123 and 421 | idle | Depth 3 with mask `0x3ff` accepted on the first attempt; every picture came from a mid-stream flush; no decoder error. |
| Game, 80 Mbps setting | 6,805 | 24 Mbps, 92 FPS sent | Never behind: no queueing, no gaps, every frame displayed. |
| Game, 300 Mbps setting | 1,541 | about 160 Mbps, 102 FPS sent | Behind 90% of the time: 13 queue overflows in 17 s, 226 frames discarded, up to 0.15 s of queue delay. |

### Decode time per frame

| Condition | Fit | Frames |
| --- | --- | --- |
| `01.000.062`, depth 1, mask `0x3f`, frames up to 62 KB | 3.98 ms + 44.3 µs/KB | 11,994 |
| Adaptive keeping up, mask `0x3ff`, frames up to 66 KB | 3.73 ms + 45.3 µs/KB | 6,804 |
| Depth 1, mask `0x3ff`, frames of 120–238 KB | 5.48 ms + 28.6 µs/KB | 1,380 |
| Four slices, depth 1, `0x3f` (round 3) | 4.4–4.6 ms + 64 µs/KB | 32,414 |
| One slice, depth 1, `0x3f` (round 3) | 6.0 ms + 256 µs/KB | 995 |

- The per-kilobyte cost follows the slice count: 256, 64 and about 45 µs/KB for
  1, 4 and 8 slices.
- The wider worker mask did not make decoding faster: the first two rows are
  the same within noise. This is why three cores stay the default.
- Large frames cost less per kilobyte than the small-frame fit predicts.

### Limits that follow

A frame rate is sustainable while the average frame decodes within one frame
interval:

| Frame rate | Largest average frame | Bitrate | Basis |
| --- | --- | --- | --- |
| 120 FPS | 102 KB | 100 Mbps | Both fits agree. |
| 90 FPS | 197 KB | 145 Mbps | Measured directly: 90.1 frames/s at 197 KB under overload. |
| 60 FPS | 391 KB | about 190 Mbps | Extrapolated; the largest measured frame is 238 KB (about 115 Mbps at 60 FPS). |

`tools/plot-bitrate-limits.py` holds this model and draws the README chart.

### The Adaptive pipeline

- At depth 3, `sceVideodec2Decode` returns in about 0.1 ms without a picture;
  the flush that follows blocks until the picture is ready. While keeping up
  this costs the same as depth one.
- **Defect found and fixed.** When a frame arrived during a two-picture drain,
  the drain stopped after the first flush. The next decode then failed within
  10 µs: Videodec2 refuses a decode between two flushes of one drain. Three
  such errors in a row triggered the planned fallback, which rebuilt the
  decoder at depth one on the same memory and resumed on a keyframe; the rest
  of the 300 Mbps session ran that way. A drain that has started now always
  runs until the pipeline is empty.
- **No throughput gain shown.** In the three overlapped pairs that ran, the
  first picture took 16.5–18.5 ms instead of about 11 ms alone. Overlapped
  decoding of eight-slice frames is therefore unproven, and Classic is the
  default. It may still help hosts that send one slice per frame, where the
  per-kilobyte cost is serial.

## Build variables

| Variable | Default | Meaning |
| --- | --- | --- |
| `VIDEO_SLICES_PER_FRAME` | 8 | Slices requested from the host. |
| `DECODER_PIPELINE_DEPTH` | 3 | Depth of the Adaptive pipeline, 1–3. |
| `INPUT_POLL_US` | 2000 | Input poll target. |
| `GPU_TIMESTAMPS` | 0 | Opt-in GPU render timing (`gpu_render` in the summary). |
| `CATCHUP_QUEUE_FRAMES` | 0 | Opt-in: a queue of this many frames for 250 ms requests a keyframe. |
| `REFERENCE_FRAME_INVALIDATION` | 0 | Opt-in: advertise RFI and use a six-frame DPB. |

`DECODER_CPU_AFFINITY`, `PRESENT_EVERY_N` and `FRAME_PACING` are removed; the
first is now the cores setting, the others no longer apply.

Release build:

```sh
make ffpfsc FEC_SIMD=1 OPUS_SIMD=1 PERFORMANCE_DETAIL=1 FLIP_POLL_US=200 INPUT_POLL_US=2000 VIDEO_SLICES_PER_FRAME=8
```

## Fallbacks

| If this happens | Then |
| --- | --- |
| Videodec2 refuses the depth or the worker mask at creation | Creation retries with the classic mask, then with lower depths, down to depth one with `0x3f`. `decoder_create_attempts` records the walk. |
| A mid-stream flush fails or returns no picture | The decoder is rebuilt at depth one on the same memory and resumes on a keyframe (`drain_faults`, `decoder_recreations`). |
| Three decode errors at depth above one without 600 clean pictures between them | Same rebuild at depth one. This path ran on the console. |
| The rebuild fails | The stream ends with "The video decoder could not be restarted". |
| Flip events cannot be registered | The flip wait polls, as before (`flip_events_active` is 0). |
| An immediate flip is rejected or never reported | V-Sync is used for the rest of the process (`vsync_fallbacks`). |
| A thread cannot be placed | It keeps its inherited mask (`placement_failed`, `*_placement_result`). |
| Presentation fails three times in a row | The stream ends with "GPU presentation stopped responding". |

`decode_errors` and `last_decode_error` in the summary record every refused
frame and the last error code.

## Not yet verified on a console

- The Adaptive pipeline after the drain fix, and overlapped decoding at all.
- The three-core thread layout as the default (the sessions above used five).
- 1440p, 1080p, 60 and 90 FPS, HDR, H.264, and V-Sync Off in this build.
- That the picture stays correct after a mid-stream HEVC flush. No decoder
  error occurred in 7,349 flushed frames, but corruption without an error would
  not trigger a fallback.

## What to collect from a test

- The overlay lines "Decoder", "Decode (last second)", "Frame rate" and both
  "Frames dropped" lines.
- `/download0/moonlight/performance-last.json` and `performance-frames.csv`, or
  the `[ProsperoLight perf]` klog records (see
  [Troubleshooting](TROUBLESHOOTING.md#collecting-performance-metrics-through-klog)).
- For Adaptive: the same game and bitrate with Classic and with Adaptive.
