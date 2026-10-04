# PyroWave validation report and regression checklist

## Hardware review

A console reviewer tested the integrated client on a PS5 with a 4K120 HDR display
(firmware 13.60) and Vibepollo 2.0.0. All profiles were checked at 3840×2160,
120 FPS; games used were Control Resonant and Rocket League. This is manual
hardware validation, not an automated visual
accuracy measurement. The reviewer confirmed that all seven profiles work after
the fixes in `c7d2e17` and `dae3679`:

| Profile | Reviewer result |
|---|---|
| H.264 High / 420 / 8-bit SDR | Working |
| HEVC Main / 420 / 8-bit SDR | Working |
| HEVC Main10 / 420 / 10-bit HDR10 | Working |
| PyroWave / 420 / 8-bit SDR | Working |
| PyroWave / 444 / 8-bit SDR | Working |
| PyroWave / 420 / 10-bit HDR10 | Working; HDR colours corrected |
| PyroWave / 444 / 10-bit HDR10 | Working; HDR colours corrected |

The reviewer separately confirmed the GPU statistics overlay (Touchpad+R1),
stream exit (Touchpad+L1), on-screen keyboard (Touchpad+Triangle), and mouse mode
(Touchpad+Square). Earlier reviews confirmed pairing, repeated connections,
manual bitrate entry and animated 4K120 output.

Two issues discovered during review were corrected:

- PyroWave HDR used reversed red/blue scanout packing. The scanout now uses
  `VK_FORMAT_A2B10G10R10_UNORM_PACK32`, matching PS5 HDR semantics.
- H.264/HEVC decoded frames but stopped at AGC initialization (`0x8a6c0004`).
  The presenter and import stub now use the SDK's version ABI, `sceAgcInit(8)`.
  The reviewer retested and confirmed these profiles working.

Quality observations remain relevant: at 500 Mbps, the reviewer preferred
PyroWave motion but found HEVC HDR clearer on nearly static content. A large
visual improvement from 444 over 420 was not reported. This change adds supported
profiles, not a new encoder or rate-control algorithm, and makes no claim that
PyroWave always outperforms HEVC in image quality.

H.264/HEVC were checked at 80 Mbps. PyroWave was checked at 80, 500, 600,
700 and 800 Mbps. The reviewer reported stable playback without observed losses
at 500–600 Mbps, and some losses at 700–800 Mbps while remaining comfortable to
use on their network. They preferred PyroWave at 700 Mbps over H.264/HEVC at
80 Mbps, particularly in motion; this is a subjective comparison at unequal
bitrates, not an equal-bitrate codec quality benchmark. The reported 0.7–0.8 ms
HUD latency/timing reading does not establish total input-to-display latency.
PyroWave → HEVC → PyroWave transitions without restarting the title passed.
A wired LAN and high bitrate are recommended for PyroWave.

The full Cartesian product has not been tested: 1080p/1440p, 60/90 FPS, 1000 Mbps,
ordinary Sunshine compatibility, USB peripherals, failure injection and a
controlled long-duration soak require additional reports. No such passes are
inferred from the profile confirmation.

## Build validation

The release process builds and signs the complete RADV client and the optional
`PYROWAVE=0` VideoDec2-only variant. It runs the repository formatting/static
analysis, host application tests, performance guards, tooling integration tests
and scalar/SIMD dependency checks. The accompanying build provenance identifies
the source commit, definitions, pinned dependencies, patch hashes and eboot hash.
Refer to the PR for the actual final command results and downloadable artifacts.

## Additional regression checklist

These checks are useful for maintainers and other console configurations:

1. Preserve an existing configuration and pairing; verify migration from v6 to
   v7, default 420, retained audio/V-Sync/decoder settings and host entries.
2. Select H.264, HEVC and PyroWave without restarting the title. H.264 must disable
   HDR; H.264/HEVC must keep chroma fixed at 420. Switching back to PyroWave must
   restore its chroma preference. Bit depth follows SDR/HDR, without a new control.
3. Run H.264 and HEVC SDR at 1080p60 and 2160p120; HEVC HDR at 2160p60/120;
   all four PyroWave profiles at 2160p120. Also check 1440p90 and PyroWave 1080p60.
   Observe audio, controller input, HUD, stream exit and reconnect in each case.
4. Check PyroWave → HEVC HDR → PyroWave SDR → H.264 transitions without restarting.
5. Enter 80/500/600/700/800/1000 Mbps. Confirm UI, saved config and stream-start
   bitrate in kbps; compare the host's requested session bitrate. Observed incoming
   Mbps may differ with scene complexity and protocol overhead. Reject empty,
   fractional, negative, 0 and 1001 input without replacing the previous value.
6. Restart after saving codec, resolution, FPS, bitrate, HDR, chroma, audio,
   V-Sync and decoder preferences. Keep one selected stream running 10–15 minutes.
7. Check HDR display indication, dark/bright gradients, saturated colours, and
   equal-bitrate 420/444 comparisons using thin coloured text/edges and motion.
8. Check V-Sync on/off and TV-safe/edge-to-edge. Platform HSYNC refusal must be
   logged and fall back to VSYNC. Check keyboard navigation, Shift, Backspace,
   Enter, mouse clicks/motion and available physical keyboard/mouse devices.
9. On ordinary Sunshine, verify H.264/HEVC. Unsupported PyroWave must produce a
   clear error without damaging settings or pairing. Separately check host HDR
   disabled, SDR display and a mismatched bitstream ID; no silent SDR fallback.

## Logs and reproducibility

`/download0/prosperolight-session.log` records stream profiles, negotiation,
VideoDec2/AGC setup and errors. `/download0/prosperolight-pyrowave.log` records GPU
profile, output mode, incoming/decoded/presented FPS, timings, loss and backlog.
The installed test title uses PPSA99018; upstream's normal app identity remains
PPSA99002. Both packages use the same signed eboot. Pairings and user configuration
are deliberately not distributed in the archives.

For an issue report include the build hash, console firmware, host version,
profile, resolution/FPS, bitrate, V-Sync, failure time, shortcuts used and logs.
