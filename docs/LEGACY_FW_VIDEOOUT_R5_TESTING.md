# Round 5: automatic VideoOut version policy and fixed 120 Hz

Build: `19-auto-videoout`; branch `fix/legacy-fw-agc-round2`.

This is the first runtime-selected diagnostic build, **not yet production**.
On startup the PS5 `kern.sdk_version` is read via `sysctlbyname`.
If the query succeeds and the packed version is at least `0x05100000`,
existing VideoOut VRR calls remain enabled. If it fails or reports older
firmware, all VRR Peg/Unpeg operations are bypassed, including calls from RADV.
This is a **conservative firmware gate**, not direct export resolution.
Homebrew enablers may spoof the reported system version: a falsely low version
disables VRR unnecessarily but does not disable HFR.

HFR is independent: `sceVideoOutIsOutputSupported(handle, 15, ...)` and
`sceVideoOutConfigureOutput(handle, 15, ...)` are used for streams above
60 FPS regardless of VRR policy. 120 Hz depends on the title's HFR entitlement,
the TV and actual VideoOut result. Requesting 120 FPS does not prove 120 Hz.

## Validation order

1. Preserve the successful `18-legacy-videoout` ZIP and logs as a rollback.
2. Install `19-auto-videoout` from this commit's successful GitHub Actions
   build; check `build-label.txt` and source SHA.
3. On FW 3.00, test 1080p60 H.264, HEVC and PyroWave, SDR, 20 Mbps; confirm
   first frames, picture, audio, and clean exit.
4. On FW 3.00 with a 120 Hz-capable TV/monitor, test H.264 1080p120 and
   optionally HEVC/PyroWave. Keep VRR off. Inspect `Native AGC stage 3`,
   `VideoOut status`, `wsi/videoout` and physical scanout reported refresh.
5. Repeat with VRR selected in the UI on FW 3.00. It must **not** call VRR
   APIs, crash, or claim VRR active; HFR may still use fixed 120 Hz.
6. On a newer, VRR-capable firmware and HDMI display, compare 60/120 fixed
   and 120 VRR output with production to rule out regressions.
7. Collect `fwcompat-startup.jsonl`, launcher and VideoOut policy logs,
   stream/debug logs and **fresh** crash data if a run fails. Include firmware
   version and actual TV 120 Hz/VRR capabilities.

Report the `videoout.vrr_firmware` and `videoout.vrr_api_available`
checkpoints, HFR supported/configured return codes and measured scanout.
Only after both legacy and modern hardware results should the auto guard
replace the diagnostic flag in the production build.
