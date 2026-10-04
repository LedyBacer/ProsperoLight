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

The stream thread is the only pad-output writer. Network callbacks update
bounded per-slot state under a mutex; PCM uses try-lock and drops contended
packets rather than delaying the control channel. Teardown clears vibration,
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
- Vibepollo's versioned haptic PCM control packet is validated and routed by
  controller number. **Current output is an RMS actuator envelope through
  `scePadSetVibration`, not native waveform playback.** Out-of-order packets
  are dropped, feedback expires after 60 ms, and ordinary rumble is restored.
- Feedback receipt counters and native API failures in session logs.

## Explicit limits

Native high-fidelity haptic waveform playback still needs a verified public PS5
output ABI. This build does not claim parity with a PC client's direct DualSense
HID waveform path. No undocumented audio-port numbers or raw output-report ABI
are guessed. Battery percentage is not present in the verified normal pad ABI;
battery capability is not advertised and unknown state is sent on arrival.
The PS button and microphone mute remain owned by PS5 system software.
Microphone capture/controller-speaker routing is not part of Moonlight's
controller event protocol and is not implemented here.

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

No per-feature hardware tests were performed during implementation. Compilation
and packaging do not establish pad API permissions or effect fidelity on PS5.

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
9. Check a native-haptics game separately. `pcm_envelope` should increase, but
   assess this as approximate vibration, not waveform-fidelity validation.

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
