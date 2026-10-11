# Application configuration and versioning

[`sce_sys/param.json`](../sce_sys/param.json) is the single source of truth for
application identity, Shell metadata, and release versioning. The build
validates it and copies it unchanged into `dist/<TITLE_ID>/sce_sys/param.json`.
There is no second project manifest to keep synchronized.

## Identity and metadata

Change these fields together when creating a new application:

| `param.json` field | Purpose |
| --- | --- |
| `localizedParameters.<language>.titleName` | Name displayed by the Shell. |
| `titleId` | Unique `PPSA` plus five-digit application identifier. |
| `conceptId` | Five numeric characters; normally the numeric title-ID portion. |
| `contentId` | Package identity containing the title ID and a 16-character suffix. |
| `contentVersion` | Application and repository release version in `NN.NNN.NNN` form. |
| `masterVersion` | Compatible release baseline in `NN.NN` form. |
| `downloadDataSize` | Reservation that makes `/download0` available. |

Keep `titleId`, `conceptId`, and `contentId` stable for updates to one installed
application. Use a new identity only when the result should be a separate title.

The supported initializer coordinates those values and the displayed name:

```bash
make init TITLE_ID=PPSA12345 APP_NAME="My Native App"
make init TITLE_ID=PPSA12346 APP_NAME="My Media App" APP_CATEGORY=media
```

It derives the 16-character content suffix from the app name unless
`CONTENT_SUFFIX` is supplied. It does not change `contentVersion`,
`masterVersion`, storage sizing, or unrelated metadata.

## One version everywhere

This project does not use Semantic Versioning. `contentVersion` is the only
release number and must use the PS5 `NN.NNN.NNN` format, for example
`01.002.003`. Use that exact value for the Git tag and GitHub Release name—do
not add a `v` prefix:

```bash
git tag 01.002.003
git push origin 01.002.003
```

The release workflow rejects a tag that differs from
`sce_sys/param.json`'s `contentVersion`. For ordinary development, keep
`masterVersion` at `01.00` and increment `contentVersion` for each release.
Change `masterVersion` only when intentionally changing the compatible release
baseline.

The loader-visible SDK and FSELF constants are internal build-format values,
not application versions. They remain fixed to the cross-firmware-validated
profile in `tools/build.sh`.

## Game and media categories

Category is represented directly by standard `param.json` fields:

| Area | `applicationCategoryType` | `contentBadgeType` | `gameIntent` |
| --- | ---: | ---: | --- |
| Games | `0` | `1` | Permit `launchActivity`. |
| Media | `65536` | `2` | Remove the field. |

The build validates these combinations but does not rewrite them. Category does
not grant codec, filesystem, network, background-execution, or other
entitlements.

## Source and runtime conventions

Every `.c`, `.cc`, and `.cpp` file below `src/` is compiled automatically.
Move experiments outside `src/` when they should not enter the build. C++20 is
the default; C files use C11. Exceptions and RTTI remain disabled.

The generated `runtime/libc.prx` is always included and verified against
`runtime/libc.prx.sha256`. Additional local PRXs may be placed under the ignored
`.local/runtime/` directory and selected with `APP_RUNTIME_MODULES`; the build
copies pre-signed modules and wraps raw ELF modules. Duplicate filenames are
rejected.

Packaged read-only data belongs under `assets/` and appears at `/app0/assets/`.

## Optional build inputs

Build-only choices use Make variables rather than application metadata:

| Variable | Space-separated values |
| --- | --- |
| `APP_DEFINITIONS` | Preprocessor definitions such as `FEATURE_AUDIO=1`. |
| `APP_INCLUDE_PATHS` | Repository-relative include directories. |
| `APP_STATIC_ARCHIVES` | Repository-relative `.a` files in linker order. |
| `APP_RUNTIME_MODULES` | PRX paths below the ignored `.local/runtime/` directory. |
| `PACBREW_PACKAGES` | PacBrew `pkg-config` module names. |
| `PACBREW_INCLUDE_PATHS` | Paths below PacBrew's `/user/homebrew`. |
| `PACBREW_STATIC_ARCHIVES` | PacBrew `.a` paths below `/user/homebrew`. |

For example:

```bash
make PACBREW_PACKAGES="sdl2 openssl" \
  APP_DEFINITIONS="FEATURE_AUDIO=1" \
  APP_INCLUDE_PATHS="include"
```

Set the same environment variables before `./build.ps1` on Windows. Paths in
these lists cannot contain spaces. See [PacBrew dependencies](PACBREW.md) for
manual archive examples.

Deployment uses a separate set of Make variables:

| Variable | Default | Purpose |
| --- | --- | --- |
| `PS5_HOST` | required | Console IPv4 address or hostname. |
| `FTP_PORT` | `2121` | FTP service port. |
| `PS5_FTP_USER` | `anonymous` | FTP username. |
| `PS5_FTP_PASSWORD` | `codex` | FTP password. |
| `DEPLOY_DRY_RUN` | `0` | Build without networking when set to `1`. |

These values affect only `make deploy`; they are not application metadata and
are never copied into the built title. See [Deployment](DEPLOYMENT.md).

Frequently used local build and deployment values may be kept in an ignored
hidden file:

```bash
cp .env.example .env
```

The file uses ordinary GNU Make `NAME=value` syntax. Do not put title identity
or release versions there: `param.json` remains the single source of truth for
the application. Do not commit credentials; `.env` is ignored by Git.

## Persistent configuration

Files live under `/data/prosperolight` after filesystem elevation, with
`/download0` as the sandbox fallback. Configuration version 8 migrates both
the upstream and PyroWave version 7 layouts, preserving saved hosts and ports. Write a
temporary file and rename it into place to avoid partial writes. Provide an
application-level export/import mechanism for important data because retention
after title deletion or cache-management actions is not guaranteed.

Native SaveData initialization is not part of this baseline; see
[Platform findings](PLATFORM_NOTES.md).

The packaged `lapy.elf` is built from the pinned upstream Lapy revision for
title `PPSA99002`. A resident service remains compatible, but is optional.

### Settings list navigation

The settings page scrolls vertically as the selected row moves out of view. Use
Up / Down to select a setting and Cross to change it. The scrollbar shows your
position; stream shortcuts and the footer stay visible outside the list.

### Custom server port

Use **Add PC** and enter `IPv4:HTTP-port`, for example `192.168.1.5:48000`.
This is the HTTP port (47989 by default), not the browser administration port.
The selected PC's address always shows its port. Automatic discovery retains
explicitly saved endpoints; the port is not a global streaming setting.

### Frame pacing and VSync

| Policy | Scheduling | VSync preference |
| --- | --- | --- |
| Unpaced | Submit the newest ready frame without a cadence timer. | On synchronizes flips; Off requests immediate output. |
| Paced | Learn source cadence, retain a bounded readiness reserve, and align fixed-VSync submissions using observed flip timing. | On/Off is retained independently. |
| Paced + VRR | Use source-clock pacing and request variable-rate VideoOut; API failure uses fixed-output pacing. | Synchronized flips are required; the saved preference is restored when another policy is selected. |

The app declares VRR to the console in `sce_sys/param.json`: `attribute3` carries `0x40`
(120 Hz) and `0x40000` (VRR). System software 6.02 rejects the newer `0x80000` "VRR 120 Hz"
bit as an unknown VRR parameter and turns VRR off for the app; its kernel log says so
(`[AvControl] ... (HFR:o VRR:x)` against `VRR:TypeA`). The console reads the declaration
when the app is registered, so a changed value needs the app deleted from the home screen
and registered again.

VRR is a request, not proof that the display accepted it. A rejected request
falls back to fixed output and is recorded in the session log. Native
immediate flips can also fall back to VSync if the system rejects them.
The launcher requests fixed 60 Hz when its own VideoOut handle opens. A subsequent
stream makes a new mode request; 60 Hz in the launcher does not limit that stream.
RADV checks the independent `0x40` HFR bit, not the legacy combined `0x80040`
mask, and restores the default mode even when its high-refresh request was refused.
With VRR, low measured vblank rates on sparse content do not imply a 60 Hz ceiling:
the PyroWave HUD separately reports the nominal mode and active pacing policy.

Menu sounds and the presentation policy are stored separately from the paired
host configuration in `prosperolight-ui-sound.bin` and
`prosperolight-presentation.bin` beside the main configuration
(`/data/prosperolight/config`, or `/download0` in the sandbox).

The list holds settings only; the panel beside it shows the six Touchpad
shortcuts, including the two that send the host's Back and Guide buttons.

Hardware validation must separate codec/decoder load from presentation policy.
H.264 4K120 can exceed its decode budget even in Unpaced; pacing does not remove
that limit. Use the [test matrix](PACING_TEST_MATRIX.md) and record the build,
firmware, API receipts, source FPS, TV readings and visible behaviour together.

### Frame rate and bitrate

Frame rate offers 30, 60, 90, 120 FPS and Custom. Custom exposes a 30–120 FPS
slider with a one-FPS step. Bitrate uses a separate slider, up to 1000 Mbps
while PyroWave is selected.

### Diagnostic logs

`Diagnostic logs` controls the detailed `debug-trace.txt` journal and the
end-of-stream CSV/JSON exports. It defaults to On and is stored beside the
main configuration as `prosperolight-logging.bin`. Detailed messages and export
requests stop when disabled; raw stdout/stderr redirection changes on the next
app start. The former separate Debug log preference is no longer used.

Stream and launcher messages have one text destination, `debug-trace.txt`.
The previous launch is kept as `debug-trace.prev.txt`; the current trace stops
at approximately 16 MiB. Raw console output is retained separately in the
bounded `prosperolight-launcher.log` fallback (8 MiB, current/previous launch).
CSV rings and native per-picture traces have their own bounded capacities;
see [measurement limits](frame-pacing-measurements.md).

### Source-clock frame pacing

Unpaced keeps the latest-frame policy without an extra software wait. Fixed Paced and fixed fallback use FramePacing, which learns source cadence and limits its readiness reserve to the smaller of 10 ms and one source interval. Active Paced+VRR uses VrrRepeatPolicy instead, with a source-PTS timeline mapped onto the local monotonic clock. Its Low Latency / Balanced / Smooth profiles cap intentional reserve at 0 / 8 / 16 ms; the effective reserve is the maximum of readiness and attributable output-feedback estimates, not their sum. The ready queue holds at most two decoded native images. Old decoded images are discarded only when a decoded successor exists; compressed H.264/HEVC references are preserved.

Native fixed-VSync pacing uses completed-flip observations as a best-effort phase estimate and submits with a preparation margin. These observations are not a precise scanout clock. VRR API failure uses the fixed-output timing policy. The HUD distinguishes successful VRR API activation from fixed fallback; API success does not prove physical panel VRR.

PyroWave submits GPU decode/render immediately, waits for preparation completion, then paces presentation. Its two-entry compressed queue preserves brief arrival bursts; it does not claim two prepared GPU surfaces or asynchronous decode/present overlap. GPU fences measure work completion, not physical display completion. Independent stale compressed PyroWave frames may be skipped, while the sole remaining frame is retained.

Per-stream pacing summaries report learned period, reserve, missed readiness targets, re-anchors, wake lateness and submission-spacing error. These counters remain controlled by Diagnostic logs. Compare completed-flip intervals and end-to-end latency, not only submission spacing, during console validation.

The upstream OpenGL launcher log additionally retains the current and previous
launch, bounded by a periodic flush/size check. Disabling logging redirects
stdout/stderr to `/dev/null`; crash reports remain available independently.

### Quit host app after stream

`Quit host app after stream` is off by default. On sends the authenticated
Sunshine cancel request when leaving the stream, stopping the host game/app.
Off keeps the host app running for resume. ProsperoLight stays open in both
cases. Initial setup failures do not trigger this optional cancel request.
The setting persists in `config/prosperolight-host-quit.bin` (PLQ1 + boolean).
The removed local app close setting is not reused.

### Custom FPS and low-rate VRR compensation

PyroWave and native H.264/HEVC use the same VrrRepeatPolicy, with original
source frame numbers so local skips do not look like slower capture. It starts
from the requested FPS, then fits the source cadence. For a fitted period `P`
in integer microseconds, the initial factor is `ceil(P / 16833)`. If that factor
is greater than one, it is increased while `P / factor > 13500` and the next
factor still permits an interval at least as long as the display floor.
The normal 119.88 Hz ceiling gives a floor of 8342 µs. This adds room for a
fresh picture around a repeat deadline; it does not always choose the minimum
factor that reaches approximately 60 Hz.

The table uses exact stable source FPS, `P = floor(1000000 / FPS)`, and a
119.88 Hz-capable variable-output mode. The logged scheduler target is
`min(floor(100000000 * factor / P), floor(100000000 / 8342)) / 100` Hz.
The factor counts all presentations **including** the original picture and
retained copies within its source period; it is not the number of extra copies alone.

| Source FPS | Repeat factor | Scheduler target refresh (Hz) |
| ---: | ---: | ---: |
| 16 (sparse capture) | 5 | 80.00 |
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

119.87 is the integer scheduler target, not a measured HDMI rate; the nominal
mode is 119.88 Hz. Timestamp variation and fitted periods can change the logged
values. Repeated pictures add no new motion information and do not interpolate.

Cadence fitting needs at least eight source intervals and 200 ms of sender
time. The consistency check tolerates quantized capture intervals when original
frame numbers are available; it is not a fixed 45/54 FPS hysteresis band.
Measured periods faster than nominal, or within 5% of nominal, snap to nominal.
A 5–25% slowdown while still near nominal needs at least 750 ms of matching
candidate windows (within 3%); this confirmation is not required for every
subsequent change. A genuinely sparse period greater than 125% of nominal
recovers after three consecutive intervals between 6 ms and 105% of the
nominal period. Recovery fits those intervals and clears the mixed cadence
window; low-rate motion such as 51 FPS keeps its normal integral repetition.

Sparse/static capture is adaptive at every requested FPS. For example, a
confirmed 51 → 16 → 51 FPS source can target 102 → 80 → 102 Hz; its grid is
not pinned to 102 Hz on static content. Transitional windows and deadlines can
produce other rates temporarily. If pictures stop altogether, the last fitted
repeat policy persists because there are no new timestamps to fit. Frequency
changes can cause brightness flicker on some displays; API success and software
VideoOut counts cannot establish physical HDMI timing or absence of flicker.

Fixed Paced requests nominal 60 Hz for requested FPS ≤60 and 120 Hz above 60,
with fractional refresh-slot placement when VSync is active and the rates do
not divide evenly. Unpaced has no cadence timer. Neither uses active VRR's
source-dependent repeat factors; rejected VRR requests use fixed pacing.

See [the repeatable test matrix](PACING_TEST_MATRIX.md) for host stimuli,
automatic FTP collection, boundary cases and hardware-validation limits, and
[measurement semantics](frame-pacing-measurements.md) for deadlines and clocks.
