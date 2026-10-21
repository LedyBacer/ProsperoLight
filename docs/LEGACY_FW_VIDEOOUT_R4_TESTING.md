# FW 3.x AGC-first / fixed-VideoOut test — variant 18

Experimental test of the FW 3.00 stream crash after working AGC-first launcher.

## Changes compared with 09-agc-first

- Retains full early Videodec2 warm-up and exactly one early AGC initialization.
- Avoids all runtime calls to VideoOut VRR Unpeg/Peg from the stream opening
  wrapper, **both exported link-time VRR wrappers (also called directly by
  RADV WSI)** and the native AGC presenter's fixed-refresh policy in this
  diagnostic build only; does not change production behavior.
- On this build, a VRR Peg request is a no-op success (the older firmware is
  already in fixed-rate mode); Unpeg returns unsupported, rather than falsely
  claiming that variable refresh is available.
- Rejects requested refresh rates over 60 FPS and VRR-enabled presentation.
- Adds bounded launch/presenter checkpoints around VideoOut open, flip events,
  fixed-rate setup, and buffer registration.

The first variant 18 (commit 647a5bed) still crashed at RIP 0 when RADV WSI
called the VRR Peg wrapper, which forwarded to a missing Unpeg import.
The new variant 18 commit specifically guards both wrappers; compare the
full source SHA in build-label.txt rather than reusing the old ZIP.
The exact firmware-side cause remains unverified until the new hardware test.

## Test procedure (PS5 FW 3.00, optionally FW 4.x)

1. Back up the complete installed `/data/homebrew/PPSA99002` folder.
   Preserve `/data/prosperolight` (including pairing, cache, configuration).
2. Download the **18-legacy-videoout** installable ZIP artifact from this
   branch's successful **Legacy firmware startup experiments** Actions run.
   Extract the GitHub artifact wrapper, verify the inner ZIP checksum from its
   matching evidence `SHA256SUMS`, and replace the entire app folder.
3. Verify `PPSA99002/build-label.txt` contains
   `FWCompat 18-legacy-videoout` followed by the build SHA.
4. Launch the app; confirm the menu is visible and responsive.
5. Disable VRR and use **SDR, 1080p, 60 FPS, 20 Mbps**. Start with H.264.
   Only if it works, test HEVC, then PyroWave. Do not test 90/120 FPS yet.
6. Record whether each stream displays video, plays audio, crashes, returns to
   the menu, or freezes. Try a clean exit and one reconnect only after success.
7. **Immediately after every attempt** save the current files from
   `/data/prosperolight/logs/`: `prosperolight-launcher.log`,
   `fwcompat-startup.jsonl`, `videoout-policy.log` if present, any
   `native-agc-*.log`, and only a newly generated `crash-last.txt`.
   Include kernel/debug output for early crashes. Do not mistake old crash
   reports for new crashes or mix logs from different runs.
8. Report exact FW version, exploit/enabler, ShadowMountPlus version, selected
   codec/settings, the last `videoout.*` checkpoint and observable behavior.
   Redact network addresses and pairing details.
9. Restore your original complete `PPSA99002` folder after testing.

A working menu or successful VideoOut allocation does **not** prove a
successfully decoded/presented frame. This is experimental hardware testing,
not a production fix.
