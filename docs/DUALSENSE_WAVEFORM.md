# Native DualSense waveform validation — 2026-10-15

This records the original MVP. See the [buffering follow-up](DUALSENSE_WAVEFORM_BUFFERING.md)
for subsequent fixes, counters and final validation status.

## Build and scope

Branch `codex/dualsense-waveform` starts at main `ca3f25f` (01.000.110).
The tested native implementation is commit
`10aa798a5d2388f300a2ee002845b73595f8f0ea`, built with OpenGL SDK 1.0.2,
label `test-lab-waveform-mvp`, eboot SHA256
`82be4628de53f4c37334ed6f4f8b1ef3dd319d83ebe2baa105b72ae7464a7efb`.
Subsequent documentation/host-diagnostic changes do not change that binary.
Speaker and microphone work is deferred by the operator.

## Native API and transport evidence

Real PS5, Vibepollo 2.0.1, USB composite DualSense (`usbip_ds5`), native HEVC,
SDR 1080p60, configured bitrate ceiling 20 Mbps. This is integration validation,
not a performance comparison or a physical latency measurement.

- Registered app: `sceAudioOutOpen(user, 10, 0, 256, 48000, 1)` returned
  handle `0x200a0019`; `sceAudioOutGetPortState` returned 0, channels 2.
  Format 1 is the public ABI's signed 16-bit stereo format. The pad owner selects
  public haptics mode 1 before PCM; legacy rumble uses mode 2.
- Standalone payload probe: foreground user was available, but native pad open
  returned `0x809b0081` and audio open `0x80260011`. This payload-context failure
  does not establish unsupported hardware: the registered app succeeded.
  No nonzero standalone-probe signal was executed.
- Run `78122abc-b818-4350-b23a-df11fbaa734b`: execution COMPLETED, functional
  PASS, measurement VALID, overall PASS. All **133** managed installation files
  matched final-destination SIZE/SHA256 readback. Native port ready; no PCM source
  was played in this first run.
- Run `da7651aa-02b2-4cc1-a946-418d7a727aa1`: same installed binary, no redeploy;
  COMPLETED/PASS/VALID/PASS. The Windows diagnostic queried the exact four-channel
  `WAVEFORMATEXTENSIBLE` format and played 26,880 frames. Speaker channels 1/2
  remained zero; actuator channels 3/4 contained low-level left/right/both PCM
  and trailing silence. Windows returned the buffer successfully.
- PS5 counters after that signal: **RX 116, drop 6, duplicate 0, reorder 0,
  gaps 0, underrun 50, contention 0, output errors 0**; native ownership ended.
  This establishes arrival and worker processing, not lossless or physically
  accurate rendering. `drop` combines overflow/stall/rejection counters; this
  run cannot distinguish their individual causes. `underrun` includes deliberate
  zero fill and the silence tail. The six drops remain a tuning/diagnostic limit.
- Both completed runs verified matching artifact manifests/export hashes,
  normal title exit, released remote lease, zero host clients/RTSP sessions,
  and unchanged saved configuration/pairing. The second run reused the first
  installation readback and separately checked installed build identity.

Local evidence lives under ignored `build/dualsense-waveform/`; raw artifacts
contain private environment data and are not published. First raw report/export
SHA256: `44eb2e41a041d77baf2f4405a9874a62042d396f8b9ce3f7dc159cd198dc3522` /
`a8ec32549b22f1ae821a07e06f93fb0786ecfa67faae287ec7d87050a540fefb`.
Transport raw report/export SHA256:
`0a7fd663d4ec8da369d227420e57f3d330e6436bdab8c0fae581920d58a0ddbb` /
`5cbb25b630622f02e6cd14126b7070c5aa4463feb40babf673a3a1e2d3296e4a`.

A third run, `e9e4c247-0292-49a2-9fe0-c416f6b21b7a`, completed
PASS/VALID on the same binary. The operator confirmed **physical vibration**,
but could not distinguish the short left/right/both segments. This is a human
observation, not a measured polarity/frequency-response or channel-isolation test.
It logged RX 115, drop 6, gap 1, contention 1, underrun 62, output errors 0.
Artifact integrity, title exit, lease/session cleanup and unchanged config/pairing
were checked. Its initial CLI export failed because the helper executable was
absent from PATH; the completed run was exported offline with the same bundle
function and verified, without rerunning the hardware test.

The explicit stronger/longer run, `8cd8f4ae-be95-47de-af74-113b60d365c1`,
completed PASS/VALID on the same binary. The operator confirmed **three distinct
impulses and distinguishable left/right sides**. The original synthesized source
used 1.5s segments, 1s gaps, peak 6000/32768, with speaker channels always zero.
PS5 counters: RX 1401, drop 81, duplicate/reorder 0, gaps 3, underrun 558,
contention 3, output errors 0. Native ownership returned inactive. These counters
show the bounded queue is dropping data under this workload: the result is a
working MVP, **not acceptance of lossless waveform fidelity**. Separate
burst/timing diagnostics and queue tuning remain before fidelity sign-off.
The final run also independently verified artifact/export integrity, idle title,
released lease, zero host clients/RTSP sessions and unchanged config/pairing.

## Queue, negotiation and fallback

See [DUALSENSE.md](DUALSENSE.md) for ownership and capability details. Per-controller
queues preserve signed stereo samples, reblock 240 into 256 frames, cap storage
at 768 frames (16ms), discard discontinuities/backlog, and zero-fill underruns.
Startup target is 5.33ms of PCM; this is not measured end-to-end actuator latency.
Callbacks only copy into owned bounded storage with try-lock; native output runs
on per-controller workers. A stalled driver cannot block network/pad/video threads.

ML_FF PCM is advertised only if a sink is ready within the bounded startup
preflight; controller PCM capabilities require that controller's ready port.
Failure withdraws local readiness and restores remembered legacy rumble.
Adaptive triggers/RGB retain their existing paths. Reconnect is needed if the
native port becomes ready after connection-level negotiation.

## Checks and remaining hardware matrix

Linux builder: `make check TEST_LAB=1 BUILD_LABEL=test-lab-waveform-mvp`
with tidy separately, full `make tidy`, and `make test-stream-performance` passed.
The batch included 64 GoogleTests, controller/performance/runtime guards,
203 Python tests (199 pass, four unavailable-Node skips), and native app build.
Additional focused wire-validation cases and the Windows source's portable
sample/channel tests (default and explicit stronger preset) passed. Scalar/SIMD FEC/Opus comparisons and five upstream
Opus tests per variant passed. Windows exact-format query/playback also succeeded.

Initial failures were corrected: missing host TurboJPEG/WebP development packages,
namespace alias, generated AppleDouble files, and a whitespace-sensitive source
assertion. These are not silently counted as successful initial attempts.

| Case | Current evidence |
|---|---|
| Vibepollo 2.0.1 USB composite DualSense | Native API and PCM receipt verified on PS5; Native PCM, physical vibration and left/right distinction confirmed; lossless fidelity pending |
| VHF DualSense without composite audio | Existing fallback preserved; no new hardware run |
| DS4 / Xbox; Vibepollo 2.0.0; Sunshine; Apollo | Host/controller mocks and existing paths; no new hardware run |
| Multiple controllers, slot reuse, driver failures, contention | Deterministic mocked API tests; no new multi-controller hardware acceptance |
| Synthesized left/right/both | Operator confirmed three impulses and distinct sides |
| Native game effects, triggers/RGB together | Operator acceptance pending |
| Controller/host reconnect, PS5 overlay | Lifecycle mocks; hardware acceptance pending |
| PyroWave, 4K120, HDR/VRR | Not tested for this change |

## Reproduction

Build using the repository's pinned dependencies and normal `make check` workflow.
For guarded deployment/normal exit/recovery use [TEST_LAB_SMOKE.md](TEST_LAB_SMOKE.md).
Start an ordinary stream with Vibepollo's USB composite DualSense option. Confirm
`ready=1`, successful state query and two channels in `DualSense waveform` logs.
Then use the explicit [Windows source](../tools/controllers/probe/README.md#windows-transport-source)
to select the exact DualSense endpoint; never use the default mapper. Record
physical left/right/both observations separately from RX/API counters. Finish the
stream normally and verify no continuing vibration. Do not retry missing receipts
or increase amplitude to compensate for an unverified endpoint.
