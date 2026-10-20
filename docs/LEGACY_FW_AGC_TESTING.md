# Legacy firmware AGC tests — round 2

Experimental launcher diagnostics for **FW 3.00 and reported FW 4.50 failures**.
Please report the **actual exact firmware**, including other 4.x versions.
These are not release builds or streaming/performance validation.

The successful CI run and exact ZIP checksums are recorded in
[LEGACY_FW_AGC_ARTIFACTS.md](LEGACY_FW_AGC_ARTIFACTS.md).
Open that run, sign in to GitHub and scroll to **Artifacts**. Download the
`ProsperoLight-FWCompat-<variant>-<source SHA>` artifact. Unzip the GitHub
artifact wrapper, then the installable ZIP inside it. Developer artifacts named
`FWCompat-symbols-evidence-...` are for developers; do not install their ELF.

| Variant | Difference from control |
|---|---|
| 00-control | All four original queries; ordinary later AGC initialization |
| 07-compute-only | Only compute memory query; streaming explicitly refused |
| 08-codecs-only | H.264 SDR → HEVC SDR → HEVC Main10 HDR memory queries; streaming refused |
| 09-agc-first | Same AGC init(8) before all four queries and storage; renderer reuses it |

## Test one build at a time

1. Close ProsperoLight normally. Back up its installed app folder **outside**
   `/data/homebrew`. Preserve **all of `/data/prosperolight`**, including saved
   configuration, pairing and cache. Note existing log/crash report timestamps.
2. Check the installable ZIP SHA256 against the artifact list. Replace the
   **complete** installed `/data/homebrew/PPSA99002` with the ZIP's `PPSA99002`
   folder. Do not overlay old files or register multiple copies of PPSA99002.
3. Check `PPSA99002/build-label.txt` for the variant and source SHA. Use matching
   developer evidence from the **same CI run**, not another local build.
4. Launch once and wait **90 seconds**. Record whether the actual menu is
   visible and responds. Splash, black screen, EGL success or absence of a crash
   is insufficient. `RESULT=LAUNCHER_READY` must agree with a visible menu;
   `RESULT=GRAPHICS_FAILURE` is failure even with a clean exit.
5. **Do not start a stream with any variant**, including 09. Close normally.
   Copy the logs immediately, before installing or launching another variant.
   If the app cannot launch at all, stop and report; no repeated attempts needed.

**FW 3.00:** 07 → 08 → 09. New 00 is optional if the earlier control failure is
still known; use it if launch conditions have changed.

**FW 4.x:** 00 → 07 → 08 → 09. Establish whether the control failure reproduces.

## Save after every launch

Copy these from `/data/prosperolight/logs/` into a separate folder per variant:

- `prosperolight-launcher.log` — the next launch can rotate it.
- `fwcompat-startup.jsonl` — match variant, source SHA and `process.start_unix`.
- `crash-last.txt` **only if newly generated**; include relevant console/kernel
  debug output if available. An unchanged old crash report is not a new crash.

If storage elevation fails, logs may be under `/download0` or its sandbox
backing directory. Early checkpoints also go to console debug output and are
replayed into the launcher log when storage opens. Without either storage or
console capture, early evidence may be unavailable; say so explicitly.

Include exact firmware, exploit/enabler and version, ShadowMountPlus version,
TV resolution/refresh/HDR/VRR mode, build label, visible result and exit status.
Firmware is operator supplied: this diagnostic does not add a guessed firmware
API import. Do not share pairing files, credentials, private addresses or host
names; redact such details from logs before public posting.

| Variant | Actual FW | Visible responsive menu | Exit / crash / unable to launch | Notes |
|---|---|---|---|---|
| 00-control | | | | |
| 07-compute-only | | | | |
| 08-codecs-only | | | | |
| 09-agc-first | | | | |

## Restore

Close the test app normally. Replace its **entire app folder** with your saved
normal release or a verified release ZIP. Leave `/data/prosperolight` untouched.
Diagnostics disable production updater entry points; restore the release
explicitly. Do not continue streaming experiments until startup logs are reviewed.

Interpretation and known evidence boundaries:
[LEGACY_FW_AGC_ANALYSIS.md](LEGACY_FW_AGC_ANALYSIS.md).
