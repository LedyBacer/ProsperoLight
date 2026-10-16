# Native DualSense and multiple controllers

This describes the controller code in `src/ps5_dualsense.cpp`. It is independent of
the video codec in use.

## Existing path and new ownership

Previously `moonlight_stream.cpp` opened only the initial PS5 user's pad, sent
controller number 0 with an active mask of 1, advertised only analog triggers,
and left every feedback callback null. Its touchpad chords own local UI,
mouse mode and keyboard input.

`ps5_dualsense.cpp` now discovers the four logged-in PS5 users, preserves stable
controller slots, updates the shared active mask, and handles arrivals,
disconnection, reconnection and user logout. The existing primary-pad input path
retains all local chords. Additional pads send game input without controlling
local UI. PS5 users must assign each extra pad to a different logged-in user or
guest; unassigned pads are not normal application input devices.

The stream thread is the only scePad-output writer. Dedicated per-controller workers
own blocking AudioOut calls. Network callbacks update
bounded per-slot state under a mutex; PCM uses a separate bounded packet inbox
and an atomic producer guard, without acquiring the pad or worker mutex. Teardown clears vibration,
trigger resistance, motion reporting and host lightbar color before closing
owned secondary handles. The existing primary handle remains owned by its
original input path.

## Implemented

- Four independently numbered controllers; analog sticks/triggers, D-pad,
  face/shoulder buttons, stick clicks, Options and the Touchpad button. The host's Back
  and Guide buttons are sent with Touchpad + L3 and Touchpad + R3; the console keeps
  its own Create and PS buttons.
- Two simultaneous touch contacts with stable IDs, normalized coordinates and
  down/move/up/cancel lifecycle. Local UI chords, mouse mode and intercepted
  system input cancel host touches and suppress feedback until focus returns.
- Host-requested accelerometer and gyroscope reporting, capped at 250 Hz.
  Public pad acceleration in g is converted to m/s² and radians/s to degrees/s.
- Independent left/right rumble, RGB lightbar and trigger vibration callbacks.
- Adaptive trigger translation from HID feedback (`0x21`), weapon (`0x25`) and
  vibration (`0x26`) effects into native PS5 commands, including all ten zones.
  Legacy `0x01/0x02/0x06/0x11/0x12` effects use a conservative approximation.
  Firmware debug/unofficial effects are reset to off and logged.
- Vibepollo's validated `0x5601` version-1/format-1 PCM is copied into an
  owned bounded queue and rendered as native S16LE stereo on AudioOut vibration
  port **10**, index **0**, **48000 Hz**, **256-frame grains**. Left/right samples,
  polarity and order are preserved; no RMS/envelope conversion remains.
- Connection-level PCM support is advertised only if a native sink is ready at
  connection startup; each controller additionally advertises `LI_CCAP_HAPTICS_PCM`
  only when its own port opened and the state query reports two channels. Late
  startup beyond the 50ms preflight budget needs a stream reconnect to renegotiate.
- Native output failures withdraw local readiness, clear pending PCM and return
  to the host's remembered ordinary rumble after worker cleanup. Ordinary Sunshine,
  Apollo and non-PCM virtual controllers retain existing rumble/triggers/LED paths.
  Host protocol support is not a claim of physically verified waveform fidelity.

## PCM ownership, buffering and interruption

Each controller has a process-lifetime worker and preallocated queue, default
**3072 stereo frames (64ms)**. `PROSPEROLIGHT_HAPTICS_QUEUE_FRAMES` permits 256..4096
frames at compile time; the Balanced default starts with a 720-frame target,
at least 12ms of initial accumulation and a 16ms short-fragment timeout. A bounded packet inbox
keeps reception independent of pad/diagnostic mutexes. Incomplete
initial/final blocks are zero-filled after a bounded wait. The target is not a
measured end-to-end latency guarantee. Overflow discards queued backlog; an output
call stalled over 60ms clears accumulated samples and in-flight backlog on return,
retires its effect token and requires a fresh pad-owner acknowledgement. An absolute sample
clock accounts for time blocked in AudioOut and avoids adding a second delay
after batched driver calls. There is no resampling,
amplitude conversion, stale repeat, unbounded allocation or network-thread I/O.

Sequence arithmetic uses unsigned modular differences: duplicates/backwards packets
are discarded, forward gaps clear prior queued samples. After the nonzero effect drains and 60ms of nonzero inactivity elapses,
native ownership ends. Incoming zeros cannot extend it or restart native output;
the input sequence stays valid across idle silence. The pad thread zeros legacy motors and grants
ownership before the worker's first waveform; adaptive triggers and RGB stay live.
On native completion it restores the most recent host-requested ordinary rumble;
failed mode switches or motor writes remain pending for a later pad poll.
Public duaLib identifies haptics mode 1 and legacy rumble mode 2. The pad owner
selects mode 1 before native preflight/ownership and restores mode 2 for ordinary
rumble. Physical vibration and distinct sides were operator-confirmed; native/legacy
coexistence under game workloads still needs hardware acceptance.

Disconnect, suppression/system overlays, stream stop, user logout and slot reuse
invalidate an ownership epoch and clear PCM. Workers finish/close old ports before
opening a new user's port; a stalled old port never consumes the new epoch's queue.
Stop/disconnect do not wait for blocking AudioOut. A stuck driver can retain its
old port until it returns or the process exits; that slot cannot claim readiness
for a replacement device in the meantime. Counters and port return codes are logged
at the existing ten-second diagnostic cadence (RX/drop/duplicate/reorder/gap,
underrun/contention/output errors, separate overflow/inbox/stale/rejected/stall
counters, padded frames, input/dequeued frame totals, resumed-stream starvation frames and output calls), not on each packet. Counters are app-lifetime.

## Current scope

The [latency/coexistence follow-up](DUALSENSE_WAVEFORM_LATENCY.md) documents
compile-time baseline/experimental profiles, software histograms and the real-PS5
comparison. Balanced reduced software call-start age in the matched smoke; Low
latency recorded resumed-stream starvation. Balanced/profile 1 is now the production default; baseline/profile 0 remains
available as an explicit compile-time override.

Only HD rumble is implemented in this change. Controller speaker and microphone
work is deferred. Vibepollo 2.0.1's actuator-only extension is not repurposed for
other audio; ordinary stereo/5.1 Opus output is unchanged.

## Native API provenance and licensing

The new renderer/probe are original code using public ABI descriptions, not copied
emulator DSP or HID implementations. SDK imports already include Open/Output/Close/
GetPortState; no proprietary headers, kernel modifications or extra import stubs.

- [SharpProspero AudioOut ABI](https://github.com/SvenGDK/SharpProspero/blob/main/src/SharpProspero/Interop/Audio/AudioOut.cs)
- [Porpoise speaker (case-sensitive Main branch)](https://github.com/elripalda/Porpoise-Dolphin-Emulator-for-PS5/blob/Main/src/porpoise_speaker.cpp)
- [Kyty audio reference](https://github.com/KytyPS5/KytyPS5/blob/9e41d8320a2774b303fbad344c2347e8d0292e83/src/libs/audio.cpp)
- [Kyty haptics reference](https://github.com/KytyPS5/KytyPS5/blob/9e41d8320a2774b303fbad344c2347e8d0292e83/src/libs/dualSenseHaptics.cpp)
- [Vibepollo 2.0.1 waveform contract](https://github.com/Nonary/Vibepollo/blob/2.0.1/docs/windows-dualsense-haptics.md)

## Protocol provenance

The original moonlight-common-c revision remains `f900dd4`. A small, repeatable
haptics-only extension is applied by `tools/controllers/apply-haptics.sh` before
building the streaming library and analyzing headers. Its named anchors allow
composition with independent codec patches without replacing the submodule.

- [Moonlight controller APIs](https://github.com/moonlight-stream/moonlight-common-c/blob/master/src/Limelight.h)
- [Nonary haptics wire contract at 927eba9](https://github.com/Nonary/moonlight-common-c/blob/927eba9639f3c9b5037ab45fc0d6486e2a19b39f/src/ControllerHaptics.h)
- [Public duaLib pad and trigger ABI](https://github.com/WujekFoliarz/duaLib/blob/03bad1bea2a36561b520846776f2a07ced6773e0/src/include/duaLib.h)
- [OpenOrbis public pad APIs](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/blob/master/include/orbis/Pad.h)

The haptic wire header retains its original GPL-3.0-or-later notice. The trigger
payload decoder and platform glue are newly written; duaLib is a reference,
not a linked dependency. No PC HID driver or third-party native code is bundled.

## Hardware review

Historical per-feature observations below describe the older controller implementation.
For the new waveform path, see the [dated validation report](DUALSENSE_WAVEFORM.md); compile/API success alone
does not establish physical effect fidelity.

1. Start a stream with one DualSense. Check every button, both sticks and analog
   triggers; verify that Touchpad + L3 sends Back and Touchpad + R3 sends Guide to
   the host. The Create and PS buttons still open the console's own menus.
2. Enable the host's PlayStation/DualSense virtual controller support. In a game
   that outputs effects, check rumble, left/right adaptive resistance and LEDs.
   Inspect `DualSense ...` session-log rows if an effect is missing: distinguish
   absent host commands from native API errors.
3. Use a host controller diagnostic or Steam Input that requests sensors. Check
   gyro direction and accelerometer data. Rates should become nonzero in logs;
   stopping sensor use should return them to zero.
4. Check touchpad taps/drags, two fingers and lift/re-touch. Verify local
   touchpad shortcuts still open keyboard, toggle mouse/HUD and exit the stream.
5. Assign a second pad to a different PS5 user/guest and check that a local
   multiplayer game sees two separate players. Repeat with three/four pads.
6. Connect another assigned pad during streaming, disconnect/reconnect it and
   log its user out. Other players must retain their numbering and input.
7. Open/close the PS5 menu during an effect, then resume. Exit streaming during
   trigger resistance: vibration and resistance must stop, LEDs reset.
8. Switch PyroWave → HEVC → PyroWave and repeat with two controllers. Reconnect
   to the host without restarting the client; no stale feedback should replay.
9. Check a native-haptics game separately. `DualSense waveform` RX/ready counters
   must increase; separately confirm physical left/right waveform effects. API
   acceptance and counters alone are insufficient for waveform-fidelity acceptance.

## Review fixes

Touchpad + L3 sends the host's Back button; Touchpad + R3 sends Guide. Both work for all controller slots and suppress constituent button clicks and touch contacts while held. PS/Create remain system-owned. DualSense connections explicitly select scePad rumble mode (2) for legacy vibration. Nonzero low amplitudes are preserved when converting 16-bit host motor values.

On Windows Vibepollo can emulate DualSense with `gamepad = vhf_ds5`, provided its virtual gamepad driver is installed. `ds4` uses a PS4 device. Reference: https://github.com/Nonary/Vibepollo/blob/master/docs/configuration.md .

## Local pre-PR validation (2026-10-02)

The upstream workflow commands were run in the Linux build container using
Clang 18 and Python 3.13 with its pinned Mbed TLS code-generation dependencies:

- `make lint`: formatting, Clang static analysis, attribution, metadata and shell checks.
- `make test-unit test-performance-guards`: 42 GoogleTest cases, C++ allocation
  runtime, pipeline lifetime, socket handling and mocked thread-placement checks.
- `make test-integration`: 49 host tooling tests.
- `make test-stream-performance`: scalar/SIMD FEC recovery and matching parity,
  five upstream Opus tests per variant, and stereo/six-channel PCM comparison.
- `make libc` and `sha256sum -c libc.prx.sha256`: deterministic runtime rebuild
  and matching checked-in checksum.
- `make ffpfsc FEC_SIMD=1 OPUS_SIMD=1 PERFORMANCE_DETAIL=1 FLIP_POLL_US=200
  INPUT_POLL_US=2000 VIDEO_SLICES_PER_FRAME=8`: independent release build,
  valid FSELF integrity and verified FFPFSC packaging with zero warnings/errors.

These host checks do not prove controller output on PS5. User review separately
confirmed two controllers, rumble, touchpad, RGB, acceleration, gyroscope and
adaptive triggers in the combined PyroWave preview. Three/four-controller use
and the new Back/Guide shortcuts have not yet been confirmed by user review.
