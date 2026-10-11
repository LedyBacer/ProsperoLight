<p align="center">
  <img src="sce_sys/icon0.png" width="128" alt="ProsperoLight icon">
</p>

<h1 align="center">ProsperoLight</h1>

> **Beta: [01.000.090](https://github.com/blackbearreloaded/ProsperoLight/releases/tag/01.000.090).**
> Updates from inside the app, 31 languages, the PyroWave codec, more DualSense features,
> frame pacing, a custom frame rate and settings for each PC. The PS5 decoder still limits
> how much bitrate is usable with H.264 and HEVC: see [Bitrate limits](#bitrate-limits).
> [01.000.060 remains stable](https://github.com/blackbearreloaded/ProsperoLight/releases/tag/01.000.060).
> Please report results and regressions through [GitHub issues](https://github.com/blackbearreloaded/ProsperoLight/issues), using the checklist in the beta release notes.

<p align="center">
  <strong>A native Moonlight client for PlayStation 5 homebrew</strong><br>
  Stream Sunshine applications with hardware video decoding, low-latency input,
  selectable stereo or 5.1 surround audio, and a controller-first interface.
</p>

<p align="center">
  <img src="https://img.shields.io/badge/platform-PlayStation%205-003791?logo=playstation&amp;logoColor=white" alt="PlayStation 5">
  <img src="https://img.shields.io/badge/video-H.264%20%7C%20HEVC%20%7C%20PyroWave-70E1DC" alt="H.264, HEVC and PyroWave">
  <img src="https://img.shields.io/badge/frame%20rate-30–120%20FPS-5BBEFF" alt="30 through 120 FPS">
  <img src="https://img.shields.io/badge/audio-Stereo%20%7C%205.1%20Opus-7DD3FC" alt="Stereo and 5.1 Opus audio">
  <img src="https://img.shields.io/badge/UI-OpenGL-5DDFA4" alt="OpenGL launcher">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-GPL--3.0--or--later-blue" alt="GPL-3.0-or-later"></a>
</p>

![ProsperoLight's PCs screen showing a paired Sunshine host](docs/images/prosperolight.png)

The picture shows the launcher running on a PlayStation 5 with a paired Sunshine host.

## Highlights

- Native PS5 hardware streaming through VideoDec2 and AGC at 1080p, 1440p,
  and 2160p, with independently selectable 30, 60, 90, and 120 FPS stream targets, plus Custom (30–120 FPS in steps of 1).
- Decoding and presentation on separate threads: a late flip never holds back
  decoding, and every display refresh shows the newest decoded frame. See the
  [measured bitrate limits](#bitrate-limits) before raising the bitrate.
- H.264 High, HEVC Main, and HEVC Main10 HDR10 support at every available
  resolution.
- Low-latency DualSense input for up to four controllers, physical USB keyboard
  and mouse, controller-driven mouse mode, and an on-screen password keyboard.
- Automatic Sunshine discovery, manual address and port entry, persistent
  multi-PC pairing, application artwork, and launch/resume/stop controls.
- Persistent stream preferences, edge-to-edge or TV-safe presentation,
  independent frame-rate selection, and a bitrate slider (to 300 Mbps, to 1000 Mbps with PyroWave).
- An interface in 31 languages that follows the console's language, updates
  installed from inside the app, and host settings kept for each PC.
- Selectable 48 kHz stereo or 5.1 surround Opus audio, a sound for every
  launcher widget, live performance metrics, and graceful connection recovery.

ProsperoLight is a native PS5 client for the open Moonlight/Sunshine streaming
protocol. Its OpenGL launcher discovers and pairs with Sunshine hosts, browses
their applications, and starts a native streaming session. Video access units
are decoded by PS5 VideoDec2 and the resulting GPU-visible surfaces are
presented by AGC without copying decoded pixels through a CPU framebuffer.

> [!IMPORTANT]
> ProsperoLight does not run on an unmodified retail console. It is intended
> for consoles you own with an already configured, compatible homebrew loader.
> This repository does not include an exploit, proprietary Sony SDK, system
> module, encryption key, firmware file, or game asset.

## Features

- Discover Sunshine hosts on the LAN or add an IPv4 address manually.
- Reach a Sunshine that does not use the default port 47989: each saved PC has
  its own port, and a discovered PC uses the port it advertises.
- Remember up to eight PCs, pairing identities, and stream preferences under
  `/data/prosperolight` across application restarts and updates.
- Pair with a two-minute PIN dialog with a countdown, and unpair after a
  confirmation.
- Update itself: when a newer version is listed on
  [homebrew.page](https://homebrew.page), a dialog offers it once per launch and
  **Update now** installs it. See [Update an installed copy](#update-an-installed-copy).
- See every saved PC and its state in one list, and browse the PC's whole
  Sunshine library as posters with launch/resume and stop controls.
- Decode H.264 High and HEVC Main streams through VideoDec2 at 1080p, 1440p,
  and 2160p, with independently selectable 30, 60, 90, and 120 FPS stream targets
  or any custom rate between;
  live 1440p/90 and 2160p/120 Sunshine sessions are hardware-validated.
- Present decoded GPU surfaces directly through AGC, with edge-to-edge and
  television-safe display modes. At 60 FPS, 1440p is GPU-scaled into a
  3840x2160 target and 2160p is presented 1:1. High-refresh streams retain the
  same 4K output geometry: 1440p is GPU-scaled to 4K, while 2160p/120 is
  presented through the PS5's native 3840x2160 119.88 Hz output path.
- Set the bitrate from 10 to 300 Mbps on a slider (to 1000 Mbps with PyroWave). For native HEVC at 4K the Settings screen
  shows how close the bitrate is to the decoder's measured limit and warns
  above it.
- Choose V-Sync, the decoder pipeline (Classic, or the experimental Adaptive),
  and how many CPU cores decoding may use.
- Enable HEVC Main10 HDR10 output at any available resolution and frame-rate
  selection when the Sunshine host advertises support.
- Decode Moonlight Opus audio and output selectable 48 kHz stereo or 5.1
  surround through PS5 AudioOut.
- Forward low-latency DualSense controls, with controller/mouse switching and
  a stream keyboard that works at Windows sign-in.
- Play with up to four controllers: every user signed in on the PS5 is a
  separate controller on the PC, and controllers can join or leave mid-stream.
- Forward a physical USB keyboard and mouse directly to Sunshine during a
  stream, including modifiers, navigation/function keys, five mouse buttons,
  and vertical/horizontal scrolling.
- Show Moonlight-style stream metrics for resolution, codec, frame rates,
  packet loss, network/host latency, and decode time.
- Recover from connection failures and return from a stream to the launcher
  without leaving a stale session running.
- Read the launcher in 31 languages: it follows the console's language, or the
  one chosen in Settings. See [Interface language](#interface-language).
- Keep host settings for each PC: muting its speakers, game optimisation,
  quitting the app after the stream, and optional Vibepollo requests. See
  [Per-PC host settings](#per-pc-host-settings).
- Use original 4K artwork, icon, loading presentation, and selection music.
- Use a launcher drawn by the GPU at up to 4K: frosted panels, the selected
  app's colours behind them, button glyphs in place of button names, and a
  sound for every move, choice, switch, dialog and notice.

## Current status

The complete 1080p60 path—pairing, application launch, VideoDec2/AGC video,
stereo audio, DualSense input, mouse mode, Windows sign-in keyboard, metrics,
return-to-launcher, relaunch, and cleanup—has been exercised on PS5 hardware
with Sunshine. Hardware runs also confirm HEVC 1440p-to-4K filtered GPU
presentation and native HEVC 2160p-to-4K presentation with the metrics HUD at
60 FPS. Bounded live sessions additionally confirm 2560x1440 at 90 FPS and
3840x2160 at 120 FPS from Sunshine. An isolated hardware oracle additionally
confirms native 3840x2160 119.88 Hz VideoOut presentation at 119.88 FPS.

ProsperoLight is still alpha software. HEVC, 1440p, 2160p, HDR, very high
bitrates, network recovery, and long gameplay sessions need broader validation
across GPUs, Sunshine configurations, networks, TVs, firmware versions, and
homebrew loaders. See the evidence and open acceptance items in
[Validation](docs/VALIDATION.md).

> [!IMPORTANT]
> Wired Ethernet is strongly recommended for reliable streaming, particularly
> at 1440p or 2160p, 90 or 120 FPS, and higher bitrates. Wi-Fi can work, but
> interference and variable latency may cause dropped frames or input delay.

## Roadmap

The aim is a client as capable as Moonlight on a PC, and better where the PS5 allows it.
These are directions, not promises or dates: each one still has to be proven on hardware.
Ideas and votes are welcome in
[GitHub issues](https://github.com/blackbearreloaded/ProsperoLight/issues).

### Next

- **Quick menu during a stream.** Send Alt+Tab, Win, Esc and Alt+F4, switch between
  controller and mouse, change monitor, and quit the app on the PC, without a keyboard.
- **Home-screen tiles for PC games.** Start ProsperoLight with a PC and an app named, so
  a forwarder tile on the PS5 home screen opens one game directly
  ([#26](https://github.com/blackbearreloaded/ProsperoLight/issues/26)).
- **Resume the last session** as the first choice when the app opens.
- **Wake a sleeping PC** from the PCs page (Wake-on-LAN).
- **Automatic bitrate.** A short network test picks it, with the decoder's measured
  limit as the ceiling.
- **Seamless reconnect.** After a network drop, keep the last picture and resume the
  stream instead of returning to the launcher.

### Picture

- **Upscaling on the console.** Stream at 1080p or 1440p and upscale to 4K on the PS5:
  less bitrate and less load on the PC for a similar picture.
- **VRR without choosing a mode.** Use variable refresh whenever the television offers it,
  and tell from the system when it is really active.
- **HDR matched to the television.** Give the PC the display's calibrated HDR values so
  games tone-map for the screen in the room.
- **Latency you can read.** An end-to-end estimate in the statistics overlay.

### Controllers and sound

- **Microphone to the PC**, from the DualSense or a headset, for voice chat (where the
  host supports it).
- **Gyro aiming**, and the touchpad as a trackpad with two-finger scrolling.
- **Controller speaker and light bar** driven by the game.
- **Button remapping and profiles** for each game.
- **7.1 surround**, beside stereo and 5.1.

### Library and settings

- **Settings for each game:** resolution, frame rate, bitrate and pacing remembered per
  app.
- **Hide, reorder and search apps**, with favourites first.
- **Artwork and names** from a public database when the PC has none.
- **Every PC at a glance:** which are awake and what they are running.

### Reach

- **Streaming over the internet:** an external address and port, IPv6, and a connection
  test that says what is blocked.
- **Host extras for Vibepollo and Apollo:** a virtual display matched to the television,
  and HDR and VRR set up automatically.
- **Reviewed translations.** A simple way for native speakers to correct the 31
  languages.
- **Tested across system software versions** before each release, since consoles on
  different versions do not always behave alike.

## Requirements

Build from Linux, WSL, or a Linux CI runner. On Ubuntu, Debian, or WSL:

```bash
sudo apt update
sudo apt install curl git make pkg-config python3 python3-venv tar unzip wget \
  clang-18 clang-format-18 clang-tidy-18 llvm-18-dev lld-18 cmake
```

The build downloads and verifies its public PS5 Payload SDK, zlib, GoogleTest,
and packaging inputs below ignored `.deps/` directories. Initialize the pinned
streaming sources after cloning:

```bash
git submodule update --init --recursive
make doctor
```

Nothing is installed globally by the project build.

See [Getting started](docs/GETTING_STARTED.md) and
[Native tooling](docs/NATIVE_TOOLING.md) for clean-machine setup details.

## Update an installed copy

### From the app

Once each time it opens, ProsperoLight asks [homebrew.page](https://homebrew.page/ps5)
which release of it is listed there. The request carries the app's title ID and nothing
else. If the listed release is newer than the one running, a dialog offers it, with its
version and download size:

- **Update now** downloads the release ZIP from its GitHub release, checks it against the
  SHA-256 in the catalog's signed list, and unpacks it beside the app. A ring shows how far
  it is and the time left; ![Circle][circle] cancels, and nothing has changed until the end.
  Then ProsperoLight closes, the update helper (`self-updater.elf`, sent to the console's
  payload loader on port 9021) replaces the app's files, and the console shows a
  notification. Open ProsperoLight again to use the new version.
- **What's new** (when the release has notes) shows what changes in it before you decide:
  the release notes as homebrew.page lists them. Up and down scroll, ![Cross][cross]
  updates and ![Circle][circle] goes back to the offer.
- **Skip** keeps the version you have. The dialog shows again the next time ProsperoLight
  opens, not when you come back from a stream.

If the download or the unpacking fails, the dialog says why and offers **Try again**; the
app stays as it was. Without a network, or without an answer, nothing is shown. When
ProsperoLight cannot install the release itself (it is not a ZIP on GitHub), a notice says
**Update available** for ten seconds instead, and the steps below still work. The app must
be installed as a folder (`/data/homebrew/PPSA99002`); a copy installed as an image cannot
replace itself, and the dialog says so.

### By hand

1. Download `PPSA99002.zip` from the latest GitHub release and verify it with
   `SHA256SUMS`. A release ZIP built by GitHub Actions can
   be checked with the GitHub CLI:
   `gh attestation verify PPSA99002.zip -R blackbearreloaded/ProsperoLight`
   (releases built from now on, not earlier ones).
2. Fully close ProsperoLight.
3. Extract `PPSA99002.zip` and upload its complete `PPSA99002` directory to
   `/data/homebrew/`, producing `/data/homebrew/PPSA99002/eboot.bin`. Do not
   upload the ZIP itself. No image is built any more: if a `PPSA99002.exfat` or
   `PPSA99002.ffpfsc` from an older version is still in a directory scanned by
   ShadowMountPlus, delete it.
4. Restart ShadowMountPlus cleanly or restart the PS5, then wait for
   ShadowMountPlus to rediscover the title before launching it.
5. Launch ProsperoLight and confirm the new version below the title in the top
   bar.

Do not relaunch immediately after replacing the same pathname: ShadowMountPlus
may still have the previous image mounted. Pairing and settings are kept in
`/data/prosperolight` (see [Files on the console](#files-on-the-console)) and
are not touched by an update; Shell presentation metadata may remain cached.

## Pairing and first stream

1. Start Sunshine on a PC connected to the same trusted LAN. Wired Ethernet
   for both the PC and PS5 is strongly recommended.
2. Open ProsperoLight and choose a discovered PC, or choose **Add a PC** and
   type its IPv4 address (see [Sunshine on another port](#sunshine-on-another-port)
   if Sunshine does not use port 47989).
3. Choose **Pair this PC**, then enter the displayed PIN in Sunshine within two
   minutes.
4. Open **Games**, choose Desktop or another advertised application, and press
   ![Cross][cross].
5. Press ![Touchpad][touchpad] + ![L1][l1] to end the stream and return to ProsperoLight.

Pairing credentials and settings are title-scoped. Installing under a different
title ID intentionally requires pairing again.

To forget an old PC, select it on **PCs**, move to **Remove** and hold
![Cross][cross] until the button fills. This works while the PC is offline and
removes only that saved entry; it does not revoke pairing on the host or affect
other saved PCs. Press ![Triangle][triangle] to search the network again, or
choose **Add a PC**, to find and pair with the replacement host. A host
advertising itself on the network may appear again after a search.

### Sunshine on another port

Sunshine listens on port 47989 unless its **Port** setting (Configuration →
Network in the Sunshine web UI) was changed. ProsperoLight keeps one port per
saved PC:

- A PC found on the network uses the port Sunshine advertises. Nothing needs
  to be typed.
- Select a PC on **PCs** and choose **Change port**. An empty entry restores
  47989.
- **Add a PC** also accepts an address with a port, for example
  `192.168.1.50:48989`.

Only this one number is needed: Sunshine derives its other ports from it and
reports them to ProsperoLight. The same address can be saved more than once
with different ports, for example for two Sunshine instances on one PC.

### More than one controller

Each user signed in on the PS5 plays with their own controller, up to four.
The user who started ProsperoLight is the first controller on the PC. To add a
player, turn on another controller and choose a user for it when the PS5 asks,
before or during a stream; a notification confirms that the controller joined.
A controller that is switched off, or whose user signs out, is removed from the
PC, and the next controller to join takes its place.

> [!TIP]
> For the smoothest 90 or 120 FPS result, choose the resolution, frame rate,
> codec, and bitrate before launching the Sunshine application. If you change
> them after returning to the launcher, stop the active application first and
> then launch it again so Sunshine creates a fresh capture/encoder session.
> Tune bitrate upward case by case; a higher value can reduce smoothness at
> 4K or high frame rates even when the network link is fast. The decoder's
> measured limits are under [Bitrate limits](#bitrate-limits).

## Controls

The console keeps its own Create and PS buttons. The stream shortcuts below
send the host's Back and Guide buttons instead.

The icons are the controller's buttons. ![Touchpad][touchpad] is the touchpad button: press the
touchpad down until it clicks.

### Launcher

| Input | Action |
| --- | --- |
| ![D-pad][dpad] / left ![analog stick][stick] | Move focus or change the selected PC/application/setting |
| ![Cross][cross] | Activate, pair, launch, resume, or change a setting |
| ![Circle][circle] | Return to the PCs page |
| ![Square][square] | Stop the active Sunshine application |
| ![Triangle][triangle] | Search the network for Sunshine again |
| ![L1][l1] / ![R1][r1] | Change between PCs, Games, Settings, and About |
| ![Options][options] | Open Settings |

### Streaming

| Input | Action |
| --- | --- |
| ![Touchpad][touchpad] + ![R1][r1] | Toggle the metrics overlay |
| ![Touchpad][touchpad] + ![L1][l1] | End the stream and return to ProsperoLight |
| ![Touchpad][touchpad] + ![Square][square] | Toggle mouse/controller mode |
| ![Touchpad][touchpad] + ![Triangle][triangle] | Toggle ProsperoLight's stream keyboard |
| ![Touchpad][touchpad] + ![L3][l3] | Send the Back button to the host |
| ![Touchpad][touchpad] + ![R3][r3] | Send the Guide button to the host |
| Physical USB keyboard | Forward keys and modifiers directly to Sunshine |
| Physical USB mouse | Move, click, and scroll directly on the Sunshine host |
| Either ![analog stick][stick] in mouse mode | Move the pointer |
| ![Cross][cross] / ![Circle][circle] / ![Square][square] in mouse mode | Left / right / middle mouse button |
| ![L1][l1] / ![R1][r1] in mouse mode | Mouse X1 / X2 button |
| ![D-pad][dpad] in mouse mode | Vertical / horizontal scroll |
| ![D-pad][dpad] while keyboard is open | Move between keys |
| ![Cross][cross] while keyboard is open | Type the selected key |
| ![Triangle][triangle] while keyboard is open | Toggle Shift |
| ![Square][square] while keyboard is open | Send Backspace |
| ![Options][options] while keyboard is open | Send Enter and close the keyboard |
| ![Circle][circle] while keyboard is open | Close the keyboard |

![Touchpad][touchpad] + ![R1][r1] and ![Touchpad][touchpad] + ![L1][l1] work on every controller. Mouse mode and
the stream keyboard belong to the first controller; on the others those two
shortcuts are ordinary button presses for the game.

[cross]: docs/images/buttons/cross.svg "Cross"
[circle]: docs/images/buttons/circle.svg "Circle"
[square]: docs/images/buttons/square.svg "Square"
[triangle]: docs/images/buttons/triangle.svg "Triangle"
[options]: docs/images/buttons/options.svg "Options"
[l1]: docs/images/buttons/l1.svg "L1"
[r1]: docs/images/buttons/r1.svg "R1"
[l3]: docs/images/buttons/l3.svg "L3"
[r3]: docs/images/buttons/r3.svg "R3"
[dpad]: docs/images/buttons/dpad.svg "D-pad"
[stick]: docs/images/buttons/stick.svg "Analog stick"
[touchpad]: docs/images/buttons/touchpad.svg "Touchpad button"

The stream keyboard contains every printable US-ASCII character used by
standard passwords. It is not currently a multilingual or Unicode input
method. Keyboard text is sent directly as Moonlight key events and is not
stored by ProsperoLight or written to its configuration.

## Settings in detail

What the less obvious rows of the Settings screen do, and what they ask of the PC.

### Interface language

Settings → Language opens a scrolling dropdown with the Moonlight Qt language
set (30 translations plus English). Automatic follows the console language at
startup; unsupported/error results use English. Manual selection applies
immediately and is preserved across restarts without changing pairing or stream
settings. Readable translations are stored in `assets/locales/*.json`. See
[localization maintenance](tools/i18n/README.md) for fonts, source attribution,
and regeneration after editing a catalog. Some complex-script and mixed-direction
layouts still need native-speaker visual review. Diagnostic logs and stream
performance measurements keep their technical English labels.

### Frame rate and host options

Select 30, 60, 90, 120, or Custom in Settings. Custom reveals a slider directly below the selector (30–120, step 1). Its selection and value persist; selecting a preset hides the slider.

**Mute host audio** requests audio on the PS5 without playback on the PC (`localAudioPlayMode=0`). Turning it off requests playback on both. Actual host silence depends on the host audio device/routing; a virtual audio sink is typically required. An already active host session can keep its existing audio policy.

**Optimize game/display settings** sends the standard Moonlight `sops` request. GeForce Experience can optimize game settings. Sunshine can use it for display automation, rather than game quality. Vibepollo removed the requirement for this flag in its display automation; it may have no effect there. Both options preserve the previous defaults (mute and optimization enabled) and apply to the next launch/resume request.

Sources: [Moonlight request implementation](https://github.com/moonlight-stream/moonlight-qt/blob/master/app/backend/nvhttp.cpp), [Sunshine configuration](https://docs.lizardbyte.dev/projects/sunshine/master/md_docs_2configuration.html), [Vibepollo display automation change](https://github.com/Nonary/Vibepollo/releases/tag/1.6.0-apollo).

Further Vibepollo options should be optional and capability-aware: virtual display selection can complement host preferences, and `clientVrrRequested` is a host display hint, not proof of PS5 VRR. Keep scaling at its default unless explicitly requested. Encoder, RTSS, and frame-generation configuration remains on the host. These additional overrides are not enabled by this change.

### Per-PC host settings

Use **PCs → PC settings**, or the **Host session** section in Settings. The named selected PC owns these settings; switching PCs does not copy them. Settings are stored by server identity, with an address/port fallback before pairing. Removing a PC clears its preferences; Reset PC settings restores defaults.

Host audio, optimization, and quitting the host app are now per-PC. Existing global choices provide migration defaults for PCs without saved preferences. FPS/codec/bitrate and PS5 presentation settings remain client-wide.

**Host extensions** defaults to Off: ordinary Sunshine receives the standard request. Select Vibepollo only for a compatible host. Its advanced options default to the host policy: Host VRR and Host display omit overrides, and 100% resolution scale omits scaling. Turning extensions off preserves saved advanced choices but sends none of them. These options apply to both launch and resume.

Host VRR is experimental and requests Vibepollo virtual-display capture timing, rather than enabling TV VRR. Automatic virtual-display policy in current Vibepollo can use a fixed 1000 Hz virtual output for more precise capture timestamps; it does not change the requested stream FPS. Explicit virtual display selection requires the host to advertise a ready driver. A scale below 100% may lower effective resolution; application overrides on the host can take priority.

The launch controller mask (`gcmap` and `remoteControllersBitmap`) contains only connected PS5 controllers, up to four. Vibepollo requests also carry their PlayStation mask (`psmap`, intersected with `gcmap`). In current Vibepollo this supports PlayStation compatibility policy for Proton launches; it is not a DS4/DS5 selector. Controller arrival events continue to identify PlayStation controllers for hot-plug. DS4 versus DS5 is selected in the host's ViGEm/VHF configuration, so the client does not expose a nonfunctional selector.

Sources: [host request parsing](https://github.com/Nonary/Vibepollo/blob/master/src/nvhttp.cpp), [Proton launch policy](https://github.com/Nonary/Vibepollo/blob/master/src/process.cpp), [host controller profile selection](https://github.com/Nonary/Vibepollo/blob/master/src/platform/windows/input.cpp).

### PyroWave, pacing and controller extensions

PyroWave is an explicitly selected GPU decoder and RADV presentation backend.
It requires a compatible Vibepollo/Vibeshine host advertising the selected
4:2:0/4:4:4 and SDR/HDR profile; compression must be disabled. Unsupported
profiles are reported instead of silently changing codecs. HEVC and H.264
remain available for ordinary Sunshine hosts. See [PyroWave](docs/PYROWAVE.md).

Use wired Ethernet and a high bitrate for PyroWave. On one PS5 13.60 setup,
4K120 SDR/HDR playback and codec changes without restarting were validated;
500-600 Mbps was stable, while 700-800 Mbps sometimes had network loss.
These observations are not a quality or latency guarantee for other hosts.

Settings offer **Unpaced**, **Paced**, and **Paced+VRR**, and a saved
V-Sync preference (forced On in Paced+VRR, with the saved preference restored on mode changes).
Unpaced is the default and adds no cadence timer. Paced uses fixed-output source
pacing; active Paced+VRR uses the shared source-timeline and adaptive repeat policy
for both video backends. Physical presentation measurements depend on the backend. See
[configuration](docs/CONFIGURATION.md#frame-pacing-and-vsync) and
[interval traces and graphs](docs/frame-pacing-measurements.md).

DualSense forwarding includes touch, accelerometer/gyro, RGB lightbar, rumble
and host-provided adaptive trigger effects, with up to four connected users.
Host emulation and game support determine which effects are available.
Native high-definition waveform playback and controller speaker audio are
not implemented; see [controller support](docs/DUALSENSE.md).

Diagnostic logs can be turned off in Settings: the stream's reports stop at
once, including the detailed trace; raw console output stops from the next start. Output traces keep the latest
capture per backend and pacing mode. Menu sounds have a switch of their own.

Settings, under Host session, has **Quit host app after stream**: it stops the
game or app on the PC when leaving the stream. It is off by default, which
keeps it running. ProsperoLight always returns to its launcher.

### Bitrate limits

> [!WARNING]
> **A higher bitrate is not always better.** The PS5 video decoder takes longer
> for larger frames. Past the limits below it cannot keep up: latency grows,
> then the stream freezes about once a second. At 4K and 120 FPS, set
> **80 Mbps or lower** for the measured native HEVC path. These limits do not apply to PyroWave.

![4K HEVC bitrate limits by frame rate: smooth up to 80 Mbps at 120 FPS, 115 Mbps at 90 and 60 FPS](docs/images/bitrate-limits.svg)

| 4K HEVC stream | Smooth up to | Freezes above | Recommended setting |
| --- | --- | --- | --- |
| 120 FPS | 80 Mbps | 100 Mbps | **80 Mbps** or lower |
| 90 FPS | 115 Mbps | 145 Mbps | **100 Mbps** or lower |
| 60 FPS | 115 Mbps | about 190 Mbps (extrapolated) | **100 Mbps**; 150 Mbps is not measured |
| 1440p and 1080p, any frame rate | at least the 4K values | not measured yet | as for 4K |

- **The numbers are the bitrate the host actually delivers.** The setting is a
  ceiling the encoder only reaches in busy scenes. One session at the 80 Mbps
  setting carried 24 Mbps; one at 300 Mbps carried about 160 Mbps.
- **Why there is a limit.** A 4K HEVC frame takes about 3.7 ms plus 45 µs per
  kilobyte to decode (5.5 ms plus 29 µs per kilobyte above roughly 105 KB). At
  120 FPS it must finish within 8.3 ms, which allows about 100 KB per frame:
  100 Mbps. A lower frame rate leaves more time per frame, so the limit rises.
- **What happens above it.** Frames wait in a queue, adding up to 0.15 s of
  delay. When 15 frames are waiting they are all discarded and a keyframe is
  requested. At the 300 Mbps setting this happened 13 times in 17 seconds, and
  the decoder still only managed 90 FPS.
- **How to tell.** With the overlay on (![Touchpad][touchpad] + ![R1][r1]), "Frames dropped
  by decoder backlog" rises and "Decode (last second)" shows a load near 100%.
  "Frames dropped by your network connection" is a different problem.
- Bitrates of 300 Mbps and above are beyond these measured native HEVC limits.
- Other profiles: PyroWave is marked smooth to 500 Mbps, with a warning above it and a
  stronger one from 700 Mbps. H.264 at 4K is marked smooth to 80 Mbps at 60 FPS and is
  warned about at 90 and 120 FPS at any bitrate.

Measured on a PS5 with HEVC SDR and eight slices per frame, decoding one frame
at a time (the default). HDR, H.264 and the lower resolutions are not measured
yet. `python3 tools/plot-bitrate-limits.py` redraws the chart from the model;
[round 4](docs/PERFORMANCE_ROUND_4.md) has the measurements.

## Reporting a problem

When something does not work (a PC that will not pair, a stream that will not start,
stutter, a black picture, no sound), the **Diagnostic logs** capture tells us what happened:

1. Open **Settings**, go to **Diagnostics** and turn **Diagnostic logs** on.
2. Repeat the problem. For a stream problem, let the stream run for half a minute if it can.
3. Copy `debug-trace.txt` from the logs folder (`/data/prosperolight/logs`, or
   `/download0` inside the app's sandbox when filesystem access was not granted; the
   About page shows which) with an FTP client, and attach it to a
   [GitHub issue](https://github.com/blackbearreloaded/ProsperoLight/issues) with a
   sentence on what you did and what you saw. `debug-trace.prev.txt` is the run before.
4. Turn **Diagnostic logs** off again.

Every line carries the seconds since the app started and a tag:

| Tag | What it records |
| --- | --- |
| `app`, `launcher`, `main` | Start-up, storage and filesystem access, each start-up stage with its time, the update check, every stream's start and end |
| `screen`, `setting`, `notice` | The page opened, a setting changed, and every message the app showed you |
| `job` | Each request to a PC (search, refresh, unpair, stop) with its result and how long it took |
| `launch` | The PC, the app and every stream setting at the moment Start was pressed |
| `session` | The stream's own reports: connection stages, decoder and display set-up with result codes, why it ended, clean-up |
| `stream` | Once a second while streaming: frames received, decoded and shown, losses, decode and queue times, network delay |

The file holds the names and local addresses of your PCs and the names of their apps.
It never holds the pairing key, the certificate or a PIN. It stops growing at 16 MB and
keeps its beginning. The single **Diagnostic logs** switch controls this trace and the CSV/JSON performance
files written after a stream. Stream and launcher messages are written once to
`debug-trace.txt`; raw console output is retained separately as a fallback.
The existing diagnostic-log preference is preserved; the former separate Debug log
preference is no longer used.

## Files on the console

When it starts, ProsperoLight gives a resident upstream Lapy service a bounded
first chance, then sends its packaged exact-title one-shot helper to the local
payload loader (the method ProsperoEden uses, see
[`tooling/elevation`](tooling/elevation/README.md)). With access, everything the
app writes is in one folder that an update, a reinstall or a new title image
does not touch:

| Path | Contents |
| --- | --- |
| `/data/prosperolight/config/prosperolight-config.bin` | Saved PCs and stream settings |
| `/data/prosperolight/pairing/` | `cert.pem` and `key.pem`, the identity Sunshine paired with |
| `/data/prosperolight/logs/prosperolight-launcher.log` | This launch's log; the previous one is `prosperolight-launcher.prev.log` |
| `/data/prosperolight/logs/performance-last.json` | The last stream's performance report |
| `/data/prosperolight/logs/crash-last.txt` | Written only if the app crashes: where it happened |
| `/data/prosperolight/cache/` | The shaders the launcher compiled, so later launches are faster; safe to delete |

The first start with filesystem access copies the saved PCs, the settings and
the pairing from the title's own storage, so nothing has to be paired again.
The older copies stay where they were. To start over, delete
`/data/prosperolight` while the app is closed. Keep `pairing/key.pem` private:
it is what lets this console connect to a paired PC.

If neither elevation path succeeds, the app keeps working from its sandbox:
`/app0` and `/download0`. The first line of the log records `existing`,
`resident`, or `helper` (`status=0` means access was given).

## Project foundation

> [!IMPORTANT]
> **Built on the [PS5 Native App Boilerplate](https://github.com/blackbearreloaded/ps5-native-app-boilerplate).**
> ProsperoLight preserves the template's C++20 structure, `.hpp` interfaces,
> reproducible clean-room runtime, native FSELF tooling, tests, safe folder
> deployment, and release automation.

The client uses the established
[moonlight-common-c](https://github.com/moonlight-stream/moonlight-common-c)
protocol implementation rather than reimplementing the wire protocol. The
launcher, stream coordination, PS5 input/audio/video integration, UI, and build
tooling are maintained in this repository.

| Identity | Value |
| --- | --- |
| Shell title | `ProsperoLight` |
| Title ID | `PPSA99002` |
| Category | Game |
| Beta / stable | `01.000.090` / `01.000.060` |
| Version source | [`sce_sys/param.json`](sce_sys/param.json) |
| Writable data | `/data/prosperolight` (`/download0` if elevation is unavailable) |

## Build

```bash
# The complete title folder: what the automation builds and releases as a ZIP.
make
```

The output is written to:

```text
dist/PPSA99002/           complete title folder
```

No image is built. The compressed `.ffpfsc` image of earlier versions is gone
(on firmware 13.60 it was seen to mount with duplicated 64 KiB blocks of the
executable), and so are the raw `.exfat` and the `.ffpkg` images: if an old
`PPSA99002.ffpfsc` or `PPSA99002.exfat` is still in a directory scanned by
ShadowMountPlus, delete it before installing the folder.

Useful development gates are:

```bash
make test       # C++ unit/runtime, presentation ownership, report, and tooling checks
make test-stream-performance # Host-only scalar/SIMD FEC and Opus compatibility
make lint       # formatting, static analysis, metadata, asset, and shell checks
make check      # lint + every host test + complete folder build
```

The folder is the only build output; see
[The app folder and the release ZIP](docs/RELEASE_ZIP.md).

## GitHub Actions and releases

The [Build workflow](.github/workflows/tooling.yml) runs for pull requests,
version tags, pushes to the `dev` and `feature/**` branches, and when started by
hand (Actions tab, the Build workflow, "Run workflow"). A push to `main` builds
nothing. It:

1. checks out all pinned submodules;
2. installs the public Linux/PS5 build prerequisites;
3. validates metadata and the release tag;
4. runs lint, GoogleTest, runtime-allocation, presentation/report guards, Python
   integration checks, and host-only scalar/SIMD FEC and Opus comparisons;
5. independently reproduces and verifies `runtime/libc.prx`;
6. builds the complete app folder and archives it as `PPSA99002.zip`, every
   entry stored as 0777; and
7. generates `SHA256SUMS` for the ZIP and uploads both files as the Actions
   artifact.

A pull request's build is uploaded as `ProsperoLight-PR<number>-<commit>` (the
pull request's own commit) and names itself on the launcher's About page with a
line such as `PR 28, 1ae2fd0` above the version; its version is unchanged, so update checks
behave as for the release. Locally, `BUILD_LABEL="my test" make` does the same.

A tag matching the exact `contentVersion` verifies that build-time checksum
again, then publishes the app-folder `.zip` and `SHA256SUMS`.
No image is built or published, by the automation or by a local target, to
prevent compatibility issues with the in-app update worker and ProsperoStore, which
install from the ZIP.

A release is made by pushing the version tag and in no other way: the workflow
builds, attests, and publishes the ZIP and `SHA256SUMS`, and files are not
attached by hand. If the tag has no release yet, the workflow creates it; if a
release exists without a ZIP (notes written in advance, or a draft), it adds the
two files and leaves the title and notes alone; if a release already has a ZIP,
nothing is replaced and the run ends with a warning.

## Deploy

For an already-running PS5 FTP service, stage the development folder with:

```bash
make deploy PS5_HOST=192.168.1.100
```

Fully close ProsperoLight before deploying. The deployer writes only the
current title below `/data/homebrew`, uploads through temporary names, and
publishes `eboot.bin` and `sce_sys/param.json` last. Upload the complete folder;
`eboot.bin` alone is not a valid deployment.

ProsperoLight never changes PS5 system settings or configures a loader. See
[Deployment](docs/DEPLOYMENT.md) for the safe development loop and removal
behavior.

## Source layout

```text
src/main.cpp                         launcher and stream take turns
src/launcher/launcher_model.cpp      launcher state and requests, no drawing
src/launcher/launcher_view.cpp       the four screens and their dialogs
src/launcher/launcher_ps5.cpp        display, controller, sound, box art
src/moonlight_backend.cpp            pairing, app listing, artwork, and control
src/moonlight_discovery.cpp          LAN discovery
src/moonlight_config.cpp             host and preference persistence
src/app_storage.cpp                  where files are kept: /data/prosperolight
src/elevation/                       request for filesystem access
tooling/elevation/                   upstream Lapy integration notes
src/moonlight_stream.cpp             Moonlight session, VideoDec2, audio, input
include/moonlight_physical_input.hpp USB-HID to Moonlight input mapping
src/native_agc_present.cpp           zero-copy AGC presentation and overlays
src/gamestream/                      retained Moonlight-compatible C boundary
include/*.hpp                        application-owned public interfaces
platform/ps5/                        narrow Moonlight PS5 compatibility adapters
third_party/                         pinned Moonlight, mbedTLS, and Opus sources
third_party/ps5-homebrew-ui/         the UI kit the launcher is drawn with
assets/                              launcher fonts and sounds
tools/launcher-host/                 the launcher on the PC, for tests and pictures
sce_sys/                             launcher metadata, 4K artwork, icon, music
runtime/libc.prx                     generated clean-room loader runtime
tooling/native/                      native ELF/FSELF and runtime build tools
tests/                               GoogleTest and Python host regressions
docs/                                architecture, setup, testing, and evidence
```

Application-owned code is C++20 with `.hpp` interfaces. The retained
`src/gamestream` C code and pinned upstream dependencies preserve their native
language and public headers; they are dependency boundaries, not a second
application architecture. See [Architecture](docs/ARCHITECTURE.md) and
[Porting notes](docs/PORTING.md).

## Versioning

[`sce_sys/param.json`](sce_sys/param.json) is the only application identity and
release-version source. Its PS5-format `contentVersion` is injected into the
top bar, checked against the release tag, and used as the GitHub Release name.
Do not add a `v` prefix.

```bash
# After updating param.json and passing all local gates:
git tag 01.000.060
git push origin main 01.000.060
```

Keep `PPSA99002`, `conceptId`, and `contentId` stable for updates to this title.
Changing the title ID creates a separate PS5 application and separate pairing
storage. See [Configuration](docs/CONFIGURATION.md).

## Documentation

| Document | Purpose |
| --- | --- |
| [Changelog](CHANGELOG.md) | User-visible changes in each published version |
| [Getting started](docs/GETTING_STARTED.md) | Clean-machine prerequisites and first build |
| [Architecture](docs/ARCHITECTURE.md) | Launcher, protocol, video, audio, and input flow |
| [Configuration](docs/CONFIGURATION.md) | Identity, versioning, settings, and build variables |
| [Testing](docs/TESTING.md) | Host test boundaries and commands |
| [Performance Round 2](docs/PERFORMANCE_ROUND_2.md) | Offline A/B candidates, measurements, and hardware promotion gates |
| [Validation](docs/VALIDATION.md) | Hardware acceptance checklist and recorded evidence |
| [Deployment](docs/DEPLOYMENT.md) | Safe folder staging and smoke tests |
| [Release ZIP](docs/RELEASE_ZIP.md) | The app folder and its ZIP, the only outputs |
| [Troubleshooting](docs/TROUBLESHOOTING.md) | Common build, launch, and runtime failures |
| [Platform notes](docs/PLATFORM_NOTES.md) | PS5 filesystem, loader, and presentation constraints |
| [Runtime shim](docs/RUNTIME_SHIM.md) | Clean-room `libc.prx` scope and reproduction |
| [Presentation assets](docs/PRESENTATION_ASSETS.md) | Icon, backgrounds, and selection audio |
| [Contributing](CONTRIBUTING.md) | Change, test, and release requirements |
| [Notices](THIRD_PARTY_NOTICES.md) | Dependency, asset, and license attribution |

### Elevation credits

Filesystem elevation uses the cooperative Lapy design from
[ArkSama's PS5-Lapy-JB-Daemon](https://github.com/ArkSama/PS5-Lapy-JB-Daemon) and
[mpereiraesaa's PS5-Lapy-JB-Daemon](https://github.com/mpereiraesaa/PS5-Lapy-JB-Daemon).

<!-- bbr-footer:start -->
<!-- Generated by ps5-homebrew-dev-protocol/scripts/readme-footer. Edit the template there, not here. -->

## Credits

Built with the [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk) by John Törnblom (ps5-payload-dev).
Third-party components, authors and licenses are listed in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## License

Copyright © 2026 BlackBearReloaded. Licensed under GPL-3.0-or-later; see [LICENSE](LICENSE). Third-party components keep their own licenses. Binary releases are built from the tagged source in this repository.

## Disclaimer

- **No affiliation.** This is an independent homebrew project. It is not
  affiliated with, endorsed by, or sponsored by Sony Interactive Entertainment.
  "PlayStation", "PS5" and related marks are trademarks of Sony Interactive
  Entertainment Inc. This project is not affiliated with or endorsed by the Moonlight or Sunshine projects.
- **No proprietary material.** No Sony SDK, firmware, encryption keys or
  decrypted system modules are included.
- **No warranty.** This project is provided "as is", without warranty of any
  kind, to the extent permitted by law. See sections 15 and 16 of the GPL.
- **Use at your own risk.** Running homebrew requires a modified console, which
  may void its warranty, breach the platform's terms of service, or cause data
  loss.
- **Legal use only.** Use it only with hardware, accounts and content you own.
  This project does not support or enable piracy.

## AI assistance

This project was developed with AI assistance from OpenAI and/or Anthropic tools.
<!-- bbr-footer:end -->
