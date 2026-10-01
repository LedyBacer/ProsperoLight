# Changelog

## 01.000.064

### Development build — decode pipeline and presentation rework

**Not released and not yet validated on a console.** Host tests cover the new
logic; see [round 4](docs/PERFORMANCE_ROUND_4.md) for what is unverified and for
the fallback to the `01.000.062` path behind each hardware-dependent change.

- Decoding and presentation run on separate threads. Decoding no longer waits
  for a flip, and each flip shows the newest decoded picture; the rule that
  presented one frame every 100 ms while behind is removed.
- New **Decoder pipeline** setting. Adaptive (default) overlaps frames in
  Videodec2 only while frames are queued and flushes as soon as it catches up.
  Classic decodes one frame at a time, as before.
- New **Decoder CPU cores** setting (5, 4 or 3 physical cores; default 5).
  Stream threads are kept off the decoder's CPUs, and the video receive thread
  gets a CPU of its own.
- New **V-Sync** setting. Off flips immediately and tears.
- Eight slices per frame above 1080p, four at 1080p.
- The flip wait uses flip events and no longer waits for a vblank.
- The overlay separates network, decoded and displayed frame rates, and frames
  lost to the network from frames discarded because decoding fell behind. Decode
  time covers the last second (mean, p95, load).
- `performance-last.json` is schema 3, `performance-frames.csv` is schema 2,
  and both the summary and five-second windows are also written to klog.
- Updates moonlight-common-c, adds 50/60/70 Mbps bitrate presets, and caches
  the FEC CPU feature probe.

Settings and pairings are kept; existing configurations migrate with V-Sync on,
Adaptive and 5 cores. If the picture shows artifacts, select Classic.

Local build: `make app FEC_SIMD=1 OPUS_SIMD=1 PERFORMANCE_DETAIL=1 FLIP_POLL_US=200`.

## 01.000.062

### Experimental performance beta — help test 4K120

This is a **prerelease**, not a replacement for stable **01.000.060**.
Improved decode headroom; remaining 4K120 stuttering is under investigation.

- Requests eight slices per frame for increased decoder parallelism. Local HEVC
  tests confirmed eight slices and lower decode times, including comparable
  compressed-frame sizes. Gameplay windows included Windows/RDP login transitions:
  these are promising observations, not a controlled percentage improvement.
- Shortens input polling to a 2-ms target and accounts for polling work time.
  This does not establish a measured input-to-photon latency reduction.
- Enables CPU-feature-checked FEC SIMD and Opus SIMD, with 200-us flip polling.
- Reduces repeated GPU cache-flush work and avoids heap allocation for common
  socket polls. Preserves bounded presentation overlap and source ownership.
- Includes local performance summaries and bounded per-frame timing records for
  diagnosis. No streamed images, typed text, or automatic remote telemetry.
- Includes PC removal/re-pairing improvements for changing host software.

Decoder depth stays **one**, worker affinity/priority remain unchanged, every
decoded picture remains eligible for presentation, and experimental pacing and
audio-backlog dropping are **off**. No host configuration is modified. Eight
slices may behave differently with other encoders/codecs; report regressions.
Higher bitrate is not higher FPS, and sustained 120 FPS is not guaranteed.

### Please report your results

Use [GitHub issues](https://github.com/blackbearreloaded/ProsperoLight/issues) and include:

- PS5 firmware; Sunshine/Vibepollo version; host GPU and driver.
- Codec/HDR, resolution/FPS, bitrate, stereo/5.1, Ethernet or Wi-Fi.
- Game and whether this beta is smoother, unchanged, or worse than `.060`.
- Complete Windows/RDP login first, reconnect, then warm up for 30 seconds and
  play the same area for two minutes. Return with Select+L1 and close the app.
  Separate login/menu/teardown time from gameplay. Stop early if unusable.
- If available, attach `performance-last.json` and `performance-frames.csv` from
  the app's `/download0/moonlight/` save directory. These are overwritten by the
  next stream; the console may expose them inside the title's UFS2 `download0.dat`.
  **Do not upload the entire save image**: it may contain pairing credentials.

### Installation, update and rollback

Title ID stays **PPSA99002**. Choose `PPSA99002.ffpfsc`, or extract
`PPSA99002.zip` and copy its `PPSA99002/` folder to `/data/homebrew`.
Do not keep both formats installed. Close the app before replacing files, then
restart ShadowMountPlus or the console. `SHA256SUMS` covers both downloads.
Existing pairing/settings are retained. To roll back, replace the beta with
the corresponding `.060` download and restart ShadowMountPlus or the console.

Local reproduction (ordinary `make` retains conservative development defaults):
`make ffpfsc FEC_SIMD=1 OPUS_SIMD=1 PERFORMANCE_DETAIL=1 FLIP_POLL_US=200 INPUT_POLL_US=2000 VIDEO_SLICES_PER_FRAME=8`.

## 01.000.060

### Smoother 4K120 streaming

- Enabled bounded decode/presentation overlap: the next picture can decode while
  the previous native flip completes, with at most one outstanding submission.
  Source surfaces and shared GPU memory remain protected until completion.
- Improved stale-frame handling, short-window FPS metrics, and fractional-refresh
  negotiation (for example, a 120 FPS request on 119.88 Hz output).
- Added a small local post-session performance summary and regression checks for
  presentation ownership, timeout handling, and incomplete report writes. No
  remote telemetry or streamed content is collected by this summary.

### 4K120 before / after

One paired Hollow Knight test on PS5 firmware 6.02, using wired Ethernet,
3840×2160 at a requested 120 FPS, HEVC SDR, 80 Mbps, stereo, and 119.88 Hz output:

| Measurement | Before: overlap off | After: overlap on |
| --- | ---: | ---: |
| Decoded / presented frames | 16,769 / 12,686 | 14,058 / 14,058 |
| Stale presentation skips | 4,083 (24.3%) | 0 (0%) |
| Mean enqueue-to-decoder-callback wait | 5.468 ms | 0.016 ms |
| Sampled pending video queue peak | 5 frames | 0 frames |
| Mean decoder-call time | 6.869 ms | 6.934 ms |
| p99 CPU-observed completion interval | 75.499 ms | 31.499 ms |

The tester reported **lots of stuttering before** and **much smoother playback
after**. Neither run reported network frame gaps or audio errors. The improvement
is in queueing and presentation, not faster hardware decoding.

These are whole-session measurements from one comparison, including startup and
menus, with different session lengths. The baseline was instrumented with overlap
disabled, not the exact public `01.000.055` binary. This is not a guarantee of
locked 120 FPS, lower input-to-photon latency, or a fix for every high-bitrate/Wi-Fi
setup. CPU-observed frame age increased from 12.560 to 16.854 ms; overlap changes
when completion is observed, and the baseline excludes skipped pictures from
that metric. It cannot establish an end-to-end latency change.

See the [Round 2 measurements and methodology](https://github.com/blackbearreloaded/ProsperoLight/blob/01.000.060/docs/PERFORMANCE_ROUND_2.md#release-checkpoint--01000060).
Further performance work is deferred to the next version: FEC SIMD, Opus SIMD,
audio catch-up, and shorter flip polling are **not enabled** in this release.

### Installation / update

Title ID remains `PPSA99002`; existing pairing and preferences are preserved.
Choose `PPSA99002.ffpfsc` or extract `PPSA99002.zip` and copy its `PPSA99002/`
folder to `/data/homebrew`. Do not keep both formats installed for the same
title. Close the app before replacing its files, then restart ShadowMountPlus
or the console. `SHA256SUMS` covers both downloads.

## 01.000.055

- Added a persistent Stereo / 5.1 surround setting.
- Added Moonlight-compatible 5.1 Opus negotiation and PS5 eight-channel AudioOut,
  with the unused side channels silenced and automatic stereo fallback when the
  surround port is unavailable.
- Compacted the Settings rows so all seven controls and the shortcut note remain
  visible above the footer.
- Replaced the temporary demo app cards with a clear loading state while the
  Sunshine application catalog refreshes.
- Unified the mbedTLS structure configuration across the C and C++ stream code,
  preventing identity initialization from overwriting the live PS5 pad state.

## 01.000.050

- Added independently selectable 90 and 120 FPS streaming at 1080p, 1440p,
  and 2160p, with native 4K/119.88 Hz output validated on PS5 hardware.
- Preserved native 4K presentation for high-resolution HFR streams and restored
  the launcher output cleanly when a stream ends.
- Added an application-side startup transition for TVs that resynchronize when
  an HFR-capable title opens.
- Moved Sunshine discovery behind the first rendered launcher frame for faster
  visible startup.
- Documented that HFR setting changes should be applied before launching, or
  followed by stopping and relaunching the active Sunshine application.
- Clarified that wired Ethernet is recommended for high-resolution and HFR
  streaming.
- Added guidance to tune bitrate per configuration because maximum bitrate can
  reduce smoothness at 4K or high frame rates.
