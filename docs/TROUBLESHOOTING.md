# Troubleshooting

## Linux, WSL, or the native toolchain is missing

On Linux or WSL, run:

```bash
make doctor
```

On Windows PowerShell, run:

```powershell
./tools/doctor.ps1
```

On Linux or inside WSL, confirm the native toolchain:

```bash
test -x /usr/bin/clang-18
test -x /usr/bin/clang++
test -x /usr/bin/curl
test -x /usr/bin/wget
test -x /usr/bin/unzip
test -x /usr/bin/tar
test -x /usr/bin/llvm-ar-18
test -x /usr/bin/llvm-config-18
```

On Ubuntu, install missing host packages with:

```bash
sudo apt install clang-18 clang-format-18 clang-tidy-18 llvm-18-dev cmake curl lld-18 make python3 python3-pip python3-venv tar unzip wget
```

## The generated `libc.prx` is missing or has the wrong hash

Run the source reproducer, which verifies both release digests:

```bash
make libc
```

Normal `make` builds it automatically when absent. Do not replace it with a
module extracted from a game or firmware.

## The linker reports unresolved symbols

Check spelling, C versus C++ linkage, and whether the needed static archive is
listed in `APP_STATIC_ARCHIVES` or the relevant `PACBREW_*` variable. Platform
imports must exist in the public SDK stubs under
`.deps/native/ps5-payload-sdk/target/lib`. Do not silence unresolved symbols;
update the SDK or provide a legitimate native implementation.

## Native dependency bootstrap fails

The first build needs network access to download the hash-pinned public PS5
payload SDK and upstream zlib source archive. Retry:

```bash
make deps
```

The script writes only to `.deps/native/` and never installs packages globally.

## Optional package setup fails

- `.ffpkg` requires Git, the .NET SDK 8 or newer, and network access on first
  use. UFS2Tool and its build output are stored under `.deps/UFS2Tool`.
- `.ffpfsc` requires Git and Python 3.9 or newer with `venv` support. MkPFS and
  its isolated environment are stored under `.deps/MkPFS`.
- Folder output has neither optional dependency. Use `make app` to isolate
  packaging from compilation.

Nothing is installed globally by these optional bootstrappers.

## FTP deployment fails

- Confirm `PS5_HOST` identifies the intended console and its FTP service is
  already running on `FTP_PORT` (default `2121`).
- Run `make deploy PS5_HOST=192.0.2.1 DEPLOY_DRY_RUN=1` to validate the local
  build and destination without sending a network request.
- Fully close the previous title and any crash dialog before replacing its
  files. Never launch while deployment is still running.
- A remaining hidden `.upload` file indicates that a transfer or rename did
  not finish. Rerunning deployment safely overwrites that temporary file.
- Folder deployment intentionally does not delete remote files absent from the
  new build. Manually clean the title directory if a removed asset or module
  must disappear.
- Do not leave both a title folder and an image with the same title ID under
  active scan paths. ShadowMountPlus may prefer its existing mounted source.
- Wait for the mount service to report the title ready before launching. The
  Make target uploads only; it does not launch the app.

## The title does not appear

- Confirm `dist/<TITLE_ID>/sce_sys/param.json` and `icon0.png` exist.
- Confirm another title is not still active in the loader.
- Use a title ID not already registered by another application.
- Wait for the directory loader's explicit ready/installed message.
- Stage the whole title directory, not only `eboot.bin`.

## A reachable Sunshine PC appears offline

- Current builds retry the saved host address automatically. The launcher
  displays `RECONNECTING` after the first two failed checks and only displays
  `OFFLINE` after three consecutive failures. Successful hosts are checked
  every five seconds; recovery checks run every second.
- A Windows login, RDP transition, virtual-display change, or Sunshine restart
  can briefly interrupt its NVHTTP endpoint. Wait for the automatic recovery
  before changing host configuration. Triangle still performs an immediate
  discovery and refresh.
- Stopping an active application retains the last successful application list
  while Sunshine tears down the stream. The Games page should display
  `RECONNECTING`, not temporarily collapse to an empty catalogue.
- Verify the same address answers `http://<host>:47989/serverinfo` from another
  LAN client before changing Sunshine or the PS5.
- PS5 network descriptors must be configured through `libSceNet`. In
  particular, use the platform adapter's `ioctl(FIONBIO)` path; libc `fcntl()`
  handles filesystem descriptors and may reject a network handle before
  `connect()` is attempted.
- The launcher includes the failed TCP stage and numeric error in its refresh
  message. Preserve that text when reporting a failure.

## Launching a stream stays black before the Connecting screen

Normal and release builds keep the optional LAN telemetry sink disabled. Older
development builds tried to open TCP port `8767` on the selected Sunshine host
before decoder setup; a silently filtered connection could block before the
loading renderer and `Select + L1` monitor were available.

Only diagnostics workstations running the telemetry receiver should build with
`LAN_TELEMETRY=1`. Never enable it in a distributed package.

## A resumed 90 or 120 FPS stream is not smooth after changing settings

Choose the resolution, frame rate, codec, and bitrate before launching the
Sunshine application. Returning to ProsperoLight and resuming an application
that is still running can retain timing from the previous host capture/encoder
session. Stop the active application, apply the new settings, and launch it
again so Sunshine creates a fresh session.

## A high-bitrate 4K or HFR stream stutters

Do not assume the highest bitrate will be the smoothest setting. Encoder load,
frame rate, codec, network jitter, and decoder/presentation pressure all vary
by configuration. Start with a moderate bitrate, test motion and input latency,
then increase it incrementally until frame pacing worsens and step back to the
last stable value. Recreate the Sunshine session after changing the setting.

## Pairing says that Sunshine has an active session

Sunshine only permits pairing while its server state is idle. A host can remain
busy after video disconnects because the launched application is still active.
An unpaired client cannot authenticate the HTTPS Stop request. Stop the app from
the Moonlight client that launched it or from Sunshine, then refresh
ProsperoLight and pair. Once paired, ProsperoLight can stop the active app with
Square or the Games-screen action.

## The icon, background, or selection audio does not update

- Run `make`; both the Make and PowerShell builds validate the tracked
  presentation assets before compiling.
- Confirm `icon0.png`, `pic0.dds`, and `pic1.dds` reached
  `dist/<TITLE_ID>/sce_sys/`.
- Selection and launch pictures must be 3840x2160 DX10 DDS files using BC7 UNORM. A PNG
  renamed to `.dds` is not sufficient.
- Audio must be ATRAC9 in a RIFF container named exactly `snd0.at9`; renaming
  MP3 or AAC input does not convert it.
- The RIFF must contain one `smpl` loop. If selecting the app stops default
  home-screen music but remains silent, inspect the chunk list.
- `Base.BgmController: Invalid file size` means Shell rejected the file. The
  observed limit is 2,097,152 bytes (2 MiB), not a fixed duration. At stereo
  192 kb/s, keep input at or below 4,193,024 samples (87.354666667 seconds) so
  frame padding stays below the ceiling.
- Presentation metadata may be cached for an already registered title. Follow
  the loader's documented refresh procedure after structural changes.
- Retail-style custom logos and descriptions are Internet catalog metadata,
  not package assets for a synthetic homebrew concept.

## The app immediately crashes

- Do not return from `main` or call an exit function.
- Keep the generated runtime digest unchanged while testing the baseline.
- Keep the default FSELF magic and SDK pair until the baseline launches.
- Run `tools/inspect.ps1 dist/<TITLE_ID>/eboot.bin` and resolve every error.
- Consult loader diagnostics; the home-screen message alone is not a root
  cause.

## Moving focus crashes the launcher

Symbolicate the fatal instruction and backtrace against `build/llvm-pie.elf`.
RmlUi loads hidden button-state textures lazily when focus changes, so a trap in
`operator new` followed by `SdlRenderInterface::LoadTexture` indicates that the
large-allocation mapping path was lost or failed. The launcher must keep
allocations of 64 KiB or more outside the bounded libc heap.

A startup `SIGBUS` at an aligned AVX store inside
`Rml::ElementInstancerPools::Initialize()` means the replacement `operator new`
returned insufficiently aligned storage. Its allocation header and small-object
backing allocation must preserve the runtime's 32-byte alignment guarantee.

## `/download0` is missing

Keep a positive `downloadDataSize` in `sce_sys/param.json`, rebuild, and stage the
new generated directory. Do not attempt to write to `/app0`.

## Collecting performance metrics through klog

Start a klog capture before testing and leave it connected until after returning
from the stream with Select+L1. ProsperoLight emits its performance summary after
the streaming workers stop, with no per-frame kernel logging during gameplay.
Filter for `[ProsperoLight perf]`. Each record includes a session number and
`part=N/total`; concatenate the text after `json=` in part order to recover the
JSON summary. Session numbers restart when the app restarts. Missing parts mean
the capture is incomplete. Login/reconnection streams produce separate summaries.

The summary includes configuration, decode/presentation timing distributions,
network counters, and audio statistics, but no host identity, credentials, or
typed text. The existing `performance-last.json` remains a local backup; detailed
`performance-frames.csv` stays local to avoid flooding klog. Kernel logging is
best-effort and does not depend on save-file access. Do not share entire save images.

Detailed probe builds also emit `kind=window` JSON records after the summary,
grouped into five-second windows relative to the first frame. Each new record
starts at `part=1`; all share the stream's session number. They include decode
mean/p99/max and over-budget counts, host-reported timing, frame bytes/gaps,
frames displayed and superseded, pending frames, reassembly/queue maxima, and
arrival/flip gaps. These are CPU observations, not GPU timestamps or
input-to-photon latency. The last window may be partial; empty windows are not
emitted. `kind=windows_end` reports recorded/reported/omitted sample counts.
Collection stops at 32,768 frames (about 273 seconds at 120 FPS); logging is
capped at 300 occupied windows.

## The picture shows artifacts or the stream stutters after an update

Open Settings and change **Decoder pipeline** to Classic, then **Decoder CPU
cores** to 3. That is the `01.000.062` decode path. If V-Sync is Off, tearing is
expected; turn it On. Report which setting made the difference together with
`performance-last.json` or the klog summary.
