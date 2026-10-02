# C++20 boilerplate port

This repository began as a clean clone of
[`ps5-native-app-boilerplate`](https://github.com/blackbearreloaded/ps5-native-app-boilerplate)
at commit `a15ab71d1a5ba6d37c6af28f65bb51520a588005`.

The ProsperoLight application logic was brought over from the earlier native
Moonlight prototype without copying its build system or runtime. The port uses
the boilerplate's C++ startup path, dependency setup, packaging, asset
validation, deployment tooling, and test/lint workflow.

Ported application areas:

- OpenGL launcher, presentation assets, and metadata.
- Sunshine discovery, pairing, app browsing, and stream lifecycle control.
- VideoDec2-to-AGC GPU presentation, Opus AudioOut, and DualSense input.
- Metrics overlay, mouse emulation, HDR preference, and stream recovery UI.

The title keeps the hardware-proven main application attribute `0x62000000`
(`1644167168`). That metadata is required for the public HDR-capable VideoOut
profile in both game and media categories; replacing it with the boilerplate
default of zero is a functional regression.

The legacy gamestream protocol sources remain C because they are a bounded,
well-tested compatibility dependency. Rewriting that transport in C++ would
add risk without improving the PS5 presentation path. New application work
belongs in C++20 unless it must match an upstream C ABI.

Completion requires the host checks and fresh-title hardware smoke tests in
[VALIDATION.md](VALIDATION.md). Until those pass, a successful package build is
evidence of link correctness, not a hardware release sign-off.
