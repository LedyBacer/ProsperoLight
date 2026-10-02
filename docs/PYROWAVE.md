# PyroWave / RADV integration candidate

Build the complete client with `make app` (equivalent to `make PYROWAVE=1 app`).
`make PYROWAVE=0 app` excludes RADV and preserves a VideoDec2-only build.
PyroWave is selected explicitly in Settings; there is no automatic codec override.
Hardware qualification of the new profiles is pending. This is not a claim that
HDR colour accuracy or the entire regression matrix has passed on PS5.

## Settings and profiles

Video bitrate uses the existing PS5 IME. Decimal integers 1–1000 Mbps are accepted,
invalid input leaves the previous value unchanged, and the stream receives Mbps
multiplied by 1000. Config v7 preserves migrations from v1–v6, paired host entries,
audio, V-Sync and decoder preferences. Default chroma is 4:2:0; the PyroWave chroma
preference is retained while another codec is selected.

| Profile | Chroma | Depth | Range | Backend |
|---|---|---|---|---|
| H.264 High | 420 fixed | 8 | SDR | VideoDec2 |
| HEVC Main | 420 fixed | 8 | SDR | VideoDec2 |
| HEVC Main10 | 420 fixed | 10 | HDR10 | VideoDec2 |
| PyroWave | 420 / 444 | 8 | SDR | RADV |
| PyroWave | 420 / 444 | 10 | HDR10 | RADV |

Resolution and FPS use the existing 1080p/1440p/2160p and 60/90/120 settings.
`ResolvedStreamProfile` separates Moonlight profile selection from the unchanged
nine-entry VideoDec2 configuration table. No PyroWave VideoDec2 mode is fabricated.

## Protocol and dependencies

The single moonlight-common-c submodule stays at ProsperoLight's original upstream
`f900dd4767759c7b9d0e93bcea666b55c69ea62f`. Build scripts apply the reviewable patch
in `tools/pyrowave/patches/moonlight/`. It carries Nonary's independent-frame
negotiation, depacketization, record-start/lost-packet markers, critical-packet
metadata and partial-frame timeout/recovery. References: Nonary
`927eba9639f3c9b5037ab45fc0d6486e2a19b39f` and
`3bbe8e818b42a8a1dc3a1381149b7e818fbd33fd`. Unrelated haptics and keyboard changes
are excluded; upstream H.264/HEVC IDR-drop behavior is retained.

Each PyroWave profile requires its exact host capability bit. Only its single
VIDEO_FORMAT is offered. An absent or mismatched DESCRIBE bitstream ID is an error,
not a fallback or warning. The required decoder revision remains
`186f0393b77f7755953b5ecde994bb1cec2e4155` (ID `186f0393`).

ANNOUNCE uses bitStreamFormat=3, stock chroma/dynamic-range attributes,
pyrowaveAdaptiveFec=0, pyrowaveAdaptiveBitrate=0 and pyrowaveFeatures=1.
Compression stays disabled. Sequence code 1 and other non-independent frames are
rejected. H.264/HEVC are manually available on ordinary Sunshine hosts.

Pinned PyroWave, Granite, Volk, Vulkan-Headers, PS5_Vulkan, PS5_Mesa and public
PayloadSDK revisions live in `tools/pyrowave/pins.sh`. RADV patches cover startup
refresh measurement, explicit VideoOut handoff and HDR10 scanout. Build caches
and source downloads stay ignored. PS5 getaddrinfo/freeaddrinfo continue using
ProsperoLight's real libSceNet adapter rather than SDK failure stubs.

## GPU and presentation

The receive callback copies a frame into a queue bounded to two frames; a worker
consumes the newest frame. The parser validates dimensions, chroma, record bounds
and sequence identity. Every frame clears the decoder. Partial frames require
intact coarse blocks and the Nonary 90% readiness criterion. No new FEC algorithm,
temporal reuse, adaptive bitrate, RDO, encoder or AV1 work is included.

SDR outputs three R8 planes; HDR outputs normalized R16 UNORM planes. 420 chroma
uses half width/height and 444 uses full width/height. The GPU shader expands
negotiated limited-range Rec.709 SDR or BT.2020 PQ HDR into RGB. PQ remains coded
through the HDR output; there is no tone mapping or CPU image readback.

HDR scanout shares `ps5_videoout_formats.h` with the existing HEVC presenter,
using its 10-bit BT.2020/PQ VideoOut format with VK_FORMAT_A2B10G10R10_UNORM_PACK32 (red in the low bits). SDR uses BGRA8. Moonlight's setHdrMode
and LiGetHdrMetadata callbacks remain active; HDR host/display mismatch reports
an error. Physical output HDR status is checked after the initial settling
windows, using the native presenter's 48-byte VideoOut status ABI.
The platform continues using its HDR calibration/output semantics as HEVC does;
no separate dynamic-metadata output implementation is introduced.

60 FPS requests the normal output mode; 90/120 request high refresh. V-Sync Off
uses the same HSYNC mode as the native presenter, with an explicit VSYNC fallback
if the platform refuses HSYNC. TV safe scales the image within the existing inset.
Touchpad+R1 toggles GPU statistics; touchpad+L1 disconnects. Touchpad+Triangle
shows the existing keyboard layout through a GPU text overlay, including selected
key and shifted labels. Native controller/physical keyboard/mouse event handling
is shared with VideoDec2. VideoDec2 CPU settings
are retained but unavailable during PyroWave selection.

On disconnect, submissions stop, the worker joins and GPU work retires. Decoder,
swapchain, device and instance are destroyed; an explicit WSI handoff then stops
the flip thread, unregisters buffers, restores output mode, closes VideoOut and
releases direct memory before the launcher reacquires the display.

Logs: `/download0/prosperolight-pyrowave.log`. Stream-start and ANNOUNCE logs show
profile, format, resolution, FPS, bitrate, chroma, depth, range, backend and
bitstream. PyroWave GPU timing stays separate from VideoDec2 decode-load metrics.

## Console review

See `PYROWAVE-REGRESSION.md`. The earlier 420 SDR PoC, HUD, disconnect/reconnect
and manual bitrate IME were confirmed by the user. The final profile/config,
limited-range conversion, HDR, 444, output-mode and transport-cleanup changes
require the later console review. Compile success does not establish these passes.
