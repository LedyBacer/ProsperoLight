# ProsperoLight user guide

[← Back to README](../README.md) · [Video and performance](PERFORMANCE_GUIDE.md) · [Troubleshooting](TROUBLESHOOTING.md)

ProsperoLight streams applications and the desktop from a compatible Sunshine host to a homebrew-enabled PlayStation 5. A wired connection for both systems is recommended, particularly at high resolution or frame rates.

## Requirements

- A PS5 with a **compatible homebrew environment and title loader already installed**. This project does not provide an exploit or loader.
- A PC with [Sunshine](https://github.com/LizardByte/Sunshine) or a compatible GameStream host. **PyroWave** requires a host version that explicitly supports that codec; ordinary Sunshine works with H.264/HEVC.
- A working local network. Ethernet is strongly recommended; Wi-Fi quality varies.
- A DualSense controller for the launcher. Additional controllers and supported USB keyboard/mouse devices can be used during streaming.

## Install or update

### Install from a published release

1. Download the appropriate **`PPSA99002.zip`** and its checksum file from [GitHub Releases](https://github.com/blackbearreloaded/ProsperoLight/releases). Read the release notes: the `main` branch can contain features not yet shipped in a release.
2. Verify the ZIP against the accompanying `SHA256SUMS`, then extract it on your PC.
3. Fully close ProsperoLight on your console. Upload the **entire** `PPSA99002/` directory to `/data/homebrew/`, resulting in `/data/homebrew/PPSA99002/eboot.bin`. Do not upload only `eboot.bin` or the ZIP itself.
4. Refresh or restart your title loader (for example, ShadowMountPlus), or restart the console. Launch ProsperoLight and check its version in the launcher.

Use the published app-folder ZIP. **Do not leave an old `PPSA99002.exfat` or `PPSA99002.ffpfsc` mounted alongside the folder copy**: duplicate title registrations can cause an older version to launch. App-folder deployment and release behavior are detailed in [Release packaging](RELEASE_ZIP.md).

> **VRR metadata changes:** The PS5 can cache a title's declared HFR/VRR capabilities when it registers the app. After updating to a build with changed `sce_sys/param.json` capabilities, you may need to remove its home-screen registration and let your title loader register it again. Do not delete `/data/prosperolight` as part of that process. See [Configuration](CONFIGURATION.md#frame-pacing-and-vsync).

### Update from inside the app

A compatible folder installation can offer **Update now** when an updated release is listed by [homebrew.page](https://homebrew.page/ps5). The app downloads and verifies the release, then closes while an update helper installs it. The update may not be offered for unlisted, image-based or development builds. Use the manual method above when needed.

Settings and pairing data are normally stored outside the application folder and survive a regular update.

## Connect to a PC

1. Start Sunshine on your PC and ensure its streaming services are reachable from the PS5.
2. Open ProsperoLight, choose a discovered PC under **PCs**, or choose **Add a PC** and enter its IPv4 address.
3. Select **Pair this PC**. Enter the displayed PIN in Sunshine's **PIN** page before the pairing prompt expires (approximately two minutes).
4. Open **Games**, choose **Desktop** or another application, and launch it with **Cross**.
5. End a session with **Touchpad + L1**. ProsperoLight returns to the launcher; by default, the host app keeps running so it can be resumed.

Pairing identities are retained for up to eight saved PCs. If a PC is no longer needed, select it on the PCs page and hold **Cross** on **Remove**. This removes the local entry; it does not revoke the host's pairing record. A host advertising itself on the LAN may appear again after a new discovery scan.

### Sunshine on a non-default port

Sunshine normally uses **47989** as its base port. Discovered hosts advertise their configured port. For a manual host, enter `192.168.1.50:48989` (replace the address and port with yours), or use **PCs → Change port**. An empty port entry restores the default. Each saved PC keeps its own host port.

### Multiple controllers

Up to four PS5 controllers can join the same streaming session. Turn on another controller and select a separate PS5 user when prompted, before or during the stream. The first controller controls the on-screen keyboard and mouse-mode toggle; **Touchpad + L1** and **Touchpad + R1** work on every controller. The host and individual games determine whether PlayStation-specific feedback is available.

## Controls

The **touchpad button** is a click: press the touchpad down while using the second button. Create and PS remain console-owned; the special Back and Guide shortcuts send host-side actions.

### Launcher

| Input | Action |
| --- | --- |
| D-pad / left stick | Navigate PCs, games and settings |
| Cross | Select, pair, launch or confirm |
| Circle | Back / PCs |
| Hold Circle for 2 seconds on the PCs page | Close ProsperoLight; release to cancel |
| Square | Stop the running host application |
| Triangle | Rediscover Sunshine hosts |
| L1 / R1 | Switch launcher pages |
| Options | Open Settings |

### Streaming

| Input | Action |
| --- | --- |
| **Touchpad + L1** | End stream and return to launcher |
| **Touchpad + R1** | Toggle performance statistics |
| **Touchpad + Square** | Toggle controller/mouse mode (first controller) |
| **Touchpad + Triangle** | Toggle on-screen keyboard (first controller) |
| **Touchpad + L3** | Send Back to host |
| **Touchpad + R3** | Send Guide to host |
| USB keyboard/mouse | Forward supported keys, clicks and scrolling to the host |

In mouse mode, either stick moves the pointer; Cross/Circle/Square map to left/right/middle click, L1/R1 to side buttons, and D-pad to scrolling. In the on-screen keyboard, D-pad navigates, Cross types, Triangle toggles Shift, Square deletes, Options enters, and Circle closes it. The **ESC** key sends Escape to the host. The virtual keyboard supports standard printable US-ASCII characters; it is not a general Unicode input method.

See [DualSense support](DUALSENSE.md) for available motion, touch, light bar, rumble and adaptive-trigger forwarding and its host-side limitations.

## Settings overview

| Setting | Meaning |
| --- | --- |
| **Resolution / FPS** | 1080p, 1440p, 4K; 30/60/90/120 FPS or Custom 30–120 FPS |
| **Codec** | Native H.264/HEVC or optional PyroWave (requires compatible host) |
| **Bitrate** | Encoder target ceiling, not a promise of actual on-wire bitrate; see [performance](PERFORMANCE_GUIDE.md) |
| **Frame pacing** | Unpaced, fixed-refresh Paced or adaptive Paced+VRR |
| **VRR timing** | Low Latency / Balanced / Smooth; applies when variable output is actually active |
| **V-Sync** | Saved independently; Paced+VRR requires synchronized flips |
| **Display area / audio** | Full image or TV-safe; stereo or 5.1 surround |
| **Decoder pipeline / cores** | Native codec tuning; Classic is the baseline, Adaptive is experimental |
| **Language / menu sounds** | Console-language or manual localization; interface sounds on/off |
| **Diagnostic logs** | Detailed text and bounded performance traces for bug reports |

### Settings for each PC

Under **PCs → PC settings** or **Host session**, host-specific choices include mute host speakers, optimize game/display settings, quit host app after streaming, and optional Vibepollo extensions. Extensions are **off by default** for ordinary Sunshine compatibility. Host-side VRR options are not the same as the PS5 television's VRR state. FPS, codec, bitrate and PS5 presentation settings are client-wide, not per-PC.

Settings may only apply when launching or resuming a host app; if an existing session retains an old encoder/display configuration, stop that host app and start a fresh session. More precise behavior is documented in [Configuration](CONFIGURATION.md).

## Diagnostics and saved data

If a problem is reproducible, enable **Settings → Diagnostics → Diagnostic logs**, run the failing scenario, and then stop the stream normally. Include firmware, host/version, codec, resolution, FPS, pacing mode, and whether the TV reported VRR. Fetch the relevant trace files over your existing file-access method before [opening an issue](https://github.com/blackbearreloaded/ProsperoLight/issues).

With filesystem access, data is stored under `/data/prosperolight/`:

| Location | Contents |
| --- | --- |
| `config/` | Saved hosts and preferences |
| `pairing/` | Host pairing certificate and private key |
| `logs/debug-trace.txt` | Current diagnostic event trace; previous run in `debug-trace.prev.txt` |
| `logs/performance-last.json` | Latest stream performance report |
| `logs/crash-last.txt` | Last crash report, if one was produced |
| `cache/` | Regenerable UI shader cache |

When elevated storage access is unavailable, the app uses its own title storage (`/download0`). The **About** screen and startup logs indicate the active storage location.

> **Privacy:** diagnostic traces may contain local IP addresses, PC names and application titles. Review them before posting. Do not publish the pairing private key (`pairing/key.pem`) or raw saved-data archives.

See [Troubleshooting](TROUBLESHOOTING.md) for individual failure modes and how to collect additional performance evidence. Developers can consult [Validation](VALIDATION.md) and [frame pacing measurements](frame-pacing-measurements.md).

### Live frame interval graph

H.264/HEVC and PyroWave use the same statistics panel and keyboard layout. The
statistics panel includes the latest 180 intervals between new-frame CPU
submissions, with a 0–50 ms scale. Repeated frames do not count as new pictures.
Sampling and graph drawing stop when the panel is hidden or the keyboard is open.
This graph measures submission cadence, not physical HDMI timing or input latency.
The PyroWave Output frequency is derived from software VideoOut counters, falling
back to the selected mode when counters are unavailable; it is not a TV measurement.
