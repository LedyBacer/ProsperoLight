# Waveform latency and rumble coexistence — 2026-10-16

## Status and scope

The operator stopped PS5 testing and reported that gameplay generally works well.
That is operator feedback on the previously installed native backend, not a timing
benchmark or verification of these new changes. No latency-profile build has been
deployed or run on PS5. **Production remains baseline/profile 0.** Speaker and
microphone work remains deferred.

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
| **0 (default)** | Baseline | 768 | 16ms | 20ms | 3072 frames / 64ms |
| 1 | Balanced, experimental | 720 | 12ms | 16ms | 3072 frames / 64ms |
| 2 | Low latency, experimental | 720 | 10ms | 15ms | 3072 frames / 64ms |

`HAPTICS_PRIME_FRAMES`, `HAPTICS_MIN_US` and `HAPTICS_FRAGMENT_US` provide explicit
compile-time overrides; the standard comparison tool refuses to label custom
parameters as one of these three profiles. No saved PS5 configuration is changed.

Future developer builds, **only when hardware testing is resumed**:

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

These modeled reductions do not establish PS5 latency improvement. Build/CI
validation results are recorded after the final host batch; hardware acceptance
remains explicitly separate.

## Event-driven wakeup investigation

The pinned SDK declares `sem_post`, `sem_timedwait`, `pthread_cond_signal` and
`pthread_cond_timedwait`. Declaration availability is not runtime/nonblocking
validation. A condition-variable design needs a lost-wakeup protocol while
keeping the producer independent of the consumer mutex. A counted semaphore could
notify the owned packet inbox and Configure/Disable, but needs validated import,
saturation, clock/timeout and epoch/permit wakeup behavior on PS5. It also cannot
interrupt an AudioOut call already blocked in the driver.

The present change therefore retains nominal 1ms idle polling and the existing
sample-clock pacing. Actual sleep can overshoot. Event wakeup is deferred rather
than silently introducing an unverified driver/runtime path while hardware testing
is stopped. The directly testable 4/6ms priming reductions are independently gated.

## Real-PS5 comparison report — not run

| Profile | Exact new binary / run | Software timing | Active-effect fidelity | Hardware verdict |
|---|---|---|---|---|
| Baseline | NOT RUN | NOT MEASURED | NOT CHECKED | NOT ESTABLISHED |
| Balanced | NOT RUN | NOT MEASURED | NOT CHECKED | NOT ESTABLISHED |
| Low latency | NOT RUN | NOT MEASURED | NOT CHECKED | NOT ESTABLISHED |

When authorized to resume, compare fresh processes and identical source/build
flags, host/gamepad mode, HEVC/PyroWave selection, network, stimulus and duration.
Keep distinct immutable build/run identities. Exercise each slot, reconnect,
silence while ordinary rumble is requested, adaptive triggers/RGB and normal exit.
Reject any additional drops/gaps, active-effect starvation, stale replay or legacy
rumble regression. Confirm artifact integrity, frame balance, native exit, host
session/lease cleanup and unchanged pairing/config separately. Older hosts and
4K120 need their own compatibility evidence.

The offline helper reads archived traces only:

```sh
python3 tools/controllers/waveform_report.py --output comparison.md
# Later, add --baseline baseline-debug.txt --balanced balanced-debug.txt \
# --low-latency low-debug.txt --slot 0
```

Without files it produces NOT RUN rows. It rejects missing/mixed/custom policies,
incomplete timing snapshots, wrong units and empty effects. Even complete trace
counters do not establish physical acceptance or matching run settings/receipts.
