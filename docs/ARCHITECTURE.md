# Architecture

ProsperoLight keeps the application boundary small and explicit:

```text
OpenGL launcher (main.cpp, src/launcher/)
        |
configuration, discovery, pairing, application control
        |
Moonlight stream coordinator (moonlight_stream.cpp)
   |                 |                 |
VideoDec2 -> AGC     Opus -> AudioOut   DualSense + USB HID -> Moonlight input
PyroWave -> RADV
```

`moonlight_stream.cpp` owns the live session and its cleanup. Video access
units are decoded by VideoDec2 into GPU-visible resources and handed to AGC for
presentation. The application does not copy decoded pixels through a CPU frame
buffer on this path.

While a stream is active, the same 4 ms input loop polls `libScePad`,
`libSceKeyboard`, and `libSceMouse`. Physical keyboard USB-HID usages are
translated to the Windows virtual-key values used by Moonlight Qt, while
physical mouse deltas, five buttons, and both wheel axes use Moonlight's native
input packets. Key repeat remains host-driven. Teardown raises every tracked
key and mouse button before the connection closes so returning to the launcher
cannot leave input stuck on the host.

The AGC presenter selects its VideoOut target at the stream boundary. At
60 FPS, a 1080p stream uses two 1920x1080 buffers, while 1440p and 2160p use two
3840x2160 buffers; 2160p is presented 1:1 and 1440p is filtered by AGC. At 90
or 120 FPS, ProsperoLight retains the same resolution-aware output geometry.
The title advertises the high-resolution 120-Hz capability in `attribute3`,
then selects HFR through ordinary VideoOut request 15: 2160p/120 maps to the
native 3840x2160 119.88-Hz scanout and 1440p HFR is filtered into that 4K
target. This matches the public retail PS5 path and avoids restricted system
or VR output interfaces. The Connecting animation, TV-safe viewport, metrics
HUD, and stream keyboard use the same selected output geometry. Per-stream
teardown unregisters and releases the dynamically sized pool.

The launcher and all PS5-specific application integration are C++20, with
application interfaces named `*.hpp`. The proven Moonlight-compatible HTTP,
pairing, and RTSP implementation remains in `src/gamestream` as a C ABI
library; its `*.h` headers and the small PS5 C adapters deliberately retain
C-compatible names. Its upstream dependencies are pinned as submodules and
compiled in their native languages. This is a dependency boundary, not a
second application architecture.

The launcher is three parts. `launcher_model.cpp` holds what is known (saved
PCs, settings, Sunshine's state, pairing) and every request to the network; it
draws nothing. `launcher_view.cpp` draws the four screens and their dialogs
with the widgets of the UI kit in `third_party/ps5-homebrew-ui`, reads the
model and asks it for things. `launcher_ps5.cpp` owns the console: the EGL
display of the ps5-opengl SDK, the controller, the audio port, the sounds and
registers the portable `launcher_artwork.cpp` decoder for box art. It uses static
libpng with bounded allocations on the model worker, without system PNG imports.
The model and the view have no console call in
them, so `tools/render-launcher.sh` runs both on a PC against a pretend
Sunshine network, checks their behaviour and writes a picture of every state.

The launcher treats Sunshine availability as a sampled health state rather
than a permanent result. It asks the selected PC again every five seconds
without repeating mDNS discovery. A failed check retains the last successful
host and application snapshot, shows "Reconnecting", and retries after one
second. Three consecutive failures are required before the launcher shows the
PC as not answering; polling continues so a host that restarts during an RDP
or Windows-session transition recovers without a manual search. The other
saved PCs are asked once each so the list can show their state. Every request
(search, refresh, health check, unpair, stop, box art) runs on one worker
thread, one at a time, because the compact NVHTTP transport has process-global
timeout/error state; pairing waits for that thread to be idle. The screen keeps
drawing while a PC is slow to answer.

The launcher and the stream never own the display together. `main.cpp` runs
the launcher until the player starts a stream; the launcher then stops its
worker thread, closes its audio port and controller, deletes its graphics
objects and closes the EGL display, and only then does the stream open
VideoOut, the controllers and its own audio port. When the stream ends the
launcher is built again from nothing. After a stream above 60 Hz or in HDR the
display is left alone for five seconds first (`HFR_SETTLE_MS`), because the
television is changing mode. AGC is initialised once per process, by the
OpenGL runtime when the launcher draws its first frame; the stream's presenter
uses that initialisation.

The build creates minimal linker-only import stubs from
`vendor/ps5/sdk/stubs/*_link_stub.c` for PS5 system modules that are not in the
bundled SDK stub set. They only describe unresolved imports to the native
linker; the console resolves the actual system modules at run time.

`sce_sys/param.json` provides title identity and `contentVersion`; the launcher
reads the version from the installed copy and shows it on the Settings screen.

Every widget asks for a sound by what happened (the focus moved, a switch
flipped, a dialog opened), not by file name. The launcher plays those cues
from the recordings in `assets/audio/sfx/glass` (48 kHz signed 16-bit PCM,
`<cue>_NN.wav`) through a small mixer on its own AudioOut thread. The welcome
cue plays only when the app opens; returning from a stream plays a shorter
one. The thread is joined and the port closed before a stream starts.

Stream audio is independent of the launcher cues. Stereo uses two-channel
signed 16-bit AudioOut. The optional 5.1 mode negotiates Moonlight's standard
six-channel Opus layout (`FL FR FC LFE BL BR`) and writes it to PS5's validated
eight-channel AudioOut layout, with `SL` and `SR` zero-filled. If the
eight-channel port cannot be opened before negotiation, the session requests
stereo instead.

## Where files are kept

`storage::Initialize()` (`src/app_storage.cpp`) is the first call in `main`,
while the process has one thread. It reads what an earlier version kept in
`/download0`, gives a resident upstream Lapy service a bounded opportunity and
otherwise sends its packaged one-shot helper to the local payload loader, and
settles every path in `storage::paths()`: the install folder for the app's own
files and `/data/prosperolight` for what it writes. It then opens the log. No
other source names a sandbox path; without access the same structure holds
`/app0` and `/download0`.

## Integrated video and input extensions

Explicit profile resolution in `stream_profile.hpp` selects native or PyroWave
callbacks. PyroWave owns Vulkan/RADV presentation after the connecting plate
releases AGC VideoOut; native video keeps the plate until the first picture.
Both backends use `frame_pacing.hpp`; decoder ownership and teardown remain
backend-specific. `ps5_dualsense.cpp` is the sole owner of additional pad handles
and extended controller feedback. See [PyroWave](PYROWAVE.md),
[DualSense](DUALSENSE.md) and [frame pacing](CONFIGURATION.md#source-clock-frame-pacing).
