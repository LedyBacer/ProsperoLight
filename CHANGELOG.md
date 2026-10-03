# Changelog

## 01.000.083

### Development build: faster start, no stutter on tabs, crash report

Not released. Everything from 01.000.082 below is included.

- **Faster start from the second launch on.** The launcher keeps the shaders
  its graphics compile in `/data/prosperolight/cache`. The first try at this
  stopped the app at its first shader: the cache writes through `mkstemp`,
  which the OpenGL runtime takes from a system library the app does not load.
  The app now has its own.
- **New version notice.** Once per launch the app asks the homebrew.page
  catalog whether a newer ProsperoLight is listed, and says so in a notice
  that stays for ten seconds. It uses the update check of
  ps5-native-app-boilerplate with libcurl; nothing is downloaded or
  installed, and no network or no listing means no notice.
- **Crash report.** A fault writes `logs/crash-last.txt` and the same text into
  the log; `tools/symbolize-crash.py` names the functions. Stopping and
  unpairing are written to the log too.
- **Stopping an app no longer closes ProsperoLight** (expected; to be confirmed
  on the console). Stopping and unpairing run on the launcher's worker thread
  since 01.000.080, and nest a refresh inside a TLS request: the thread now has
  a 1 MiB stack instead of the default.
- **No more one-second freezes, and a faster start.** Each write to the log
  file under `/data` takes tens of milliseconds, and the OpenGL runtime writes
  some forty lines of statistics every ten thousand draws (about every nine
  seconds in the launcher), plus a few lines per shader at start. The screen
  waited for all of them. The log is now buffered in memory and written by a
  thread of its own, five times a second. The screen's thread also has a core of its
  own, and requests to Sunshine run on the other CPUs.
- The log notes any launcher frame that takes longer than 50 ms, with the time
  each part of it took, and how long each graphics program took to build.

### What was tested

On the PC: the build, the tests and the launcher render check. On the console:
see the notes for this build.

## 01.000.082

### Development build: one connecting screen, and no black start

Not released. Everything from 01.000.081 below is included.

- **The connecting screen stays until the stream is there.** It used to stop
  at a fixed point when the launcher handed the display to the stream. Now the
  stream keeps the same screen on the television and moves the bar on with
  each step: display ready, decoder ready, Sunshine has started the app,
  connected. When the first picture arrives the bar runs to its end, the
  screen fades out, and the stream appears. Touchpad + L1 still cancels.
- **No black screen when the app opens.** The console's own splash picture
  now stays until the launcher has drawn its first frame; before, it was
  taken away about four seconds too early.
- The log has a time for each step of opening the launcher.

### What was tested

On the PC only: the picture handed to the stream, the bar and the fade are
drawn and checked by the launcher render check (three `stream-*.png`
pictures) and by unit tests. Not yet seen on a console: the hand-over itself,
and the splash picture staying up.

## 01.000.081

### Development build: files under `/data/prosperolight`, and an About page

Not released, and not yet run on a console. Everything from 01.000.080 below
is included.

- **One folder for everything the app writes.** When it starts, ProsperoLight
  asks the console's ELF loader for access to the filesystem, the way
  ProsperoEden does. With it, saved PCs and settings are in
  `/data/prosperolight/config`, the pairing in `/data/prosperolight/pairing`,
  and the log and the last stream's performance report in
  `/data/prosperolight/logs`. An update or a reinstall does not touch them.
- **Nothing to pair again.** The first start copies the saved PCs, the settings
  and the pairing from the title's own storage. The older copies stay there.
- **Still works without it.** With no ELF loader on port 9021, or if the request
  is refused, the app uses `/app0` and `/download0` as before. The first line
  of the log says which it is.
- The app folder has one more file, `sandbox-elevator.elf`: the helper the ELF
  loader runs. It only answers title `PPSA99002`.
- **One connecting screen.** After the launcher's connecting screen, the old
  dark screen with the circling dots is gone. The picture stays black for the
  moment it takes the television to change mode, then the stream appears.
- **About.** A fourth tab, as in ProsperoEden: credits for Moonlight and
  Sunshine, thanks, who made the PS5 edition, the version, three steps to get
  started, and the folders the settings, the pairing and the logs are in.

### What was tested

On the PC only: the build, the checks of the helper's file, and the tests.
What a console run must still show: access is given (`status=0` in the log),
the launcher finds its fonts and sounds in the install folder, the saved PCs
and the pairing come over, and a stream still starts and returns with the
process outside its sandbox.

## 01.000.080

### Development build: a new launcher

Not released, and not yet run on a console. Everything from 01.000.071 below
is included.

- **New look.** The launcher is drawn by the GPU with the widgets of
  ps5-homebrew-ui, at up to 4K, in ProsperoLight's own Moonlight theme: frosted
  panels over a night-blue sky. Tabs in the header replace the sidebar.
- **PCs.** Every saved PC is on screen with its state: ready, needs pairing, or
  offline. The panel beside the list shows the selected PC's address, port,
  pairing, app count and running app. **Remove** must be held; **Unpair** asks
  first. The Refresh button is gone: Triangle searches the network again.
- **Games.** Box art is shown as posters, seven to a row, and the list scrolls.
  The selected app's name is the headline and the page takes its colours from
  the poster. The running app carries a badge.
- **Settings.** Rows are grouped, left and right change a value, and bitrate is
  a slider from 10 to 300 Mbps. At 4K a meter shows the bitrate against the
  decoder's measured limit and a warning appears above it. The stream shortcuts
  are listed with the buttons' shapes.
- **Sounds.** Every widget has its sound: moving, choosing, switching, sliders,
  dialogs, notices, and starting a stream.
- **Address and port** are typed on a number pad inside the launcher. The PS5
  keyboard is no longer used.
- The screen keeps moving while a PC is slow to answer: requests to Sunshine
  run on their own thread.
- Notices appear as toasts, and buttons are shown as glyphs, not named.
- After a stream above 60 Hz or in HDR, the screen stays black for five seconds
  before the launcher returns, while the television changes mode.
- SDL2, RmlUi and FreeType are no longer part of the app. The launcher uses the
  ps5-opengl SDK; the app file grows from 5 MB to 25 MB.
- PC and app names can use western and central European letters and Cyrillic.
  Other scripts are not drawn yet.

### What was tested

On the PC only: the launcher's logic and screens run against a pretend Sunshine
network (first start, three PCs, pairing, port and address entry, removing,
stopping an app, starting a stream, settings, a PC that does not answer), every
state is rendered to a picture, and every sound a widget asks for has a
recording. The console build links and imports nothing that an earlier console
build did not. The hand-over between the launcher and the stream on one display
has not run on a console.

## 01.000.071

### Development build — multiple controllers and a Sunshine port per PC

Not released. It adds two features to experimental beta **01.000.070**; the
01.000.070 notes below still apply.

- **Up to four controllers.** Every user signed in on the PS5 plays with their
  own controller. The user who started ProsperoLight is the first controller on
  the PC. Turn on another controller, choose a user for it when the PS5 asks,
  and it joins as the next one, before or during a stream. A controller that is
  switched off, or whose user signs out, is removed from the PC.
- `Touchpad + L1` (leave the stream) and `Touchpad + R1` (statistics) work on
  every controller. Mouse mode and the stream keyboard stay with the first one.
- **The stream shortcuts are named after their button.** They were written
  `Select + ...`; the button is the touchpad, pressed down until it clicks, so
  they are now `Touchpad + L1`, `Touchpad + R1`, `Touchpad + Square` and
  `Touchpad + Triangle`. The shortcuts themselves did not change. Settings, the
  launcher's footer and the README show the buttons as icons.
- **Sunshine port per PC.** The PCs page has a **Port** button for the selected
  PC, and **Add PC** accepts `address:port`. Use it when Sunshine's **Port**
  setting is not 47989. One address can be saved with several ports.
- A PC found on the network uses the port Sunshine advertises.
- Saved PCs and settings from earlier versions are kept. Going back to an
  earlier version resets them; pairing is kept.

### What was tested

Host tests only: controller joining, leaving, numbering and shortcuts against a
simulated PS5; saved-PC migration, address and port entry, and discovery
against simulated replies and one reply from a real Sunshine. Neither feature
has run on a console yet.

## 01.000.070

### Experimental performance beta — new decode and presentation threads

This is a **prerelease**, not a replacement for stable **01.000.060**. It
supersedes beta 01.000.062.

- Decoding and presentation now run on separate threads. Decoding no longer
  waits for a flip, and every display refresh shows the newest decoded frame.
- A stream that outruns the decoder keeps playing at the decoder's pace. The
  rule that showed one frame every 100 ms once decoding fell behind is removed.
- Stream threads are kept off the CPUs reserved for the decoder, and the video
  receive thread has a CPU of its own.
- The flip wait uses flip events instead of polling or waiting for a vblank.
- The overlay separates network, decoded and displayed frame rates, and frames
  lost on the network from frames discarded because decoding fell behind.
  Decode time covers the last second (mean, 95th percentile and load).
- New settings: **V-Sync** (Off flips immediately and tears), **Decoder
  pipeline** (Classic by default; Adaptive is experimental) and **Decoder CPU
  cores** (3 by default; 4 and 5 are experimental).
- Bitrate presets gain 50, 60 and 70 Mbps.
- A failed stream now says whether the connection was lost while streaming or
  never completed, with the stage and the error code.
- moonlight-common-c is updated, and the performance summary is also written
  to klog.

### Know the bitrate limit

The PS5 decoder, not the network, limits the usable bitrate, and this beta
does not raise that limit. Measured at 4K HEVC: smooth up to 80 Mbps at
120 FPS and about 115 Mbps at 90 or 60 FPS; above 100 Mbps at 120 FPS and
145 Mbps at 90 FPS the stream freezes about once a second. The table and chart
are under [Bitrate limits](https://github.com/blackbearreloaded/ProsperoLight#bitrate-limits).

### What was tested

Run on one PS5 with one NVENC host at 4K, 120 FPS, HEVC SDR, with both decoder
pipelines. 1440p, 1080p, 60 and 90 FPS, HDR, H.264 and V-Sync Off use the same
code but were not re-run on a console for this beta. The Adaptive pipeline had
one defect fixed after its last console run; leave it on Classic unless you
want to help test it.

### Please report your results

Use [GitHub issues](https://github.com/blackbearreloaded/ProsperoLight/issues) and include:

- PS5 firmware; Sunshine/Vibepollo version; host GPU and driver.
- Codec/HDR, resolution/FPS, bitrate, stereo/5.1, Ethernet or Wi-Fi.
- The three new settings, and whether this beta is smoother, unchanged, or
  worse than `.062` or `.060` in the same game.
- With the overlay on (Touchpad + R1): the "Decoder", "Decode (last second)" and
  both "Frames dropped" lines.
- If available, attach `performance-last.json` and `performance-frames.csv` from
  the app's `/download0/moonlight/` save directory. These are overwritten by the
  next stream; the console may expose them inside the title's UFS2 `download0.dat`.
  **Do not upload the entire save image**: it may contain pairing credentials.

### Installation, update and rollback

Title ID stays **PPSA99002**. Choose `PPSA99002.ffpfsc`, or extract
`PPSA99002.zip` and copy its `PPSA99002/` folder to `/data/homebrew`.
Do not keep both formats installed. Close the app before replacing files, then
restart ShadowMountPlus or the console. `SHA256SUMS` covers both downloads.
Existing pairing and settings are retained. To roll back, replace the beta with
the corresponding `.062` or `.060` download and restart ShadowMountPlus or the
console.

Local reproduction:
`make ffpfsc FEC_SIMD=1 OPUS_SIMD=1 PERFORMANCE_DETAIL=1 FLIP_POLL_US=200 INPUT_POLL_US=2000 VIDEO_SLICES_PER_FRAME=8`.

## 01.000.062

### Experimental performance beta — help test 4K120

This is a **prerelease**, not a replacement for stable **01.000.060**.
Improved decode headroom; remaining 4K120 stuttering is under investigation.

- Requests eight slices per frame for increased decoder parallelism. Local HEVC
  tests confirmed eight slices and lower decode times, including comparable
  compressed-frame sizes. Gameplay windows included Windows/RDP login transitions:
  these are promising observations, not a controlled percentage improvement.
- Shortens input polling to a 2-ms target and accounts for polling work time.
  This does not establish a measured input-to-photon latency reduction.
- Enables CPU-feature-checked FEC SIMD and Opus SIMD, with 200-us flip polling.
- Reduces repeated GPU cache-flush work and avoids heap allocation for common
  socket polls. Preserves bounded presentation overlap and source ownership.
- Includes local performance summaries and bounded per-frame timing records for
  diagnosis. No streamed images, typed text, or automatic remote telemetry.
- Includes PC removal/re-pairing improvements for changing host software.

Decoder depth stays **one**, worker affinity/priority remain unchanged, every
decoded picture remains eligible for presentation, and experimental pacing and
audio-backlog dropping are **off**. No host configuration is modified. Eight
slices may behave differently with other encoders/codecs; report regressions.
Higher bitrate is not higher FPS, and sustained 120 FPS is not guaranteed.

### Please report your results

Use [GitHub issues](https://github.com/blackbearreloaded/ProsperoLight/issues) and include:

- PS5 firmware; Sunshine/Vibepollo version; host GPU and driver.
- Codec/HDR, resolution/FPS, bitrate, stereo/5.1, Ethernet or Wi-Fi.
- Game and whether this beta is smoother, unchanged, or worse than `.060`.
- Complete Windows/RDP login first, reconnect, then warm up for 30 seconds and
  play the same area for two minutes. Return with Touchpad + L1 and close the app.
  Separate login/menu/teardown time from gameplay. Stop early if unusable.
- If available, attach `performance-last.json` and `performance-frames.csv` from
  the app's `/download0/moonlight/` save directory. These are overwritten by the
  next stream; the console may expose them inside the title's UFS2 `download0.dat`.
  **Do not upload the entire save image**: it may contain pairing credentials.

### Installation, update and rollback

Title ID stays **PPSA99002**. Choose `PPSA99002.ffpfsc`, or extract
`PPSA99002.zip` and copy its `PPSA99002/` folder to `/data/homebrew`.
Do not keep both formats installed. Close the app before replacing files, then
restart ShadowMountPlus or the console. `SHA256SUMS` covers both downloads.
Existing pairing/settings are retained. To roll back, replace the beta with
the corresponding `.060` download and restart ShadowMountPlus or the console.

Local reproduction (ordinary `make` retains conservative development defaults):
`make ffpfsc FEC_SIMD=1 OPUS_SIMD=1 PERFORMANCE_DETAIL=1 FLIP_POLL_US=200 INPUT_POLL_US=2000 VIDEO_SLICES_PER_FRAME=8`.

## 01.000.060

### Smoother 4K120 streaming

- Enabled bounded decode/presentation overlap: the next picture can decode while
  the previous native flip completes, with at most one outstanding submission.
  Source surfaces and shared GPU memory remain protected until completion.
- Improved stale-frame handling, short-window FPS metrics, and fractional-refresh
  negotiation (for example, a 120 FPS request on 119.88 Hz output).
- Added a small local post-session performance summary and regression checks for
  presentation ownership, timeout handling, and incomplete report writes. No
  remote telemetry or streamed content is collected by this summary.

### 4K120 before / after

One paired Hollow Knight test on PS5 firmware 6.02, using wired Ethernet,
3840×2160 at a requested 120 FPS, HEVC SDR, 80 Mbps, stereo, and 119.88 Hz output:

| Measurement | Before: overlap off | After: overlap on |
| --- | ---: | ---: |
| Decoded / presented frames | 16,769 / 12,686 | 14,058 / 14,058 |
| Stale presentation skips | 4,083 (24.3%) | 0 (0%) |
| Mean enqueue-to-decoder-callback wait | 5.468 ms | 0.016 ms |
| Sampled pending video queue peak | 5 frames | 0 frames |
| Mean decoder-call time | 6.869 ms | 6.934 ms |
| p99 CPU-observed completion interval | 75.499 ms | 31.499 ms |

The tester reported **lots of stuttering before** and **much smoother playback
after**. Neither run reported network frame gaps or audio errors. The improvement
is in queueing and presentation, not faster hardware decoding.

These are whole-session measurements from one comparison, including startup and
menus, with different session lengths. The baseline was instrumented with overlap
disabled, not the exact public `01.000.055` binary. This is not a guarantee of
locked 120 FPS, lower input-to-photon latency, or a fix for every high-bitrate/Wi-Fi
setup. CPU-observed frame age increased from 12.560 to 16.854 ms; overlap changes
when completion is observed, and the baseline excludes skipped pictures from
that metric. It cannot establish an end-to-end latency change.

See the [Round 2 measurements and methodology](https://github.com/blackbearreloaded/ProsperoLight/blob/01.000.060/docs/PERFORMANCE_ROUND_2.md#release-checkpoint--01000060).
Further performance work is deferred to the next version: FEC SIMD, Opus SIMD,
audio catch-up, and shorter flip polling are **not enabled** in this release.

### Installation / update

Title ID remains `PPSA99002`; existing pairing and preferences are preserved.
Choose `PPSA99002.ffpfsc` or extract `PPSA99002.zip` and copy its `PPSA99002/`
folder to `/data/homebrew`. Do not keep both formats installed for the same
title. Close the app before replacing its files, then restart ShadowMountPlus
or the console. `SHA256SUMS` covers both downloads.

## 01.000.055

- Added a persistent Stereo / 5.1 surround setting.
- Added Moonlight-compatible 5.1 Opus negotiation and PS5 eight-channel AudioOut,
  with the unused side channels silenced and automatic stereo fallback when the
  surround port is unavailable.
- Compacted the Settings rows so all seven controls and the shortcut note remain
  visible above the footer.
- Replaced the temporary demo app cards with a clear loading state while the
  Sunshine application catalog refreshes.
- Unified the mbedTLS structure configuration across the C and C++ stream code,
  preventing identity initialization from overwriting the live PS5 pad state.

## 01.000.050

- Added independently selectable 90 and 120 FPS streaming at 1080p, 1440p,
  and 2160p, with native 4K/119.88 Hz output validated on PS5 hardware.
- Preserved native 4K presentation for high-resolution HFR streams and restored
  the launcher output cleanly when a stream ends.
- Added an application-side startup transition for TVs that resynchronize when
  an HFR-capable title opens.
- Moved Sunshine discovery behind the first rendered launcher frame for faster
  visible startup.
- Documented that HFR setting changes should be applied before launching, or
  followed by stopping and relaunching the active Sunshine application.
- Clarified that wired Ethernet is recommended for high-resolution and HFR
  streaming.
- Added guidance to tune bitrate per configuration because maximum bitrate can
  reduce smoothness at 4K or high frame rates.
