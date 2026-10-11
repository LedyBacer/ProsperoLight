# Repeatable frame-pacing validation

The host stimulus and log collector are in [`tools/pacing-test`](../tools/pacing-test).
No application installation, browser extensions or external web services are required for the scene.
The collector requires Python 3 and uses only its standard library.

## Preparation

1. Fully close ProsperoLight and remount the new PPSA99002 image. Verify the About build label.
2. Enable Diagnostic logs (the single switch for detailed text and CSV/JSON). Use wired networking. Keep the host display at 120 Hz or higher and the browser visible on the captured display. Do not change TV settings between runs.
3. Baseline: PS5 system VRR On, “Apply to Unsupported Games” Off, 120 Hz output enabled; VSync On in ProsperoLight. Record firmware and TV model/settings.
4. Open `tools/pacing-test/index.html` on the PC; a local file works. The canvas is always 3840×2160; the selected stream resolution determines the encoded resolution. The scene is SDR even when the stream transport is HDR, so it does not test HDR colour accuracy.
5. Start the collector on any LAN computer: `python collect.py --watch --host PS5_IP --output pacing-logs`. Allow FTP through the PS5 environment. It archives completed-session traces so subsequent runs do not overwrite them. Ctrl+C stops it. Each archive includes the current build label and bounded logs.

## Each run

1. Apply the matrix row's settings in ProsperoLight and connect to Desktop.
2. Set matching labels on the host test page. These labels **do not configure the client or host**.
3. Start the page in full screen. Let its complete 90-second schedule finish: warm-up 10 s, motion 20 s, static 20 s, motion 20 s, then motion/static/motion/static for 5 s each. Remain in the stream without the PS5 system menu.
4. If using the LAN server, wait for the page to confirm automatic JSON upload; otherwise download the JSON report. Then stop streaming. Wait **15 seconds** before starting the next stream so the collector can capture completed traces. Check its “Saved” message.
5. Record the matrix ID, visible flicker/stutter, TV Hz during motion/static/return to motion, and any error. Match the JSON settings to the actual client settings.

The page animates according to elapsed time at every browser refresh. It deliberately does not cap its drawing to the client FPS: otherwise a 45 FPS page on a 120 Hz host would introduce its own uneven timing. Static phases do not repaint or update a visible timer. The JSON reports browser refresh and warns about hidden/background tabs or insufficient host refresh. These measurements describe the **host browser**, not PS5 HDMI scanout. PC and PS5 clocks are not assumed to be synchronized.

## Matrix

The explicit cases and blank result columns are supplied in [`matrix.csv`](../tools/pacing-test/matrix.csv).
Start with **A49, A50, A51 for all three codecs**: nine runs, about 14 minutes plus reconnect time.

| Group | Resolution / codec | FPS | Policy | Purpose |
|---|---|---|---|---|
| A | 1080p SDR, PyroWave / HEVC / H.264 | 45, 48, 49, 50, 51 | Paced+VRR, VSync On | Boundary behaviour and parity of decoders; 15 runs |
| B | 1080p SDR, PyroWave | 30, 40, 60, 75, 90, 100, 120 | Paced+VRR, VSync On | Other Custom values; seven runs |
| C | 1080p SDR, PyroWave / HEVC | 45, 75, 90, 120 | Paced, VSync On | Fixed-refresh regression; eight runs |
| D | 4K HDR, PyroWave / HEVC | 45, 49, 50, 90, 120 | Paced+VRR, VSync On | Production-resolution/HDR regression; ten runs |
| E | 1080p SDR, all three codecs | 120 | Unpaced, VSync On | Baseline; three runs |
| F1 | 1080p SDR, PyroWave / HEVC, PS5 VRR Off | 45 | Paced+VRR, VSync On | Fixed-output/API-fallback behaviour; two runs |
| F2 | 1080p SDR, PyroWave / HEVC, PS5 VRR On | 90 | Paced+VRR, saved VSync Off | Synchronization forced On while using VRR; two runs |
| F3 | Same codecs, immediately after F2 | 90 | Paced, VSync Off | Saved Off preference restored; two runs |
| G | 4K HDR PyroWave or HEVC | 120 | Paced+VRR, VSync On | 30–60 minutes in an actual game; choose one, then the other if needed |

Use fixed bitrates throughout: 1080p PyroWave **200 Mbps**, HEVC/H.264 **20 Mbps**; 4K PyroWave **500 Mbps**, HEVC **80 Mbps**. These are test conditions, not general quality recommendations. Avoid H.264 4K120 in the primary pacing matrix: its known decoder load can obscure presentation problems. Test it separately if investigating decoder performance.

## PS5 predictive readiness and receive-deadline feedback

PyroWave uses the existing moonlight-common-c extension
`LiSetVideoReassemblyDeadlineCallback`. The renderer publishes a bounded
source-RTP-to-LiGetMicroseconds deadline, subtracting learned p95 dequeue-to-
GPU-prepared service. The receiver reads one atomic snapshot; no locks, queue
access or allocations. Unknown, stale, unpaced, or discontinuous RTP periods
return 0, so the original 1ms packet-silence rule stays in force. The existing
common-c patch still requires the final packet plus all critical data and never
permits synthesizing missing H.264/HEVC reference chains. This is a targeted
late-partial-frame path, NOT permission to drop packets on a clean wired LAN.

For variable output, only confirmed consecutive unique flips with *both* delayed
readiness and delayed output can supply additional spacing-based reserve.
Request timing must corroborate the output delay when available, excluding
CPU counter-poll jitter as evidence; lost confirmation resets the feedback
epoch and its reserve.
Host/game FPS changes, retained LFC frames, and fixed-VSync wait cannot train
this model. Feedback attack is 250us per qualifying sample after three
consecutive misses; release is 40us per clean sample. Effective VRR reserve is
the max of source/readiness learning and attributable output feedback, clamped
to the configured Low Latency/Balanced/Smooth ceiling. Renderer readiness
p95 also informs fractional fixed HFR submissions without adding VRR playout
buffer to Paced. Unpaced retains zero intentional wait and discards a frame
that was overtaken in the PyroWave queue before GPU ingest.

Regression: 4K120, 90/120 fixed Paced, 50/51 LFC, low FPS static,
late partial PyroWave blocks, 30min stable wired LAN, controlled UDP jitter,
VRR enabled/disabled and delayed GPU decode. Verify critical detail remains
intact; a premature partial-frame release is worse than 1 ms latency.

## Current scheduling behaviour

Use the [verified FPS/factor/target table](CONFIGURATION.md#custom-fps-and-low-rate-vrr-compensation)
and [deadline/clock definitions](frame-pacing-measurements.md) as the reference.
They describe the current algorithm rather than earlier PR iterations.

Active Paced+VRR uses source PTS mapped onto the local monotonic clock and a
single VrrRepeatPolicy admission clock. Low Latency / Balanced (default) /
Smooth limit intentional reserve to 0 / 8 / 16 ms. Both backends use the same
cadence fitting and repeat-factor policy; their GPU/decoder readiness and
counter-observation paths differ. Fixed Paced and Unpaced do not use this
active-VRR policy.

Verify these stable cases (119.88 Hz-capable variable output):

| Source FPS | Repeat factor | Scheduler target (Hz) |
| ---: | ---: | ---: |
| 16 (sparse/static) | 5 | 80.00 |
| 30 | 3 | 90.00 |
| 35 | 3 | 105.00 |
| 40 | 2 | 80.00 |
| 45 | 2 | 90.00 |
| 48 | 2 | 96.00 |
| 49 | 2 | 98.00 |
| 50 | 2 | 100.00 |
| 51 | 2 | 102.00 |
| 60 | 1 | 60.00 |
| 75 | 1 | 75.00 |
| 90 | 1 | 90.00 |
| 100 | 1 | 100.00 |
| 120 | 1 | 119.87 |

The factor includes the original presentation; duplicates add no motion
information. The 120 FPS target rounds to 119.87 in the integer calculation,
while the nominal VideoOut mode is 119.88 Hz. `expected_motion_target_hz` in
CSV is a scheduler target under successful variable output, not a TV pass/fail
frequency. Its fixed rows require separate inspection of API and TV receipts.
35 FPS is an additional targeted check; the supplied 49-run CSV is unchanged
in scope and does not contain it.

Cadence confirmation needs eight intervals and 200 ms of sender time. Original
frame numbers normalize local skips and permit capture-clock quantization;
mixed windows can restart fitting. Periods faster than nominal or within 5%
of it snap to nominal. A modest 5–25% slowdown while still near nominal needs
750 ms of mutually consistent candidate windows (3% tolerance). Sustained FPS
changes can therefore legitimately change the factor; this is not an invariant
60 Hz lock. A fitted sparse period over 125% of nominal recovers after three
consecutive source intervals from 6 ms through 105% of the nominal period.

Test motion → sparse capture → motion separately from complete source silence.
For example, 51 → 16 → 51 FPS can converge to 102 → 80 → 102 Hz; no fixed
102 Hz grid is held across static phases. With no new timestamps the last
fitted policy remains in use. Keep transitional outliers and TV brightness
observations; steady-state targets alone cannot demonstrate smooth transitions.

Outside LFC, the first idle repeat allows one source period plus profile grace
(750 / 1500 / 2500 µs), then a 16 ms idle watchdog. LFC uses its fitted repeat
interval and grace near an expected new picture, with a 16 ms watchdog bound.
Submission times anchor the repeat grid; completion polling only authorizes
admission. New normal-rate pictures after an idle repeat respect the display
floor, not an extra full 16 ms delay. PyroWave rechecks fresh work before repeat
acquisition (`repeat-superseded`); acquired images must finish their GPU/WSI
sequence. Retained LFC images retain bounded preparation lead.

Test unique frames arriving 0.3–3 ms late near a repeat slot, all VRR timing
profiles, long static periods and static-to-motion return. Compare `target`,
`repeat-target`, `repeat`, `repeat-skipped`/`repeat-deferred` and
`repeat-superseded` where the backend emits them. Optional-repeat errors retry
with 5–250 ms capped backoff; verify recovery without triggering a new capture.

## Expected results and interpretation

Unpaced has no intentional cadence wait; its VSync setting selects synchronized
or requested immediate output, with platform fallback. Fixed Paced requests
nominal 60 Hz at FPS ≤60 and 120 Hz above 60. With VSync On, non-divisible
source rates naturally alternate refresh-slot counts (45 FPS on 60 Hz is an
example). Paced+VRR forces synchronized flips and requests variable HFR output;
if rejected, fixed pacing applies. F2/F3 verifies restoration of saved VSync Off.

Initialization and sparse-to-motion recovery should converge to the same stable
factor. There must be no persistent 60 FPS ceiling with 90/120 FPS input, no
sustained stale growth in stable motion, crash or hang. Record brightness
flicker separately: variable refresh changes can affect the panel even with
correct software deadlines. Firmware 6.02/13.00/13.60 and TV acceptance still
need real-console evidence; public API receipts do not prove physical HDMI VRR.

Compare unique pictures, repetitions and network losses separately. Native
`shown` counts unique pictures; PyroWave live `shown` includes repeats. Cadence
receipts describe rounded source FPS, the repeat factor and the capped scheduler
target. CSVs contain software observations, not calibrated HDMI timestamps.
Exclude warm-up only when calculating steady percentiles; preserve transition
outliers in the report. Tiny losses from requested 120 FPS versus 119.88 Hz,
source jitter or decoder load must not be confused with the old 60 FPS cap.

The automated host tests cover Custom FPS 30–120, sparse recovery, local skips
and delayed observations. They verify policy calculations, not panel brightness
or firmware acceptance. For a visual comparison, use the reference scene and
HEVC 60 FPS / Paced / VSync On, then HEVC/PyroWave 51 FPS / Paced+VRR.
Prefer a 120 Hz host display for the 60 FPS reference: a 200 Hz browser sampled
at 51 FPS can itself produce uneven animation steps. The page JSON measures
host-browser timing, not PS5 output or input-to-photon latency.
