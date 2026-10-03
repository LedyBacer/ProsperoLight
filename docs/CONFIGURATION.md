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
| `DEPLOY_FORMAT` | `folder` | `folder`, `ffpfsc`, or `ffpkg` output. |
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

VRR is a request, not proof that the display accepted it. A rejected request
falls back to fixed output and is recorded in the session log. Native
immediate flips can also fall back to VSync if the system rejects them.
The launcher requests fixed 60 Hz when its own VideoOut handle opens.

Menu sounds and the presentation policy are stored separately from the paired
host configuration in `prosperolight-ui-sound.bin` and
`prosperolight-presentation.bin` under `/download0`.

The scrolling list contains settings only; two permanent lines below it show
local touchpad-click combinations and the host Select/Back and PS/Guide
shortcuts. These lines use the loaded 20 px bitmap font.

Current hardware evidence: H.264 4K60 completed Unpaced, Paced, and
Paced + VRR selected-policy sessions with zero decoded frames discarded
before presentation. Those measurements preceded the VRR initialization-order
fix and do not establish active VRR. Confirmation after that fix, PyroWave
pacing comparisons, and HEVC HDR regression checks are pending. H.264 4K120
recorded decoder timing spikes and input-queue overflows even with Unpaced;
these limits must not be represented as eliminated by pacing.

### Manual frame-rate entry

Select Stream frame rate and press Cross to open the same system keyboard used
for bitrate. Enter any integer from 30 to 120 FPS. Cancel, invalid input and a
failed save retain the previous value. The saved target is passed unchanged to
H.264, HEVC or PyroWave and their pacing policy. No configuration-format change
is required. This is a stream target, not a new fixed HDMI mode: VideoOut uses
its fixed60/120Hz path or requests VRR. Arbitrary targets such as75FPS can have
uneven display intervals with fixed-refresh VSync; sustained decoding capacity
and active VRR still require console verification.

### Diagnostic logs

`Diagnostic logs` enables or disables application diagnostic files, LAN diagnostic telemetry, and end-of-stream performance exports. It defaults to On and is stored independently beside the main config (`config/prosperolight-logging.bin` under `/data/prosperolight`, or `/download0/prosperolight-logging.bin` in the sandbox). Disabling keeps existing files for inspection; it does not erase them or disable the statistics overlay.

Session, PyroWave and menu-output text logs rotate at 1 MiB each, retaining one `.previous` file per log (up to 6 MiB for those three logs). Rotation is size-based and can occur within a long stream; files are not reset on every connection. Legacy oversized current logs are discarded on their next write. Performance JSON/CSV files replace the previous report rather than accumulating sessions. System/etaHEN logs are outside this switch.

### Source-clock frame pacing

Unpaced keeps the latest-frame policy without an extra software wait. Paced and Paced+VRR share a source-clock controller, learn the source interval from validated RTP timestamps (presentation timestamps are the fallback), and carry a small bounded readiness reserve (up to the smaller of 10 ms or one source interval). The ready queue holds at most two decoded native images. Old decoded images are discarded only when a decoded successor exists; compressed H.264/HEVC references are preserved.

Native fixed-VSync pacing uses completed-flip observations as a best-effort phase estimate and submits with a preparation margin. These observations are not a precise scanout clock. VRR API failure uses the fixed-output timing policy. The HUD distinguishes successful VRR API activation from fixed fallback; API success does not prove physical panel VRR.

PyroWave submits GPU decode/render immediately, waits for preparation completion, then paces presentation. Its two-entry compressed queue preserves brief arrival bursts; it does not claim two prepared GPU surfaces or asynchronous decode/present overlap. GPU fences measure work completion, not physical display completion. Independent stale compressed PyroWave frames may be skipped, while the sole remaining frame is retained.

Per-stream pacing summaries report learned period, reserve, missed readiness targets, re-anchors, wake lateness and submission-spacing error. These counters remain controlled by Diagnostic logs. Compare completed-flip intervals and end-to-end latency, not only submission spacing, during console validation.

The upstream OpenGL launcher log additionally retains the current and previous
launch, bounded by a periodic flush/size check. Disabling logging redirects
stdout/stderr to `/dev/null`; crash reports remain available independently.

### Automatic app close

`Close app after stream` is off by default. When enabled, ProsperoLight requests
a clean PS5 title shutdown after leaving/ending a stream, after streaming
resources are released. It does not stop the game on the host. Initial
connection failures return to the launcher for recovery. The setting persists
in `config/prosperolight-auto-close.bin` (PLE1 + boolean).
