# Measuring frame pacing on PS5

Enable **Diagnostic logs**, select the desired frame pacing mode and stop the
stream normally after 60–90 seconds. Compare the same codec, scene, resolution,
FPS, VSync setting and network conditions. Repeat each mode rather than using
one run as proof of improvement.

After stopping, download these files through FTP from
`/data/prosperolight/logs` with filesystem elevation, or from the mounted
app's `download0` directory in sandbox fallback mode:

- H.264 / HEVC: `performance-frames-modeN.csv` and `performance-last.json`.
- PyroWave: `pyrowave-output-modeN.csv`.
- Both backends: `debug-trace.txt`, `videoout-policy.log`,
  `<backend>-scanout-modeN.csv` and `<backend>-decisions-modeN.csv`.

`N` is 0 (Unpaced), 1 (Paced), or 2 (Paced+VRR). Each new stream replaces
its previous trace for that backend and mode, so all three modes can be tested
before collecting files. H.264 and HEVC share native traces: collect one codec
before testing the other. Each trace family has one file per backend/mode.
Native per-picture recording stops at 32,768 samples; PyroWave output, scanout
and decision rings retain their latest 32,768 records. Headers report omitted
or overwritten records. At 120 unique FPS, 32,768 pictures span about 273 s;
scanout and decision traces may cover less time because repeats and multiple
events per picture also consume records. Capture uses bounded memory and does not write files
in the streaming loop. Turning logs off prevents export. The previous native
reports inside `moonlight/` are legacy files; new reports are outside that
restricted pairing directory. Pairing credentials are never included.

Keep copies with names such as `hevc-paced.csv`, `hevc-vrr.csv` and
`hevc-unpaced.csv`. Generate a standalone HTML graph and JSON statistics:

```sh
python3 tools/plot-frame-pacing.py hevc-unpaced.csv hevc-paced.csv hevc-vrr.csv \
    --fps 120 --output hevc-pacing.html
```

The green line is the requested frame period: 8.33 ms at 120 FPS, 16.67 ms at
60 FPS. Look for fewer long intervals followed by short intervals, lower p95,
p99 and RMS interval error, without more dropped frames or additional latency.
The summary excludes the first three seconds where the trace is long enough.
Plots preserve interval minima/maxima when reducing samples for display.
Counts above 1.5 periods and below 0.5 periods are indicators, not a universal
pass/fail threshold. Fixed-refresh output can legitimately alternate intervals
when the source FPS is not divisible into the physical refresh rate.

## What these clocks mean

Native submit is a CPU marker after the presentation call. Native completion is
the CPU observation after the completed flip wait; polling/scheduling delay is
included. This is stronger than measuring decode speed, but it is not an exact
hardware scanout timestamp.

PyroWave submit is the CPU callback immediately before `queuePresent`. Paced
modes wait for GPU preparation before this callback; Unpaced preserves its
original order and only records the marker. `observed_us` records a CPU observation of the associated VideoOut completion
when available; the worker also samples counters while retiring prior flips. Repeated counts do not represent new flips.
If the count jumps by more than one, the graph excludes that interval and reports
an aggregated observation: individual display intervals cannot be reconstructed.
A single prepared image is still used; this instrumentation adds no GPU pipeline.

CPU submission graphs alone cannot prove smooth display. GPU fence completion
is not scanout. VideoOut counter observations do not prove that the TV activated
VRR. For end-to-end confirmation, use a high-speed camera on a scrolling scene
or visible frame counter, alongside the TV's refresh-rate indicator. These traces
do not measure input-to-photon latency.


## Presentation modes and low-FPS capture

- **Unpaced:** newest ready picture, without an intentional cadence wait. VSync
  On synchronizes output; Off requests the immediate/HSYNC path with a platform
  fallback to VSync. There is no adaptive VRR repetition policy in this mode.
- **Paced:** fixed nominal 60 Hz at requested FPS ≤60, 120 Hz above 60. With
  VSync On, fractional placement uses observed completion as a best-effort
  phase estimate. A source rate that does not divide into refresh can alternate
  refresh-slot counts. Saved VSync Off remains available.
- **Paced+VRR:** requests an HFR-capable variable output and forces synchronized
  flips. When variable output is active, VrrRepeatPolicy supplies the sole fresh
  and repeat admission clock; fixed FramePacing is not added as another wait.
  A refused variable-output request uses fixed pacing. Changing modes restores
  the saved VSync preference.

Host VRR is a host capture/display hint, not the PS5 HDMI VRR switch. Record
system VRR settings and TV readings even if the output API reports success.
The launcher separately requests fixed 60 Hz; its mode does not cap a later stream.

Active VRR repeats the retained image when no new picture is ready. Native AGC
reuses its completed buffer without holding a decoder slot; PyroWave re-renders
retained planes without decoding the compressed picture again. Repeats run on
existing presentation workers and stop at teardown. Fixed native output holds
the last image without this VRR repeat loop; PyroWave has a separate fixed idle
path whose first timeout allows two missing source periods. Do not apply that
timeout to the active VRR policy.

The verified stable source-FPS/factor/target table and cadence confirmation rules
are in [Configuration](CONFIGURATION.md#custom-fps-and-low-rate-vrr-compensation).
Stable 30 FPS uses factor 3 (~90 Hz), 35 uses 3 (~105 Hz), 40 uses 2 (~80 Hz),
and 51 uses 2 (~102 Hz). Confirmed sparse 16 FPS uses factor 5 (~80 Hz),
regardless of whether the requested rate was 30, 51, 60 or 120. A completely
silent source retains its last fitted repeat policy; it cannot supply a new
cadence estimate. Repeated images do not add motion information.

Console regression checklist (system VRR on, logs on):

- HEVC HDR 4K120, VSync On, Classic decoder, 3 cores: test Paced and Paced+VRR.
  Leave a static desktop for 60 seconds, then scroll/move windows for 60 seconds.
  Record TV refresh and brightness flicker in each phase and transition.
- Repeat with Sunshine and Vibepollo, Host VRR Off, then PyroWave and H.264 SDR.
- Test 60/120 FPS motion, exit/reconnect/switch codecs without restarting.
  Verify idle repeats stop on exit and pairing survives.
- Repeat with system VRR Off as a control. A VRR logo does not measure refresh;
  report its numeric range too.

Firmware 13.00 and 13.60 require console confirmation. The exact meaning of
`0x8029001c` has not been established from public documentation; do not identify
it as a firmware limitation based only on that code.


VideoOut ABI correction: Peg takes a handle and two reserved 64-bit arguments,
both zero. This was verified against the local console's firmware 13.60 library:
nonzero reserved arguments return InvalidValue (`0x80290001`). The prior
one-argument declaration left these registers undefined and explains the test
receipts. Peg also requires the process-local Unpeg state; the wrapper now
attempts Unpeg then Peg, recording both results even when Unpeg fails. These are
public exports, without hidden entry points or memory patches. Firmware 13.00
still requires confirmation. A successful API result is not physical HDMI timing
proof; record the TV's numeric refresh range during motion and transitions.

Public emulator stubs establish export names, not the complete ABI or firmware
behaviour. A fixed-output request, API-active state and actual panel refresh
must be checked separately; repeat scheduling does not guarantee the absence
of brightness flicker.

With diagnostic logs enabled, `videoout-policy.log` records each peg/unpeg return
code, selected mode, handle and API-active state. This bounded (64 KiB) receipt is
written only during output setup, directly to the log directory, independently
of redirected stdout. It is deliberately separate from frame timing and the old
component logs. Current diagnostic messages go to `debug-trace.txt`;
old component files are not evidence of the current session.


## Source timeline, feedback and admission

Source PTS increments map onto a local monotonic timeline; source and PS5
clocks do not share an epoch. The timeline corrects persistent drift in bounded
steps, does not treat one late picture as a phase change, and re-anchors on
nonmonotonic timestamps, source gaps over 250 ms or a phase mismatch over
100 ms. With zero reserve it returns readiness time. Otherwise its target is
no earlier than readiness and no later than readiness + reserve + 2 ms.

The readiness learner uses consecutive unique frames and source-versus-ready
jitter, with a 32-sample window (p90 Balanced, p95 Smooth). At least 12 samples
are needed; reserve increases at most 250 µs per sample and releases by up to
50 µs. Output-spacing feedback additionally requires confirmed completion of
the same consecutive unique flip, delayed readiness and delayed output. Where
request timestamps exist, delayed requests must corroborate the observation;
completion-poll jitter alone cannot train the reserve. Qualifying runs increase
reserve by 250 µs after three consecutive misses; other samples release 40 µs.
Lost confirmation or source discontinuity resets feedback history and reserve.
`feedback_misses` is a learning counter, not a count of dropped pictures.

Low Latency / Balanced / Smooth cap the two reserve estimates at 0 / 8 / 16 ms.
Effective reserve is their maximum, not their sum. Retained copies do not train
unique-frame feedback. Fixed output uses its own pacing learner; these VRR
profiles do not change Unpaced or fixed Paced.

The architecture is independently adapted for PS5 from the source-clock and
feedback approach described in Nonary's
[VRR worker](https://github.com/Nonary/moonlight-qt/blob/master/app/streaming/video/ffmpeg-renderers/pacer/vrrpacingworker.cpp).
Qt renderer code is not copied.

## Repeat deadlines and fresh-frame priority

Deadlines advance on the **submission** grid. A VideoOut completion observation
authorizes another flip but does not reset that grid to the CPU polling time.
The previous requested flip must retire; `UINT64_MAX` (initial “no picture”)
is not evidence of retirement. GPU fences likewise do not establish scanout.

Outside low-rate compensation, the first idle repeat waits until the last
unique submission + fitted source period + profile grace (750 / 1500 / 2500 µs
for Low Latency / Balanced / Smooth), subject to the existing next deadline.
After that, idle repeats use a 16 ms watchdog interval (~62.5 Hz scheduling).
This watchdog is distinct from the fitted target refresh and does not promise
a physical HDMI minimum. On resumed normal motion, a fresh picture respects
the 8342 µs display floor rather than waiting a full idle-repeat interval.

For LFC, repeats use the fitted `period / factor`, clamped to the display floor
and 16 ms. A repeat slot near the next expected unique picture can be delayed
by the profile grace, but no later than 16 ms after the previous submission.
A fresh image consumes the next legal slot; the slot has a 95% spacing guard
and the display floor. Normal motion uses a 98% source-period spacing guard.
Controlled catch-up is allowed only with a queued successor and queue age
above one fitted period; it never exceeds the display ceiling.

A queued fresh picture takes precedence over an optional repeat. PyroWave
waits without early acquisition for normal-motion idle repeats, and rechecks
running/queue state immediately before acquiring a repeat image. If superseded,
it records `repeat-superseded` and returns to fresh work without WSI backoff.
Once an image is acquired its GPU/semaphore sequence must finish; a later arrival
cannot cancel it. For LFC, preparation lead is retained: a recent p90 preparation
estimate (startup maximum) combined with half the p95 renderer-service estimate,
bounded at 6 ms. Temporary optional-repeat failures use 5–250 ms capped backoff.
These CPU/GPU constraints can produce transition outliers even when a target
frequency is unchanged; TV testing remains necessary.

## Reading bounded traces

`pyrowave-scanout-modeN.csv` and `native-scanout-modeN.csv` contain software
observations of VideoOut counts, including repeats. `kind=coalesced` or
`count_delta>1` means the observation missed intervening flips. Do not invent
individual hardware timestamps for them. Use `tools/plot-frame-pacing.py` for
counter-observation plots alongside the per-picture traces. The requested FPS
reference is a **unique-picture** period; an LFC scanout trace has a different
expected interval (`period / factor`).

`pyrowave-decisions-modeN.csv` records arrival, dequeue, capacity/age loss,
target, submit and repeat arbitration. Its `source` is raw 90 kHz RTP for
arrival/dequeue/drop events and unwrapped microseconds for target/submit.
Native decisions use source microseconds. Rings export after teardown and
replace the prior backend/mode capture; Off skips allocation/export. No per-event
disk write occurs in receive/presentation loops.

Compare unique pictures, retained repeats and network losses separately.
Native `shown` counts unique pictures; PyroWave live `shown` includes repeats.
`source_fps` is the rounded cadence estimate, `repeat_factor` counts total
presentations per source period, and `target_refresh_x100` is the capped
scheduler target. Their product need not equal observed `shown` per second
during transitions or stalls. None of these counters proves HDMI timing.
Retain warm-up and transition outliers separately from steady-state percentiles.
