# Port validation

## Host gate

Before deploying, run:

```sh
make check
make ffpfsc
```

Confirm the package uses the expected `titleId`, `contentVersion`, and
ProsperoLight title from `sce_sys/param.json`.

## PS5 smoke test

Before touching the console, check the shared PS5 workspace `lock.txt`. If it
exists, wait and re-check every 15 seconds. If it is absent, create it for the
duration of this test; delete it immediately afterwards. Do not change PS5
settings.

1. Install the fresh title and open the launcher. Confirm the version in the
   top bar matches `contentVersion`.
   A saved Sunshine host must be reported online when its `/serverinfo`
   endpoint is reachable; an offline result fails this gate.
2. Move focus across every item on all four launcher screens, including the
   host card and action buttons. This exercises lazy texture loading; the title
   must remain alive and the detected host state must not regress.
3. Pair with Sunshine and restart the app once to confirm pairing persistence.
4. Launch Desktop at default H.264 1080p. Check video, stereo audio, and
   DualSense input. Then select 5.1 in Settings, relaunch the stream, and verify
   discrete front-left, front-right, center, LFE, back-left, and back-right
   output with no signal on the unused side pair.
5. Toggle the metrics overlay with `Touchpad + R1`, then return using
   `Touchpad + L1`. Confirm no debug text is left in the launcher.
6. At Windows sign-in, press `Touchpad + Triangle` and confirm ProsperoLight's
   stream keyboard appears without a PS5 notification. Use the D-pad and Cross
   to enter private text, Triangle for Shift, Square for Backspace, and Options
   for Enter. Confirm the password is never present in ProsperoLight
   diagnostics or storage. Toggle the keyboard off again.
7. Launch and stop a non-Desktop Sunshine application. Repeat a launch after
   returning to the launcher; a failed or stalled session must return with a
   useful error rather than remain black.
8. Test mouse emulation using `Touchpad + Square`, then return to controller
   mode with the same shortcut. Confirm both modes still accept input.
9. Connect a physical USB keyboard and mouse. In Desktop, type letters,
   punctuation, Shift/Ctrl combinations, Backspace, Enter, and an arrow key;
   then verify relative pointer movement, left/right/middle click, and the
   wheel. Disconnect only after releasing all keys and buttons, then return to
   the launcher and reconnect to confirm no host input remains stuck.
10. If the host supports them, test HEVC and HDR separately. Verify image,
   audio, input, cleanup, and recovery; do not treat HDR audio-only output as a
   pass.

For release readiness, add a longer 1080p60 gameplay soak, network interruption
recovery, repeated launch/stop cycles, and a check that no decoded-video CPU
copy or frame backlog appears in the metrics.

## Latest hardware evidence

On 2026-08-28, `PPSA77003` content version `01.000.011` passed a clean PS5
launch/close cycle after the C++ runtime migration. The console reported the
restored HDR-capable title profile as `HDR:o`, and the launcher reached the
saved Sunshine host `Gaming-PC` at `192.168.4.20`, changing it from the
regressed offline state to `ONLINE / NOT PAIRED`. The fix routes network-handle
nonblocking configuration through the `libSceNet` `ioctl(FIONBIO)` adapter
instead of libc `fcntl()`.

The tested `eboot.bin` SHA-256 is
`539c0a0cd7bc16c41f40aee6c24bbc58416d4d5597c45e69c61e5d70d4361a36`.
No application fatal signal appeared in the bounded kernel log, and FTP, klog,
and loader services remained reachable after close. Pairing and physical-pad
focus traversal remain explicit manual gates for this fresh application data.

Content version `01.000.013` was also exercised against Sunshine state
`SUNSHINE_SERVER_BUSY` with active app ID `881448767`. The launcher remained
online, labeled the host `BUSY`, disabled pairing before opening the PIN modal,
and displayed the unpaired-client recovery instruction without clipping. Its
tested `eboot.bin` SHA-256 is
`aaa47276ba5844e6aee625ed7def176149631a92ea950ec3e82eba8d80eaf7f9`.

Content version `01.000.014` moves controller initialization and the
`Touchpad + L1` escape monitor ahead of decoder allocation and keeps the monitor
alive through the Moonlight connection handshake. Startup is bounded to 20
seconds and the first video frame to 10 seconds; either failure closes the
Sunshine session, reports the reason, and returns to the launcher. Host tests
(`make test` and `make lint`) pass. The folder build was deployed, but its PS5
launch/recovery gate remains pending because the console's loader, log, and
file-service ports became unavailable before the test could be completed.

Content version `01.000.017` isolated the later connection freeze precisely.
The encrypted RTSP OPTIONS, DESCRIBE, and audio SETUP exchanges completed,
Sunshine supplied audio port `48000`, the PS5 UDP socket bound successfully,
and `pthread_create()` returned success. The new audio-ping thread then entered
the common-c wrapper but stopped inside `pthread_setname_np()` before invoking
its entry point. This explains why the connecting animation eventually ended
in a process failure and why video SETUP never began.

Content version `01.000.018` makes thread naming a PS5-only no-op in the forced
compatibility header, leaving thread creation and every stream worker entry
unchanged. The temporary common-c tracing was removed and the submodule was
restored to its fetchable upstream commit. Nine integration tests, eight
GoogleTests, the allocation-runtime test, lint, and the native app build pass.
The candidate `eboot.bin` SHA-256 is
`1d1e7bd949aeec4598b33f1640ca33d0932c6fc4a79e7ec3bbd6bcccd91d8eb1`.
The deployed build then completed all 11 Moonlight connection stages, started
H.264 video, stereo audio, and input, and presented at least 3,600 decoded
frames with `pending=0`. ShadowMount recorded a normal title stop with no new
crash candidate, and FTP, klog, and loader services remained reachable after
cleanup.

Content version `01.000.021` adds password-safe native PS5 text entry during a
stream. The frozen candidate at commit `a608b67` launched, rendered the paired
launcher, resumed the H.264 Desktop stream, and opened PS5 common-dialog title
`NPXS40093` when the targeted `Touchpad + Triangle` chord was sent. Cancelling
the dialog sent no text; the existing `Touchpad + L1` shortcut then returned to
the launcher, and the title closed normally. FTP, klog, and loader services
remained reachable after teardown. The tested `eboot.bin` SHA-256 is
`a7c0c7bb03d605c738d1db531b4923109e555fccc247bc144fc65bc8f09881b7`.
This is a `partial-pass`: native password-dialog launch, cancellation, and
stream recovery are hardware-proven, while text-plus-Enter delivery and an
actual private Windows sign-in remain the operator acceptance step.

Content version `01.000.022` replaced the native dialog with the documented
Windows `Win + Ctrl + O` shortcut. The PS5 notification proved that the
`Touchpad + Triangle` handler ran, but the Windows On-Screen Keyboard did not
appear at the secure sign-in screen. This is a failed acceptance result for
that approach; Windows documents opening its Accessibility menu at sign-in
rather than relying on the desktop OSK shortcut.

Content version `01.000.023` therefore rendered a password-safe keyboard inside
the existing AGC stream overlay and sends each selected key through Moonlight's
ordinary keyboard event path. Host tests cover layout navigation, shifted
labels, and all stream shortcut chords. The frozen candidate `eboot.bin`
SHA-256 is
`d6d32540483262394695fffa0c302ef5192b90fa10d2066be9abe6c4cb717225`.
On PS5 the overlay opened without interrupting video and D-pad navigation moved
its selection. The operator subsequently confirmed that private Windows sign-in
typing worked perfectly. This is a `pass` for the input path.

Content version `01.000.024` moves the keyboard into the upper-center safe
region and enlarges its key labels. It also removes the keyboard toggle
notification, raises panel opacity to 82%, and adds the previously missing
backtick/tilde key. A host regression test proves that the layout contains all
printable US-ASCII characters. The frozen candidate `eboot.bin` SHA-256 is
`f5a7e8bf655141dfa53dae455bd1c2d11ba07339485d9fe32d8586e98933baf2`.
The folder candidate was deployed to `PPSA77003`; the launch request was
accepted and both payload and FTP services remained healthy after cleanup.
This Windows session could not capture Chiaki's render window, so the 82%
opacity and revised placement still require direct TV/Chiaki visual acceptance.

Content version `01.000.025` disables the optional blocking TCP telemetry sink
in normal builds. A `01.000.024` reproduction entered a black frame before the
Connecting renderer, produced no first-stage receipt, remained alive until an
explicit title-aware close, and left FTP, klog, and loader services healthy.
The first stream-stage action was a synchronous port-8767 telemetry connection
with no deadline. The production build now compiles that path to an immediate
no-op; `LAN_TELEMETRY=1` is reserved for bounded diagnostics with a receiver
already running.

The frozen `f3ad73f` candidate passed the corresponding hardware gate with no
listener on port `8767`. It started and presented H.264 1080p Desktop twice in
one process, returned through `Touchpad + L1`, then survived a complete title
close/reopen and resumed the Sunshine-reported running Desktop session. Sunshine
recorded a client connection, disconnection, and completed encoder teardown for
each observed session. The title processes stopped normally and FTP, klog, and
loader services remained reachable. The tested `eboot.bin` SHA-256 is
`1a184f009fb138579644e3195ab5e2a7958f3c4db10306f50e9c7906b37b9fcc`.

The same run selected Steam after returning from Desktop. ProsperoLight returned
to the launcher instead of hanging; Sunshine identified the independent host
failure as `steam://open/bigpicture` being denied by the Windows service
account. Fixing that Sunshine application command remains a host-configuration
task, not a client lifecycle failure.

Commit `048d334` adds automatic Sunshine health recovery. The launcher checks
the saved address asynchronously every five seconds, retries transient failures
after one second, and requires three consecutive failures before displaying
`OFFLINE`. The policy has a focused host test and the complete `make check`
gate passed. The folder candidate was deployed to `PPSA77003`; its `eboot.bin`
SHA-256 is
`d0cd9430b421442008c404524f93cdf0946da71ddf45d009e8a21bbde2d0e2b9`.
It entered `eboot`, ran for the bounded ten-second observation without a loader
error or fatal signal, closed through the title-aware controller, and left FTP,
klog, and elfldr reachable. This is a `partial-pass`: launcher lifecycle and
cleanup passed, while an RDP/Windows-session transition must still verify the
visible `RECONNECTING` to `ONLINE` recovery behavior.

Commit `9b0e020` preserves the last complete Sunshine application catalogue
during stop-session teardown. The stop request now refreshes into a temporary
snapshot and only replaces the visible catalogue when the refresh succeeds or
returns application entries. A transient post-stop application-list failure
therefore shows `RECONNECTING` while retaining the existing game cards instead
of collapsing the Games page to an empty state. The complete `make check` gate
passed with 14 GoogleTests and 14 Python integration tests. The folder candidate
was deployed to `PPSA77003`; its `eboot.bin` SHA-256 is
`0f042240680ad4dba333fa1b8eeb93136c936b747690ba3af7507bb7f245f8ef`.
The title entered its native executable, remained alive during the bounded
observation, closed normally, and left FTP, klog, and elfldr reachable. This is
a `partial-pass`: startup and cleanup are hardware-proven, while the original
launch, return, then stop interaction remains the operator acceptance step.

Commit `9f981f7` coalesces each PS5 pad batch to its newest timestamped sample
before forwarding controller state to Sunshine. This matches the project's
validated native-input recipe and Moonlight Qt's practice of batching pending
axis motion before sending the resulting current state. It prevents stale pad
states from being replayed in a tight burst after a delayed poll. The same
candidate changes the HUD text and RTT refresh cadence from roughly four times
per second to roughly once per second, matching Moonlight Qt while retaining
per-frame counters and composition. The complete `make check` gate passed with
14 GoogleTests and 15 Python integration tests. The deployed `PPSA77003`
`eboot.bin` SHA-256 is
`3fcb7763cd7e59e4f3f9baebe7407d981c78526017ca2b61bb00fea4a606a278`.
The title entered its native executable, survived the bounded observation,
closed normally, and left FTP, klog, and elfldr reachable. This is a
`partial-pass`: lifecycle is hardware-proven; in-game D-pad navigation remains
the operator acceptance step.

The HDR launch-handshake candidate adds Moonlight Qt's standard HDR capability
parameters to both launch and resume requests whenever the selected stream
format is 10-bit. SDR requests continue to omit the HDR block, while every
request carries the selected width, height, frame rate, and SOPS flag so
Sunshine can apply its automatic display configuration. The complete
`make check` gate passed with 14 GoogleTests and 16 Python integration tests.
The folder candidate `eboot.bin` SHA-256 is
`08e30eddd7fa48f081d524133384a509e3fd8cba4d2d7113f540cd4e96328d3f`.
Windows exposes an HDR toggle for the selected VDD, so the remaining acceptance
step is to confirm that an HDR stream makes Sunshine enable it and negotiate
HEVC Main10/Rec.2020 on PS5.

The first `01.000.025` HDR reproduction completed that host-side acceptance
step. Sunshine's session log recorded a 1920x1080 capture in
`DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020`, 10 bits per color, and an
`hevc_nvenc` stream identified as HDR (Rec. 2020 + SMPTE 2084 PQ) and 10-bit.
The client disconnected roughly three seconds later. ProsperoLight was
terminating the stream when the first decoded frame arrived before
moonlight-common-c's asynchronous HDR control packet had updated `hdrActive`;
the pinned dependency explicitly permits that state to be stale for several
frames during transitions.

Commit `e0f52eb` changes the renderer gate to tolerate an unknown initial HDR
state. It still rejects a stream after the control channel explicitly confirms
HDR is disabled, or when decoded frames are not Rec.2020. A regression test
covers the delayed-confirmation case, and the complete `make check` gate passed
with 14 GoogleTests and 17 Python integration tests. Content version
`01.000.026` (commit `e33961c`) was deployed in folder form to `PPSA77003`; its
`eboot.bin` SHA-256 is
`d85061697b2df4f16ce6e338f90148d100b25c5a62ce4a4f5dc0ae5f8b61436e`.
This is a `partial-pass`: Sunshine/VDD HDR negotiation and the PS5 deployment
are proven; visible video and the HDR metrics overlay await operator acceptance.

The completed `01.000.026` HDR acceptance decoded and displayed the Windows
sign-in screen in native HDR10. The initial metrics overlay created its SDR HUD
shader by relocating the already-relocated Main10 shader header, causing
`sceAgcCreateShader` to reject it. Giving the HUD its own header and SDR color
conversion resources fixed both shader creation and the temporary solid-color
overlay. The operator confirmed that HDR video and readable metrics now work
together. These fixes retain the exact Videodec2 output pointer as AGC input.

The `01.000.027` image-quality candidate made VideoOut geometry a property of
the selected stream. A 1080p stream registers two 1920x1080 scanout buffers;
1440p and 2160p streams request the public 4K-buffer privilege and register two
3840x2160 buffers. The loading handoff, AGC viewport, television-safe inset,
metrics HUD, and stream keyboard all consume the same geometry. The complete
`make check` gate passed with 16 GoogleTests and 19 Python integration tests,
and both signed containers passed integrity validation. Hardware acceptance is
pending: launch one 1080p, one 1440p, and one 2160p stream, verify the complete
frame and readable HUD on the TV, then confirm that 2160p text is materially
sharper than the previous 1080p scanout path. Both high-resolution modes instead
produced a black frame before the HUD. Sunshine logs proved that the requested
2560x1440 and 3840x2160 HEVC streams were active, isolating the failure to the
PS5 presentation boundary.

The console's `libSceVideoOut.sprx` decompilation then exposed an ABI error in
the recovered prototype: `sceVideoOutAddBuffer4k2kPrivilege` requires the open
VideoOut handle. Version `01.000.027` declared it with no parameters and passed
an undefined first argument. Version `01.000.028` corrected that ABI, but a
telemetry-off hardware run still negotiated a `3840x2160` HEVC Sunshine session
and disconnected after roughly ten seconds without presenting a frame.

The hardware-accepted `AGC_4K` reference registers its two 32 MiB
`3840x2160` buffers directly and does not call
`sceVideoOutAddBuffer4k2kPrivilege`. Version `01.000.029` therefore removes
that unproven additional gate and follows the accepted VideoOut sequence:
open, set the flip rate, allocate/map the aligned buffer pool, set attribute 2,
then register buffers 2.

The resulting `01.000.029` folder build completed the full hardware matrix on
August 29, 2026. Sunshine and the on-screen HUD independently reported the
negotiated stream dimensions, while the PS5 system log reported a 3840x2160
VideoOut status for both high-resolution cases:

- `1920x1080` HEVC decoded in VideoDec2 and rendered through a 1920x1080 target;
- `2560x1440` HEVC decoded in VideoDec2 and GPU-scaled through a 3840x2160 target;
- `3840x2160` HEVC decoded in VideoDec2 and rendered 1:1 through a 3840x2160 target.

Each controlled session remained connected for roughly 30 seconds, displayed
the Windows sign-in image and readable metrics HUD, then disconnected cleanly.
Sunshine recorded zero network drops in the observed frames. The 1440p path
uses the native texture probe's filtered sampler word (`0x09500000`); its
explicit nearest-neighbour variant is `0x08000000`. Version `01.000.030` names
and regression-tests that distinction and shows the actual VideoOut target in
the HUD. Its complete gate passed 17 GoogleTests and 21 Python integration
tests; the signed folder `eboot.bin` SHA-256 is
`e228e40a37df9c2dc5ac6e09fdd44a88a4d82afb007dcf9796c99c8310e89756`.
The deployed build then repeated both 1440p and 2160p sessions, with the HUD
showing `Output: 3840x2160` in each case. This completes the native
higher-resolution image-quality milestone; long gameplay and cross-hardware
performance remain release acceptance work.

A follow-up codec matrix switched the same deployed build from HEVC to H.264
without changing the output code. Sunshine recorded `h264_nvenc` at both
`2560x1440` and `3840x2160`; the HUD reported `Codec: H.264`, VideoDec2
hardware decode, and `Output: 3840x2160`. Both sessions displayed the complete
Windows image for roughly 30 seconds and disconnected cleanly. Native 1440p
and 2160p presentation is therefore hardware-proven for both selectable video
codecs.

Version `01.000.031` refreshes only the launcher palette, using the existing
icon's sapphire, sky-blue, and pale moonlight colors without changing layout or
streaming behavior. Commit `ee6d72e` passed the complete `make check` gate (17
GoogleTests and 21 Python integration tests), was deployed in folder form to
`PPSA77003`, and displayed the Games screen with the new surfaces, borders,
focus state, unchanged geometry, and visible version string. The title then
closed cleanly and FTP, klog, and title-control services remained available.
This bounded visual case is a `pass`; evidence is stored locally at
`results/PPSA77003-01.000.031-palette.png`. The deployed `eboot.bin` SHA-256 is
`e228e40a37df9c2dc5ac6e09fdd44a88a4d82afb007dcf9796c99c8310e89756`, and
the deployed RCSS SHA-256 is
`01127b74b1c6af08ca539404ca6d5f8a39c1f032b2c6b722292f998c848bcbc8`.

Version `01.000.032` adds direct PS5 USB keyboard and mouse forwarding during
an active Sunshine stream. Commit `29e6710` passed the complete gate with 20
GoogleTests and 21 Python integration tests, native import validation, signing,
SELF integrity, and PFSC integrity. The signed folder `eboot.bin` SHA-256 is
`4148f67593760c97d5847f2fcbe24d5c044e747374932be2f961a12eb909bb16`;
the `PPSA77003.ffpfsc` SHA-256 is
`55dcd16c35f9282526ba72c3d19c7af4ff903d904dbc5837e84595c1ce3836d6`.
The folder candidate was deployed to `PPSA77003`, and remote metadata reported
the expected `01.000.032` content version. A bounded console cycle reached the
title-specific start and stop checkpoints without a crash candidate, then left
FTP, klog, and title-control services reachable. This is a `partial-pass`:
build, deployment, launcher lifecycle, and cleanup are proven; physical USB
motion, buttons, wheel, typing, modifiers, and reconnect still require the
interactive Sunshine-stream acceptance step. Evidence is stored under
`results/usb-input-032/`.

The subsequent active-stream test exposed a deterministic crash in
`01.000.032`: its new input path called the keyboard and mouse libraries without
first loading sysmodules `0x0106` and `0x00a9`, assumed both devices used index
zero, and sampled keyboard state instead of draining transition records. Version
`01.000.033` replaces that shortcut with the device-validated contract from the
native input investigation: load each module before initialization, retain every
accepted device index, drain bounded keyboard and mouse batches oldest-first,
neutralize intercepted or disconnected samples, and close every retained handle.
The complete gate passed with 20 GoogleTests and 22 Python integration tests,
native import validation, signing, SELF integrity, and PFSC integrity. The
folder candidate was deployed to `PPSA77003` and completed two live Desktop
sessions through Chiaki-NG. Captures confirmed a 2560x1440 H.264 stream rendered
through the native 3840x2160 output with the metrics overlay; both sessions
remained alive beyond the former deterministic crash boundary and closed through
the title controller. The user then verified physical USB mouse and keyboard
input on the streamed Windows login screen. This is a `pass`. The signed
`eboot.bin` SHA-256 is
`0027357a8d78933992d22e24d5f53a1c60f75f5b48414f4aaec24ee911f311d5` and the
verified `PPSA77003.ffpfsc` SHA-256 is
`41f164a9785874f2fd2fbcdb0f52ca9926aa6cadf8179d569a0115ad69782a14`.
Local screenshots, klog, and ShadowMount evidence remain under `results/` and
are intentionally excluded from commits.

Version `01.000.034` removes the launcher Diagnostics section, its focus path,
runtime text updates, styling, and unused panel texture. L1/R1 now cycles only
PCs, Games, and Settings through `Screen::Count`; all content focus indices were
shifted with the shorter navigation list. The complete gate passed with 20
GoogleTests and 23 Python integration tests, including a regression check that
the Diagnostics page cannot return to the UI. Native signing, SELF integrity,
and PFSC integrity also passed. The folder candidate was deployed over WSL to
`PPSA77003`, launched for a bounded smoke interval, and closed through the exact
title controller. FTP, klog, and title-control services remained reachable.
The signed `eboot.bin` SHA-256 is
`2018fe9901e16b6c7652f8453fc61e848dce2cfc4d7e925af73ded81bbc15c8b` and the
verified `PPSA77003.ffpfsc` SHA-256 is
`0db6f18b4bbe7ed8d2bdb2aae6b2a9b9b0536221b8a2f3028024e1823d9e4598`.

Version `01.000.035` adds eight responsive launcher sound effects through the
hardware-validated SDL PS5 audio backend: opening, navigation, confirmation,
setting change, back, success, error, and stream start. The supplied four-second
opening sound is preserved at its original duration and plays only on initial
process startup. The stream-start sound may continue over connection setup, but
the Moonlight audio callback closes the UI device before opening native streamed
AudioOut, preventing the two paths from competing. Asset validation now requires
every cue to be complete 48 kHz stereo signed 16-bit PCM. Static analysis, 20
GoogleTests, and 23 Python integration tests passed; the native folder and PFSC
integrity checks also passed. The folder candidate was deployed over WSL to
`PPSA77003` and remained alive for a bounded ten-second launcher smoke test until
the exact title controller closed it normally. Audible cue acceptance remains a
manual operator check. The signed `eboot.bin` SHA-256 is
`6bb315e8bbef5606007868c9924842d317acc85c9583c603e1288aef26e28126`; the
verified `PPSA77003.ffpfsc` SHA-256 is
`2eb3ee54ff9239c8ee0112a96b8c19660d708a03582f0d80a420bb132c22ee23`.
Bounded klog evidence is retained locally under `results/` and excluded from
commits.

Version `01.000.036` fixes the launcher-to-stream regression introduced by the
first sound-effect integration. PS5 klog and the unstripped ELF resolved the
faulting stack to `PS5AUDIO_CloseDevice`, called first from Moonlight's audio
worker and then reproducibly from the launcher thread. The UI audio device is
therefore process-scoped: its queue is cleared before streaming, SDL tears down
only video, and Moonlight opens its independent native Opus/AudioOut port. This
retains launcher cues without invoking the faulty SDL device-close path.
The complete lint gate, 20 GoogleTests, and 24 Python integration tests pass,
including a source-level lifecycle regression check. Two bounded console runs
then remained stable until normal controller shutdown. The visual run rendered
a live 2560x1440 HEVC Sunshine stream through the native 3840x2160 output with
the metrics overlay active and zero network-dropped frames; klog contained no
application-crash event. The signed `eboot.bin` SHA-256 is
`c82d7070d3b40083e553fb058c306e363c1f9861fb19ba5e66189d47cee8f8e3`; the
verified `PPSA77003.ffpfsc` SHA-256 is
`611de80729bd4d03772b587cfec25f070252eda9ca7f245e2f8b7e6802c7ba72`.

Version `01.000.037` aligns the first content block on PCs, Games, and Settings
to the same 164-pixel top coordinate while preserving each screen's internal
spacing. A tooling regression check prevents those three anchors from drifting
independently again. The complete lint gate, 20 GoogleTests, and 25 Python
integration tests pass, and the native folder and PFSC integrity checks report
no errors. The folder candidate was deployed over WSL to `PPSA77003`; a bounded
Chiaki launcher smoke test opened the updated Settings screen, displayed
`v01.000.037`, and closed normally through the exact-title controller. The signed
`eboot.bin` SHA-256 is
`c82d7070d3b40083e553fb058c306e363c1f9861fb19ba5e66189d47cee8f8e3`; the
verified `PPSA77003.ffpfsc` SHA-256 is
`7e42cface7855cb075912f417ba787d0f5621a5167e71214b64895096785a563`.

Version `01.000.038` aligns the Games action row to both outside edges of the
selected-app panel while retaining the existing button assets and spacing. The
complete lint gate, 20 GoogleTests, and 26 Python integration tests pass; native
folder and PFSC integrity checks report no errors. The folder candidate was
deployed over WSL to `PPSA77003`. The signed `eboot.bin` SHA-256 is
`c82d7070d3b40083e553fb058c306e363c1f9861fb19ba5e66189d47cee8f8e3`; the
verified `PPSA77003.ffpfsc` SHA-256 is
`727e9a3b2a95c040c0e2df8080ffcc5b8fffd91b8662d0df9fc1a5d85862509d`.

Version `01.000.039` gives the stop action a rendered pending state before its
synchronous Sunshine request, identifies HDR on the metrics overlay's first
line, and extends HEVC Main10 HDR selection to 1080p, 1440p, and 2160p. HDR now
survives resolution changes and persisted configuration loading instead of
forcing 1080p. The complete lint gate, 20 GoogleTests, and 29 Python integration
tests pass; native folder and PFSC integrity checks report no errors. The folder
candidate was deployed over WSL to `PPSA77003` without opening Chiaki. The
signed `eboot.bin` SHA-256 is
`ad6baf737e889b4c2c709c29deabce5826fd9319fc24d6525020f65b44d6ae12`; the
verified `PPSA77003.ffpfsc` SHA-256 is
`fc2c079d243070df67fb1aaceb818251f469a75b57c5b2e53d4beaa08faff005`.
Live 1440p and 2160p Main10 acceptance remains an operator test because this
deployment deliberately did not initiate a Remote Play session.

Version `01.000.040` fixes the first 1440p/2160p Main10 hardware acceptance
failure. The decoder and HDR VideoOut path were active, but the AGC Y and UV
texture descriptors still encoded the proven 1920x1080 view. Their dimension
fields now follow the runtime visible resolution while retaining the validated
Main10 formats, swizzle, shader, and color conversion. A source regression
check covers the dynamic luma and chroma dimensions. The complete lint gate,
20 GoogleTests, and 30 Python integration tests pass. The folder candidate was
deployed over WSL to `PPSA77003` without opening Chiaki. The signed `eboot.bin`
SHA-256 is
`40d384f5312b0e0aa8f1110642703b405e92708a84a7bd46e6af9fd818f4f567`; the
verified `PPSA77003.ffpfsc` SHA-256 is
`e07e86c5734371a184e7a90e0462b8d4320b46af55ce588a62f64a91d9c871b7`.
Live 1440p and 2160p Main10 image acceptance remains the operator test.

Version `01.000.044` validates native high-refresh VideoOut independently of
Sunshine. The 90 FPS path requests the proven 119.88 Hz mode and unpegs VRR;
the 120 FPS path retains fixed 119.88 Hz output. A three-surface bounded
presenter waits for a completed flip before a decoder surface is reused while
allowing high-refresh submissions to proceed without serializing every frame
on its own vblank. On hardware, the isolated 18-second oracles measured 89.99
FPS and 119.85 FPS respectively, with all requested frames presented, clean
AGC/VideoOut teardown, no GPU fault or kernel panic, and ports 2121, 3232, and
9021 still healthy. The 120 FPS animated-loading control measured 109.33 FPS,
which isolates that lower figure to regenerating and flushing the complete
1080p loading surface rather than to a PS5 GPU or VideoOut ceiling. A bounded
50-second live-path smoke then entered 1920x1080 at 119.88 Hz, remained alive,
and closed normally without a GPU fault or kernel panic. Because LAN telemetry
was disabled for that run, this proves the high-refresh presenter handoff but
does not yet prove that Sunshine delivered 120 decoded frames per second. The
matching Sunshine log independently records a 120 Hz captured display and a
requested frame rate of exactly 120 FPS. Its subsequent 60 FPS minimum target
is Sunshine's documented static-content floor of half the requested rate, not
a negotiation cap; moving-content cadence remains the operator acceptance.

Version `01.000.045` established that the public HFR contract used by a retail
PS5 title is ordinary `sceVideoOutConfigureOutput` request 15 with the HFR bit
set in `attribute3`; it does not use a restricted system or VR output API. The
legacy metadata preset selects a 1920x1080 119.88 Hz VideoOut surface.

Version `01.000.046` completed the high-resolution HFR matrix on August 30,
2026. Isolated five-second presenter tests produced all requested frames with
clean teardown: 2560x1440 at 89.99 FPS and 3840x2160 at 119.88 FPS. Subsequent
bounded live Sunshine sessions confirmed a 2560x1440 desktop captured at 90 Hz
with an exact 90 FPS client request, and a 3840x2160 desktop captured at 120 Hz
with an exact 120 FPS client request. Those initial sessions used the legacy
1920x1080 119.88 Hz HFR output while VideoDec2 retained the negotiated source
resolution and AGC performed the final scale.

The subsequent high-resolution entitlement probe replaced the legacy VRR bit
with `attribute3=0x80040` and retained ordinary VideoOut request 15. A bounded
native presenter run registered two 3840x2160 buffers, switched the physical
output to 3840x2160 at 119.88 Hz, presented all 600 frames at 119.87 FPS, and
returned to 59.94 Hz on teardown. Ports 2121, 3232, and 9021 stayed healthy;
there was no kernel panic or GPU fault. This establishes the public native
4K/120 presentation contract without a restricted VideoOut or VR API.

A second oracle used a true 3840x2160 source surface rather than a lower-
resolution source scaled into the 4K target. It again registered 3840x2160
buffers and presented all 600 frames at 119.87 FPS with clean AGC shutdown,
unmap, and release. This separates native 4K source processing from the
VideoOut timing result and confirms both parts of the 2160p/120 path.

Firmware 12.70 was independently validated with the same true-4K-source
oracle under an isolated `PPSA77120` development-folder title. VideoOut
reported `2160P_11988`, AGC registered 3840x2160 buffers, and all 600 frames
were presented at 119.88 FPS before returning to 59.94 Hz. The process exited
cleanly, no kernel panic or GPU fault appeared, and ports 2121, 3232, and 9021
remained healthy after the cycle.

The first diagnostic wrapper then restored and relaunched the production title
before ShadowMount reported that the oracle's runtime layers were released,
which exposed a post-test mount race and powered down the console. The oracle
had already exited cleanly and production files had already been restored. The
wrapper now waits for the title-specific `runtime layers released` marker
before restoring or relaunching, and production deployment remains a separate
bounded cycle.
