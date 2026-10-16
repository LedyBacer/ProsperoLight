# Waveform latency and rumble coexistence — 2026-10-16

## Status and scope

The operator reported that gameplay generally works well on the previously
installed native backend and subsequently authorized real-PS5 latency comparisons.
That gameplay feedback does not verify the new latency profiles. The current
comparison status is recorded below. **Production now defaults to Balanced/profile 1 by operator decision after this comparison.**
Speaker and microphone work remains deferred.

## Comparison with the requested reference

Reviewed [Nonary/moonlight-qt dualsensehaptics.cpp at f96b052](https://github.com/Nonary/moonlight-qt/blob/f96b05248bc9d458855eef5a265e2a5ed6f2d82e/app/streaming/input/dualsensehaptics.cpp).
Its ingress keeps sequence history across idle silence, updates nonzero activity
separately and ignores idle zero packets. Its Windows USB renderer uses a 480-frame
prebuffer, a 10ms timeout, a 40ms FIFO bound and WASAPI buffer/event scheduling.

ProsperoLight adopts the activity/sequence principle, not that scheduling backend.
PS5 retains signed stereo PCM, native port 10, 48kHz, 256-frame grains, bounded
64ms storage, connection epochs, asynchronous driver cleanup and an absolute sample
clock. No WASAPI conversion, HID transport, driver-event model or upstream code is
copied into the PS5 backend.

## Silence and output ownership

`last_packet` and `last_nonzero` are independent. All valid packets advance input
sequence, including idle zeros. Duplicate/backward packets cannot refresh activity.
Zeros never initiate an effect. During an effect they retain the sample timeline;
after nonzero data drains and 60ms of nonzero inactivity elapses, remaining queued
zeros are discarded and native output releases ordinary rumble. Continuous zero
packets then remain ignored, without resetting sequence. Fault flushes preserve
sequence; a new connection/user epoch resets it.

Connection epochs still guard packet/user lifetime. Each active effect now also
has its own acknowledgement token: an idle/stale acknowledgement cannot authorize
a future effect. The pad thread refreshes the current acknowledgement without
repeating native API calls, covering an idle transition missed between polls.
Ordinary rumble resumes from the latest host-requested motor state; triggers and
RGB retain their existing paths.

`inputs = dequeued + ignored_silent` after complete drain is the no-loss balance
when no discontinuities/faults occurred. Ignored/discarded **silent** frames are
intentional priority handling, not lost nonzero effects. Drop/gap/error counters
remain separate.

## Compile-time policies

| HAPTICS_PROFILE | Name | Target frames | Minimum accumulation | Short fragment timeout | Sample storage |
|---|---|---:|---:|---:|---:|
| 0 | Baseline | 768 | 16ms | 20ms | 3072 frames / 64ms |
| **1 (default)** | Balanced | 720 | 12ms | 16ms | 3072 frames / 64ms |
| 2 | Low latency, experimental | 720 | 10ms | 15ms | 3072 frames / 64ms |

`HAPTICS_PRIME_FRAMES`, `HAPTICS_MIN_US` and `HAPTICS_FRAGMENT_US` provide explicit
compile-time overrides; the standard comparison tool refuses to label custom
parameters as one of these three profiles. No saved PS5 configuration is changed.

Developer comparison builds:

```sh
make app TEST_LAB=1 HAPTICS_PROFILE=0 BUILD_LABEL=test-lab-haptics-baseline
make app TEST_LAB=1 HAPTICS_PROFILE=1 BUILD_LABEL=test-lab-haptics-balanced
make app TEST_LAB=1 HAPTICS_PROFILE=2 BUILD_LABEL=test-lab-haptics-low-latency
```

Archive each image/provenance separately before building the next one. Use
[guarded deployment and normal recovery](TEST_LAB_SMOKE.md); do not launch again
while a prior operation has an unresolved receipt/lease.

## Instrumentation

Existing ten-second diagnostics add a policy line and four `waveform timing`
metrics per controller slot. All use the PS5 monotonic clock and fixed histograms;
receive callbacks do not compute quantiles, allocate or acquire worker/pad mutexes.

- `receive_to_audioout`: per-dequeued-frame packet arrival → AudioOut **call start**.
- `first_nonzero`: first nonzero packet arrival → first accepted nonzero output
  grain call, once per ownership effect.
- `audioout_blocking`: wall time spent inside AudioOut, including OS preemption;
  it does not isolate driver CPU time or device playback delay.
- `queue_depth`: queued frames immediately before nonempty output grains.
- `starvation`: zero-filled frames followed by resumed PCM before ownership ends;
  final silence is counted in `padded`, not treated as resumed-stream starvation.

Summaries report samples, p50/p95/p99 and exact observed maximum. Time percentiles
are conservative 1ms bucket upper bounds; queue depth uses 32-frame buckets. The
overflow bucket reports its maximum rather than inventing a precise percentile.
For first-effect percentiles, collect multiple effects; a single effect is only
one observation. Histograms accumulate for the app lifetime: use a fresh title process for each
comparison. Final/failed calls are included in API duration; receive/first-effect
metrics include only accepted output. These are **software timestamps, not physical
actuator latency**, and do not include unknown device/internal AudioOut buffering.

## Deterministic host comparison

Virtual-clock tests feed identical signed stereo samples through the production
playout policy. Every scenario preserves all 48,000 input frames bit-for-bit,
with zero drops/gaps and zero resumed-stream starvation for all profiles.
The blocking fixture waits exactly 20ms every fourth output call in virtual time.

| Input / driver model | Baseline receive p50/p95/p99 ms | Balanced | Low latency |
|---|---|---|---|
| Individual 5ms packets | 16 / 20 / 21 | 12 / 16 / 17 | 10 / 14 / 15 |
| Two packets per 10ms burst | 18 / 24 / 25 | 14 / 20 / 21 | 12 / 18 / 19 |
| Four packets per 20ms burst | 23 / 34 / 35 | 19 / 30 / 31 | 17 / 28 / 29 |
| Variable 0–3ms arrival jitter | 17 / 21 / 22 | 11 / 15 / 16 | 10 / 14 / 15 |
| Four-packet bursts + blocking driver | 31 / 44 / 48 | 27 / 40 / 44 | 25 / 38 / 42 |

First-effect delay in the regular schedules is 16/12/10ms. A one-packet impulse
uses the short-fragment timeout, 20/16/15ms. Other tests cover continuous silence,
retained sequence/wraparound, delayed nonzero rejection, legacy priority recovery,
reconnects, native failures, blocked driver cleanup, multiple slots and fresh
per-effect acknowledgement. A real-worker integration test checks a finite burst;
continuous cadence tests use virtual time because OS sleep duration is not an
exact audio clock. Initial wall-clock fixture failures and a Makefile flag typo
were corrected; they are not counted as successful initial checks.

These modeled reductions alone do not establish PS5 latency improvement. Linux
`make check` (including the native SDK build), tidy and scalar/SIMD stream checks
passed on `260c0ed`; controller/performance guards and the native profile builds
passed after the pacing fix on `7f02cea`. The standalone native-worker tests and
all fifteen virtual profile/scenario combinations passed. The loader tests
verify legacy GET, definite-405 POST negotiation and no retry after uncertain
errors. [CI for code commit ed5a0ab passed](https://github.com/blackbearreloaded/ProsperoLight/actions/runs/38869998407).
The ARM macOS aggregate performance target cannot compile existing x86 intrinsics;
its applicable Linux target passed. Hardware acceptance remains separate.

## Event-driven wakeup investigation

The pinned SDK declares `sem_post`, `sem_timedwait`, `pthread_cond_signal` and
`pthread_cond_timedwait`. Declaration availability is not runtime/nonblocking
validation. A condition-variable design needs a lost-wakeup protocol while
keeping the producer independent of the consumer mutex. A counted semaphore could
notify the owned packet inbox and Configure/Disable, but needs validated import,
saturation, clock/timeout and epoch/permit wakeup behavior on PS5. It also cannot
interrupt an AudioOut call already blocked in the driver.

The present change therefore retains nominal 1ms idle polling and the existing
sample-clock pacing. Actual sleep can overshoot. Event wakeup remains deferred
until its synchronization and runtime behavior are independently validated.
The 4/6ms priming reductions are independently gated. The worker waits for each
absolute deadline before draining the inbox and preparing its grain, so packets
arriving during that wait can fill the due output rather than an already-padded
grain. Epoch and ownership are rechecked after the wait.

## Real-PS5 comparison — 2026-10-16

Real PS5, controller slot 0, Vibepollo 2.0.1, native HEVC / SDR / 1080p60,
20 Mbps configured ceiling, operator-confirmed Ethernet on both machines, 10-second warmup and
30-second measurement. Each fresh title process received the same explicit
336,000-frame / seven-second Windows four-channel source (actuator channels only):
1.5s left, 1s silence, 1.5s right, 1s silence, 1.5s both, 0.5s tail. Execution order
was Balanced, Low latency, Baseline; baseline was left installed at the end of
the comparison. The subsequent default-selection change does not deploy another
image to the console. This is one run
per profile, not a repeated/randomized performance study.

All native images use source `7f02cea173da1e2f53307abca29d74985ae13979` and
OpenGL SDK **1.0.2**. Later `ed5a0ab` changes the host-side loader only, adding
Payload Manager 0.6 POST negotiation after an explicit HTTP 405; it changes no
native application code. No retry follows a timeout or other ambiguous failure.

| Profile | Status | RX / first samples | RX → call p50/p95/p99 ms | First effect p50/p95/p99 ms | API duration p50/p95/p99 ms | Queue p50/p95/p99 frames | Drop / gaps / starvation |
|---|---|---|---|---|---|---|---|
| Baseline | COUNTERS ONLY | 336480 / 1 | 19/26/28 | 19/19/19 | 1/1/1 | 1024/1312/1376 | 0 / 0 / 0 |
| Balanced | COUNTERS ONLY | 336480 / 1 | 16/22/23 | 14/14/14 | 1/1/1 | 864/1120/1152 | 0 / 0 / 0 |
| Low latency | COUNTERS SHOW ISSUES | 336480 / 1 | 14/21/23 | 12/12/12 | 1/1/1 | 800/1120/1184 | 0 / 0 / 64 |

All three generic integration runs completed **PASS / VALID**. That verdict does
not enforce waveform-specific starvation acceptance. Balanced had zero drops,
gaps and resumed-stream starvation, with receive-to-call p50/p95/p99 lower by
3/4/5ms than baseline in this single comparison. On that evidence, the operator
requested promotion of Balanced to the production default; baseline remains
available with `HAPTICS_PROFILE=0`. Low latency recorded 64 starvation frames
(1.33ms): its physical impulses worked, but it **does not pass the strict zero-
starvation gate** and is not recommended for promotion. The counter detects
padding followed by resumed PCM within ownership; it does not locate an audible
defect or prove those 64 frames overlapped nonzero source content.

Each first-effect histogram contains **one ownership observation**, despite three
physical pulse segments. Treat its displayed percentiles as that one observation,
not a statistically established tail latency. The host PCM samples were not
captured, so do not infer exact silent-sample content from source pauses alone.

Exact identities and independently checked report/export hashes:

| Profile | Run ID | Build label / eboot SHA256 | Raw report / redacted export SHA256 |
|---|---|---|---|
| Baseline | `53739f2e-08c3-4632-8aba-059c3f09a3c1` | `test-lab-waveform-latency-p0-7f02cea`<br>`8f59c82a0dfb0413ff210c3787f92f55b76b7696275c5623d20ffe3f4de9675b` | `ac71e94c9e32236d681aacea1a0334c3566b1283fededac9b344ecdf5698fa2c`<br>`932ea7e2991e307939b3a00a24b40275bf311a83804a5b79a0ffd2e022b95004` |
| Balanced | `36e083bb-a0ea-4000-a26b-a89acbefd4fc` | `test-lab-waveform-latency-p1-7f02cea`<br>`eaeb37c20f1e2c0f592446e643ae16d18c9fb88edae129f4c3dee61321950abf` | `06f8f62932a118248436f3937308a9a2092fc67eb2c62af3de78d8a8f5989253`<br>`df2c33c546af30d24e520d10486bad0b27fc18629abb647bea9c8140afe71c1e` |
| Low latency | `d6b4c7b3-6eb2-40b8-a8c2-fba05ac57a45` | `test-lab-waveform-latency-p2-7f02cea`<br>`9cee27f926e29f1e96412c847f926423e6b0ee1c1bc0194563961ddcfc96cd49` | `a6f8c4385ac7a867fb01d4c94c9365b6081e6f4c27ca87996784c3206b25f04e`<br>`1bd27e9769df7356e5a01c5240c7f5345f104d63892d38d324456c4f210f67fa` |

| Profile | Input = dequeued + ignored silence | API blocking exact maximum | First-effect exact observation |
|---|---|---|---|
| Baseline | 336960 = 336480 + 480 | 18 us | 18381 us |
| Balanced | 336960 = 336480 + 480 | 28 us | 13296 us |
| Low latency | 336960 = 336480 + 480 | 26 us | 11382 us |

Every run verified 133 installation files by final-destination SIZE/SHA256
readback (native LIST inventory; trusted managed-installation limitation retained),
matching run receipt, raw and redacted artifact manifests, normal title exit,
released remote lease, zero active clients/RTSP sessions and unchanged saved
configuration/pairing. Final snapshots show native ownership inactive. Tail
padding is separate from resumed-stream starvation; nonzero sample fidelity is
established by deterministic host tests, not a physical waveform capture.

The operator explicitly confirmed all three impulses and distinguishable left/
right sides on **Low latency**. Balanced was not physically observed; do not
relabel the earlier original-backend observations as validation of this image.

Preparatory failures remain archived as INCONCLUSIVE: one stale lease from the
previous stopped run and two preflights blocked by a runtime RADV cache. They
did not launch a stream. A separate read-only doctor request exceeded the local
HTTP timeout before run creation. The exact old lease was reconciled against
its terminal report and TITLE_IDLE; the cache was preserved without deleting
its six files and restored afterward, with its original file set/sizes verified.
None of these attempts is counted as hardware PASS.

**Production now defaults to Balanced/profile 1 by operator decision after this comparison.** These software observations do not
establish physical actuator latency, an across-game/network improvement, older-
host runtime compatibility, all-four-controller hardware behavior, reconnect/
adaptive-trigger hardware behavior, ordinary rumble during continuous exact-zero
PCM on hardware, PyroWave runtime compatibility, HDR/VRR correctness or 4K120
stability. Those paths retain deterministic/compatibility coverage where present;
broader hardware acceptance remains separate.

Reproduction and normal recovery: [Test Lab smoke instructions](TEST_LAB_SMOKE.md).
The offline helper reads archived traces only and never establishes hardware
acceptance from counters alone:

```sh
python3 tools/controllers/waveform_report.py --baseline baseline-debug.txt \
  --balanced balanced-debug.txt --low-latency low-debug.txt --slot 0 \
  --output comparison.md
```

## Final concurrency and rumble review

Native correctness fixes in `cab1ecc` leave Balanced's timing parameters unchanged:

- A call blocked for more than 60ms retires the active effect and its permission,
  flushes published in-flight packets and sets an arrival cutoff for a producer
  that was still copying at reset. Sequence history is preserved. Fresh PCM gets
  a new monotonic token and cannot reach AudioOut until acknowledged again.
- The pad owner passes the exact token observed in its output poll. A delayed
  acknowledgement cannot authorize a replacement effect or a different connection
  epoch; permission still has to equal the worker's current token. The atomics
  retain sequentially consistent ordering. Reset, inbox retirement and playout
  changes happen under the worker mutex; driver I/O remains outside it.
- Completion/error/stall handling only mutates the playout of its matching epoch.
  An old port finishes/closes before the worker opens another user's port.
- Mode tracking distinguishes unknown, native and legacy states. Rejected mode 1
  does not imply that mode 2 is already installed; legacy output explicitly
  selects it before sending rumble. A regression reproduced the missing fallback
  before the fix and passes with explicit mode tracking.
- Failed legacy-mode transitions and motor writes retain the latest host request
  for retry on a later pad poll. Trigger and LED output is not skipped while a
  rumble retry is pending. Obsolete envelope-only fields were removed.

Targeted native-worker and controller tests pass, including a held >60ms driver
call, packets published during the stall, stale/fresh acknowledgements, a user
change followed by shutdown while blocked, latest-rumble restoration, failed
mode/write retries and native-failure fallback. The native-worker suite also
passed Linux ThreadSanitizer without reported data races. These are automated
software checks, not physical acceptance of a driver stall or a motor waveform.

The earlier three-profile hardware comparison above tested `7f02cea`, before
these correctness fixes; it must not be relabelled as final-binary validation.
See [PR #57's final validation record](https://github.com/blackbearreloaded/ProsperoLight/pull/57)
for the final full checks, production build, pushed-commit CI and subsequent smoke
identities/results. A source-generated silent interval is not proof of exact-zero
PCM at PS5 ingress unless received-zero counters establish that independently.
