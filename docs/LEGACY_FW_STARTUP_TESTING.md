# Legacy firmware startup tests (experimental)

For PS5 FW **3.00 and 4.50**; reports from other older firmware are welcome.
These are launcher diagnostics, not a production release or a performance test.
All seven builds use title ID **PPSA99002**. Install **one at a time**.

## Install and test

1. Close ProsperoLight normally. Save the existing app folder somewhere outside
   `/data/homebrew`, and note the timestamps of existing logs/crash reports.
   Keep `/data/prosperolight/config`, `pairing` and `cache` unchanged.
2. Unzip the selected `ProsperoLight-FWCompat-<variant>.zip`. Replace the complete
   installed `/data/homebrew/PPSA99002` folder with the ZIP's `PPSA99002` folder.
   Do not overlay files onto an older app. Do not register two copies with this
   title ID: the loader cannot distinguish them reliably.
3. Read `PPSA99002/build-label.txt`: it must name the chosen variant and source
   SHA. Check the ZIP against the supplied `SHA256SUMS`.
4. Start the app once. Allow **90 seconds** for initialization. Record whether
   the actual menu is visible and responds. A splash/black screen, a graceful
   graphics failure or “not crashed” alone is **not PASS**.
5. Do **not stream games** in this investigation. In particular, `02` and `06`
   intentionally skip decoder initialization and refuse streaming. Close the
   application normally before changing variants.

Recommended order: **00 → 01 → 02 → 03 → 04 → 05 → 06**. Keep the control
result even if it fails. If a build works, still test the remaining isolated
variants; combined success alone does not identify one cause.

## Return these logs after EACH attempt

Normally under `/data/prosperolight/logs/`:

- `prosperolight-launcher.log` (copy it before the next launch rotates it).
- `fwcompat-startup.jsonl` (latest attempt only).
- `crash-last.txt` **only if newly generated**, plus relevant kernel/debug output.

If filesystem elevation fails, logs may instead be in `/download0` (or its
sandbox backing directory); the console debug output is especially important
if startup fails before elevation. Early checkpoints go directly to console
output and are replayed into the launcher log once storage opens. Without
storage access and without captured console output, early evidence may be lost.

An old `crash-last.txt` is not evidence that this attempt crashed. Save the old
report before testing; compare its modification time/content afterwards. Match
new logs to `[FWCOMPAT] variant=...`, `git_sha...`, and `process.start_unix`.
Keep the matching developer ELF out of the console installation. Do not mix
symbols from local builds and GitHub CI builds, even with the same variant/SHA;
use the artifact receipt and checksums supplied with that exact ZIP.

`RESULT=LAUNCHER_READY` means the diagnostic code saw the first render with no
GL error or flagged AGC failure, followed by a successful EGL swap. **Also say
whether you actually saw the menu**: software checkpoints do not prove visible
HDMI output. `RESULT=GRAPHICS_FAILURE` is FAIL, even if there is no crash.

Send firmware, jailbreak/enabler and version, ShadowMountPlus version, TV mode,
variant/build label, visible first frame, crash/clean exit, and the logs above.
Redact private host names, addresses and account details before public posting.

| Variant | Menu opened / first frame visible | PASS / FAIL | Crash / clean exit | Notes |
|---|---|---|---|---|
| 00-control | | | | |
| 01-no-warmup | | | | |
| 02-no-videodec2 (launcher only) | | | | |
| 03-no-shader-cache | | | | |
| 04-force-1080p | | | | |
| 05-no-pyrowave | | | | |
| 06-combined (launcher only) | | | | |

## Restore

Close the test app normally. Replace its **whole app folder** with your saved
normal release (or a verified normal release ZIP). Leave your data folder
untouched. Diagnostics disable the production update check/apply entry points;
restore the normal app explicitly when testing ends.
