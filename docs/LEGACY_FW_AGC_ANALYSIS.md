# Legacy firmware AGC investigation — round 2

## Evidence received (round 1, FW 3.00)

Reviewed the supplied seven-variant archive, SHA256
`560af8159e4d0316785a35b6d7c6232272386b8894094ea4e995696fae5ac4a4`.
The log identities name source `9aad3d3a095a075c7cb29c1d300df461752500f0`
and SDK patch identity `ps5_opengl_1_0_2_fwcompat_6f8b53411000`.
Raw logs are intentionally not committed or uploaded.

| Variant | Queries returned | AGC init | Defaults | Recorded outcome |
|---|---|---|---|---|
| 00-control | All four: 0 | One call, 0 | NULL | GRAPHICS_FAILURE |
| 01-no-warmup | Skipped | One call, 0 | Non-NULL, 489 registers | LAUNCHER_READY; operator saw menu |
| 02-no-videodec2 | Skipped | One call, 0 | Non-NULL, 489 registers | LAUNCHER_READY; operator saw menu |
| 03-no-shader-cache | All four: 0 | One call, 0 | NULL | GRAPHICS_FAILURE |
| 04-force-1080p | All four: 0 | One call, 0 | NULL | GRAPHICS_FAILURE |
| 05-no-pyrowave | All four: 0 | One call, 0 | NULL | GRAPHICS_FAILURE |
| 06-combined | Skipped | One call, 0 | Non-NULL, 489 registers | LAUNCHER_READY; operator saw menu |

The failing attempts each recorded three renderer failures and no ready frame.
The supplied report states no fresh crashes, normal closing, and no streaming.
It describes an initial OS launch refusal for 04, with unchanged logs, followed
by the logged graphics failure. Do not count that stale log as the first attempt.
Operator environment: FW 3.00, UMTX2 (reported latest), kstuff-lite
(echostretchfork v1.11), ShadowMountPlus v1.7-beta5fix1, 4K60 TV with HDR on.
These environment details and visible menus are operator reports, not measurements
performed by the developer. FW 4.50 remains a reported target, not this dataset.

This establishes a warm-up correlation, not which query changes AGC state.
A zero init return is demonstrably insufficient to establish usable defaults.
PyroWave, cache and output resolution changes did not remove the correlation.
No streaming or performance result follows from a working menu.

## Controlled experiments and interpretation

All four new artifacts use the same SDK source and patch, derived from checksum-pinned
OpenGL 1.0.2/source `a519561626933257213d64a4a8bd5e02b55dceb2`.
Production defaults and the original seven selectable variants are retained.
Only 09 invokes the new early hook. It uses the SDK's existing API table,
`init(uint32_t)` with value 8, and actual `runtime_agc_initialized` state.
It logs defaults immediately afterwards and after warm-up. Failed initialization
is not retried or treated as success; a sticky failure prevents rendering.
The regular renderer logs whether it reused early initialization.

| New hardware observation | Interpretation / next step |
|---|---|
| 07 fails; 08 passes | Compute query independently sufficient under these conditions |
| 07 passes; 08 fails | At least one codec query implicated; isolate it next |
| Both pass; 00 fails | Interaction/cumulative initialization implicated |
| Both fail | Multiple triggers or common transition remain possible |
| 09 visible menu, complete warm-up, one init, later reuse | Ordering is a promising workaround; streaming unverified |
| 09 fails before warm-up | Investigate early prerequisites and pinned init contract; no guessed ABI change |
| 09 defaults valid before queries, NULL afterwards | Direct evidence of state transition during warm-up |
| New control unexpectedly passes | Recheck launch conditions and instrumentation; do not apply old comparison blindly |

A codec query error is reported individually and cannot be hidden by launcher
success. 07/08 retain module preload but refuse forwarded and menu stream starts.
09 keeps full stream code, but all external testing is launcher-only for now.

## ABI review and optional probe

[OpenAGC](https://github.com/OpenAGC/OpenAGC/blob/main/src/game_compat.c) and
[AnyPS5](https://github.com/boykopovar/AnyPS5/blob/main/core/libs/prx/libSceAgc/DcbState/src/Init.cpp)
show distinct entry points for one-argument init and `_0090` state/version use.
[Orbistoun observations](https://github.com/project-oops/Orbistoun/blob/main/crates/orbistoun-hle/data/knowledge/libSceAgc.toml)
also distinguish their NIDs; their annotations are not proof of the FW 3.00 ABI.
The [ps5-vulkan caller](https://github.com/mpereiraesaa/ps5-vulkan/blob/main/native/platform_ps5.c)
uses its own two-argument contract. None justifies globally replacing the pinned
SDK's signature or treating `0x8A6C0004` as universally “already initialized”.

The optional `sceAgcGetRegisterDefaults2(uint32_t)` probe is omitted: a reliable
optional resolver for this native-title path has not been established. Adding a
mandatory old-firmware import or using a guessed ABI would spoil the experiment.
No emulated/default register tables are substituted. Existing NULL, absent-bank
and missing-required-register guards remain; a non-NULL pointer alone is not
proof of firmware ABI compatibility.

## Verification boundary

Focused host tests execute the actual warm-up function with query stubs.
The patched SDK's real target-state, early-hook and later reuse code execute
under ASan/UBSan in a host harness.
They cover subsets, order/configuration, error propagation, NULL handling, early
failure and one-init reuse. Compile-time tests retain production defaults.
Build/ZIP provenance verifies software artifacts, not firmware compatibility.
New round-2 physical results are pending tester logs **and** visible-menu reports.
No new console deployment, release, main merge or performance test is part of this task.
