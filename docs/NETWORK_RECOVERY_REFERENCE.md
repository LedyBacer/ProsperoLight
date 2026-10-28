# Network recovery: reference, target and implementation

Baseline: `86b2c945606f7b7417a8dde5e669a4bf7e4e4dad`. Work branch:
`codex/network-playout-recovery-70`. Evidence date: 2026-10-26.

## What the references actually establish

* [Nonary Moonlight VRR17](https://github.com/Nonary/moonlight-qt/releases/tag/v6.1.0-vrr17),
  source `704a89f9525c37d8580ca0094c02355fec10d4aa`:
  `app/streaming/video/ffmpeg-renderers/pacer/vrr/intervalbuffer.h`,
  `readinessfeedback.h`, `reserve.h`, `vrrtimingcontroller.h`.
  Readiness/service lead is separated from presentation target; fresh attributable
  spacing error authorizes gradual bounded reserve, quiet intervals release it.
  Its 99.5% Balanced **controller quality target is not a displayed-frame ratio**.
  [VRR16 release observations](https://github.com/Nonary/moonlight-qt/releases/tag/v6.1.0-vrr16)
  report some mean local queue values falling about 12→2 ms. These are preliminary
  author observations on other hardware/VRR, not a controlled PS5 fixed-60-Hz result.
  `docs/vrr-latency-captures.md` explicitly distinguishes software submission
  timestamps and physical display latency. Preserve our confirmed VideoOut flip
  accounting; do not copy WASAPI/Vulkan scheduling into PS5 AudioOut/VideoOut.
* [Punktfunk source](https://git.unom.io/unom/punktfunk/src/commit/325cde25f84a7ddc209b45fdd2540c40c8d35840),
  `crates/core/punktfunk-core/src/audio/jitter.rs` and `phase.rs`:
  source timestamps are left intact while offset/skew are learned; audio uses
  consecutive starvation hysteresis, a depth average to identify unbanked target
  growth, cautious shrink probes and crossfaded drift correction. Its changelog
  reports ~2000→9 audible events in a **ten-minute simulation** of 25-ms gaps every
  300 ms at −50 ppm. That is a simulation claim, not an acoustic hardware result
  or the same jitter/loss distribution as our netem test.
  [Client settings](https://docs.punktfunk.unom.io/docs/client-settings) describe
  an optional 1–3-frame smoothness reserve. Its native transport and clock
  handshake differ from GameStream; native-protocol capture→reassembly numbers
  are not interchangeable with PS5 receive→display measurements.
* [Common-C DECODE_UNIT timing contract](https://github.com/moonlight-stream/moonlight-common-c/blob/master/src/Limelight.h):
  receive/enqueue, source presentation and client monotonic timestamps have
  distinct clock domains. Neither source PTS nor RTT alone measures actuator,
  HDMI scanout or input-to-photon latency.

The algorithmic references are directly inspected source at the pinned revisions.
The numeric goals below are **our engineering acceptance thresholds**, inspired by
bounded buffering and continuity in those clients. No public matched PS5/netem
benchmark was found that proves these numbers on our setup.

## Concrete first acceptance gate

Same real PS5/Windows Ethernet setup, Native Classic/Paced, H.264 NVENC SDR
1080p60, 20-Mbps ceiling, scrolling seed137, 220-Hz audio. Warmup10s, measure100s.
One LAN and one jitter run after a substantial code/build improvement.

| Metric | LAN target | Existing wifi-congested target |
|---|---|---|
| Presented / dequeued unique AUs | ≥99.5% | ≥97% |
| Dequeued / host sent rate | ≥98% | ≥95% (diagnostic until phase-matched) |
| Presented FPS when source sustains ≥58 FPS | ≥57.5 | ≥55 |
| AU enqueue→observed flip p95 / p99 | ≤25 /35 ms | ≤60 /80 ms |
| Maximum presentation gap | ≤70 ms | ≤150 ms; no >1s stall |
| Decode errors / queue overflows / RTP capacity flushes | 0 | 0 |
| Audio short-read grains (active session) | 0 | ≤2 over the ~110s active session |
| Active silence grains | 0 | ≤10 over the ~110s active session |
| Audio maximum adaptive target / absolute ring cap | 80 /180 ms | 80 /180 ms |

Do not trade frame recovery for unbounded latency. LAN mean/jitter and tails must
also be compared to the prior exact trace; a pass against a loose ceiling does
not erase a regression. Host rates include their independent observation window;
use matched source frame IDs/phase boundaries before calling a ratio wire loss.
Short-read grains and full reprime events must be reported separately: avoiding
reprime can expose more small deficits while reducing manufactured silence.

The previous wifi result was 48.64 displayed /51.25 dequeued /60.61 host FPS,
frame-age p95 50.66ms, 915 network gaps, 13 unrecoverable frames, 32 audio
underruns, 233 total silence grains. Targeting only presentation would hide
missing AUs upstream; all gates must be reported, including failures.

`wifi-congested` retains delay20±15ms normal, loss1% correlation20%, reorder0.2%.
Previous receipts show 0–1 actual qdisc drops, **not verified 1% loss**. Reproduce
this as a jitter/reorder experiment only; do not claim 1%-loss compatibility
without independent drop/flow qualification. No synthetic calibration traffic is
required during the two hardware streams. Check actual video UDP ports rather
than assuming the client's destination equals the host's 47998 source port.

## First coherent changes

1. Retain small unseen frame/block holes even when the current FEC lists are
   empty. Common-C's `currentFrameNumber` is still an expected frame, not
   permission for a later UDP arrival to skip it. Preserve the same 40ms global
   hold deadline, 2048-packet/4-MiB bounds, immediate expected-packet dispatch,
   legacy FEC ownership and PyroWave bypass. Large discontinuities retain fast
   resynchronization. Per-packet age and the active block deadline bound waiting; a retired queue
   epoch must not expire later frames. Empty queues ignore old receive clocks.
2. Correct Common-C audio FEC milliseconds→microseconds arithmetic: for negotiated
   5ms packets, four shards plus10ms allowance is30ms, not10.020ms. Keep the clean
   in-order fast path and require a successor FEC block before declaring loss.
3. Adapt the **concept** of Punktfunk audio starvation hysteresis/hollow average
   into a bounded 48-kHz PCM policy. Brief short reads fade/conceal only their
   missing suffix and resume on the next grain without waiting for the whole
   target. Sustained ≥40ms starvation (at least2grains), or an unbanked target,
   re-primes. Keep AudioOut grains, independent hardware clock, PCM FIFO ordering,
   Opus ownership, existing fades,80/180ms caps and gradual drift/shrink policy.
4. Add explicit reprime/concealed-frame counters and test the actual applied
   transport functions, not only a parallel Python timing model.

No native-protocol wire code or platform API is transplanted. The audio policy
is an original C++ adaptation of the cited ideas; Punktfunk offers MIT/Apache-2.0,
Nonary GPL-3.0. Existing licensing and attribution are retained. The existing
source-clock shadow experiment remains disabled; fixing demonstrated transport
and PCM discontinuities precedes switching timing models.


## Second coherent block: progress and missing audio blocks

The first dev hardware run improved presentation coverage to98.34%, but exposed
an incorrect queue-wide hold epoch (204 video expiries) and retained30 audio
reprimes. Do not promote that version. The real audio FEC failure counter fell
2750→679 after the unit conversion, while the old completely-unseen-block branch
still skipped source PCM time without PLC.

The next transport layer uses the oldest logical held packet's deadline, bounded
by its active FEC block, instead of a queue epoch that survives successful frame
progress. A deterministic replay reproduces the stale-epoch case. Audio on an
already reordered link waits the negotiated block horizon for up to3 unseen FEC
blocks. After that, genuinely missing slots use the existing one-packet PLC
contract; large discontinuities keep the original explicit resync. Real Common-C
queue tests cover late whole-block recovery, exact PLC slot counts, stale packet
rejection and16-bit sequence wrap with ASan/UBSan. No PCM samples are duplicated
or held indefinitely. Only this meaningful second block justifies one additional
jitter hardware comparison; no parameter sweep is planned.

## Hardware checkpoint (2026-10-26)

Three streams total: first-dev LAN, first-dev jitter, final-dev jitter. An
additional deploy preparation stopped without launching the title, retained its
lease safely, then all133final files were verified and reused without redeploy.
No hardware parameter sweep was run.

| Jitter metric | 86b2c94 | Final development source |
|---|---:|---:|
| Host / dequeued / presented FPS | 60.61 /51.25 /48.64 | 57.45 /56.46 /55.74 |
| Presented/dequeued coverage | 94.91% | 98.73% |
| AU enqueue→observed flip p95 /p99 | 50.66 /59.32ms | 47.40 /52.91ms |
| network_frame_gaps (whole session) | 915 | 74 |
| Unrecoverable frames | 13 | 17 |
| Max observed presentation gap | 183.52ms | 100.12ms |
| Audio underruns (whole session) | 32 | 4 (all before measurement) |
| Underruns in100-second final measurement | unavailable from aggregate alone | 0, last-underrun timestamp precedes measurement |
| Audio full active silence grains | 180 | 4 |
| Audio target final /peak | 80 /80ms | 20 /50ms |

Final source digest `40d834ada3f28df2424d292ead57758fba22026e151bd44b26b248275fab2208`,
eboot `a63a5c78e236560708ef11eda04c09684c30eb424f0b559c47ed0dc3135cc679`,
run `609fa133-3339-440d-aab2-95eac7eb4355`, SDK1.0.2. Base86b2c94 plus the
local changes. Both developer revisions retained the same incremental build
label; digests distinguish them. Native/phase/unified experiments remain off.

First-dev LAN:58.80received=presentedFPS, p95/p99 age19.03/22.48ms,0frame gaps,
0decode/queue faults,one32-PCM-frame short read(0.67ms),no re-prime or full active
silent grain. **Final revision's LAN was not rerun.** Its additional logic is
restricted to reordered queues, but this is not a final-image LAN hardware proof.

The video thresholds were reached in the targeted final jitter run. All strict
acceptance gates are **not** complete: whole-session audio4exceeds2,final LAN has
no hardware run,host was below58FPS,real1%wireloss is unverified. Raw verdict
remains functionalPASS/measurementINCONCLUSIVE. No optical/acoustic latency or
A/V sync was measured. Final ring EWMA was49.17ms despite a20ms target; actual
standing-depth release remains a separate issue. Do not advertise20ms audio
latency from a target alone. The first bad queue epoch (204timeouts) was rejected,
not hidden or promoted.

11focused host tests and the native builds passed. Original installation,
configuration,pairing,Windows display and temporary network state were restored.
The raw reports,CSV traces,logs,manifests and SHA256 integrity indexes are local
build artifacts; no pairing material or raw private artifacts were published.

## Final LAN and standing-audio-depth hardening

The first main/current native LAN pair straddled a host reboot and changed the
advertised capture output from DISPLAY28 to DISPLAY1. Both browser traces were
60 FPS, but that does not establish identical capture/packetization. Do not use
that pair to dismiss lower source throughput or prove client-only regression.
A recorded-input replay of 6464 targets produces identical main/current LAN
FramePacing deadlines with zero network reserve; hardware scheduling remains a
separate requirement.

Keep PyroWave's presentation backend at the main LAN policy: no native arrival
jitter controller, reserve or successor catch-up is installed in that backend.
Native shadow estimators now execute only in the explicit unified experiment,
not on the production presentation hot path. Successful-submit and confirmed-flip
accounting remain intact.

Game audio retains its independent output worker, continuous hardware clock,
short-read hysteresis, bounded recovery and crossfades, but restores 256-frame
grains and uses a 480-frame/10-ms clean-link floor. Repeated starvation/near misses
still increase reserve to the existing 80-ms ceiling. Standing-depth trimming
qualifies using the EWMA and only trims at a safe instantaneous depth: one low
grain can defer a trim but cannot repeatedly erase two seconds of standing excess.
The fixed-point EWMA uses elapsed time without coarse per-grain weight truncation.
DualSense waveform ownership, transport and Balanced profile are unchanged.

Final scope is finite: a short main/current native LAN pair without host topology
changes, one current PyroWave LAN sample and one qualified independent-loss/jitter
sample. Gate on no decoder/queue faults or long stalls, comparable source-aware
LAN throughput and latency, and bounded audio reserve. This does not certify every
WAN distribution, acoustic/optical latency or physical haptic fidelity. Any such
additional scope belongs in follow-up work rather than silently extending PR #70.

### Final fixed-LAN ready-picture selection — 2026-10-27

The matching-rate fixed-LAN path now starts with one waiting picture; real arrival
jitter retains two. `ReadyMailbox::limit` explicitly returns a surface retired by
shrinking. Unpaced, VRR and fractional/output-mismatched cadence retain their own
policies. A picture already ready before the previous confirmed flip may represent
an old startup phase: before submitting it, wait only until the bounded next-vblank
admission deadline for a fresh successor. Without a successor, submit the retained
picture on that slot. This avoids a persistent one-frame FIFO tax rather than
continuously dropping frames. Decode callbacks remain nonblocking.

Final real-PS5 comparison: main `a5301b7` versus `test-lab-lan-final-v2`, SDK1.0.2,
H.264 NVENC SDR1080p60/20Mbps ceiling, Native Classic/3cores/Paced/VSync, scrolling
seed137, 220Hz tone. A temporary guarded120Hz source avoids60Hz source/capture
aliasing while the stream/output remain60/59.94FPS. Both final LAN samples use
10s warmup+20s measure, identical host capture topology/settings in one session.

| Final sample | Host sent FPS | PS5 presented FPS | Enqueue→flip p95/p99 |
|---|---:|---:|---:|
| Main LAN | 59.915 | 59.859 | 29.329/29.432ms |
| Final LAN | 59.906 | 59.831 | 13.006/13.165ms |
| Final independent-loss jitter | 60.046 | 59.095 | 45.992/52.056ms |

LAN throughput is comparable (boundary/replacement-scale difference), not a
statistically proven FPS gain. LAN has zero network gaps, unrecoverable frames,
decoder errors, queue overflows, audio underruns/reprimes or active silence. The
same main binary previously had8.94ms p99 in another start, confirming its latent
phase sensitivity rather than proving every PR latency difference host-caused.
The final source/eboot hashes identify the tested working tree:
`3bbf6df8882fe5956855ec41d3d4bf0cba42bb1338c9433467b6ec870b1cb70c` /
`7ead4360a92723ecfc63d18ac95ac5dd0b793833be8eb6fd31b693e05c02d43e`.

The independent-loss run is10s+30s, delay20±15ms/reorder0.2%, and actually loses
341of34870offered qdisc packets (~0.978%). Its video UDP path is independently
counted. Maximum no-progress gap83.423ms,4unrecoverable frames,18network gaps,
2070reorder-rescued frames,0decoder errors/overflows/capacity flushes. Raw verdict
remains functionalPASS/measurementINCONCLUSIVE (NETWORK_LOSS); impairment
qualification is a separate PLAUSIBLE result. Do not relabel the report PASS.

Audio has0underruns during measurement, but5during warmup (864missing PCM frames,
18ms aggregate;6active full-silence grains;1reprime). This does not meet the earlier
aspirational ≤2whole-session short-read target. Initial jitter priming remains a
specific quality follow-up; do not claim all audio acceptance gates or acoustic
quality passed. Final target50ms/EWMA53.17ms is not50ms physical latency. Hardware
long quiet/release and final-v2 PyroWave were not rerun; the prior30s PyroWave sample
passed with117.965FPS/p99ready→submit0.801ms, and its backend remains main-identical.

Run receipts: main `8cc7cd6b-f1e9-418b-b4d7-ee56483cd249`, final LAN
`0db568c8-9f40-4000-ada8-b6a52bd33067`, final loss/jitter
`bbd7db7b-7892-49bd-b4f6-c1e2a0f26516`. Indexed evidence remains local; no private
artifacts or pairing credentials are published. Final recovery restores the
production image, latest saved settings/pairing, display, worker and routing state.


### Startup audio drought recovery — 2026-10-28 (host-tested only)

Production does not classify Ethernet versus Wi-Fi. Audio reserve follows PCM
availability; native video arrival jitter has its own bounded queue policy.
Stable delivery retains the 480-frame/10-ms audio floor regardless of interface
or RTT. An unstable Ethernet path can require the same reserve as Wi-Fi.

Within five seconds of first PCM output, a short read accompanied by a delivery
drought longer than two negotiated packets (or a device grain) now authorizes
fast recovery. The worker raises reserve to the observed drought plus a packet
and grain, rounded up to 10-ms steps, with a conservative 30-ms minimum and the
existing 80-ms ceiling. If this raises the target, the next grains bank real PCM
until the reserve is met. Valid PCM in the triggering grain is consumed once;
AudioOut keeps ticking, including deliberate silence during rebuffering. No
callback blocking, packet discard, RTT-based reserve, interface detection, or
DualSense change is introduced. Outside the startup window the existing slow
hysteresis and continuity policy remain; normal quiet-period shrink also applies
to a startup increase. A lone early delivery drought may therefore trade a
bounded rebuffer pause for fewer subsequent short reads. These thresholds are a
conservative software policy, not a hardware-proven optimum.

Deterministic four-second replays with repeated 35-ms droughts compare the old
and new recovery at identical packet/sample clocks. Short reads decrease from
5 to 2 for 5-ms packets and from 5 to 3 for 10-ms packets. Concealed short-read
PCM decreases from 896 to 128/384 frames respectively. These counts exclude
intentional priming silence, which is separately bounded in the regression test.
Including rebuffer silence, missing/synthetic PCM totals fall from 2176 to 1664
frames for 5-ms packets and remain 2176 frames for 10-ms packets. Thus the latter
replay consolidates dropouts rather than proving lower aggregate silence.
Clean 5/10-ms packet replays retain all PCM, zero short reads and the 10-ms floor.
Tests also cover timestamp reversal, missing timestamps, the five-second cutoff,
reserve saturation, idempotent re-prime and subsequent quiet-period release.
No PS5 run was performed for this change; improvement in audible startup quality
and the balance between rebuffer pause and dropouts remain hardware-unverified.

### Final startup-audio and PyroWave check — 2026-10-28

The PS5 build `test-lab-audio-startup` has eboot SHA256
`bbd9cd1255948e3daa3371ddffba383299b806bc7110f3945f2a93fc5f029199`;
its source digest was verified against the local tested source. A first LAN
attempt stopped in stimulus preparation (`RAF_BELOW_TARGET`) without starting
a stream. After the interactive desktop was made available, three short runs
completed. Each used the same 1080p scrolling source and tone; native used H.264
NVENC 60 FPS, the source was held at 120 Hz, and PyroWave used a 120 FPS stream.

| Run | Verdict | Host FPS | PS5 FPS | Age or ready-to-submit p99 |
| --- | --- | ---: | ---: | ---: |
| Native LAN, 10s warmup + 20s measured | PASS / VALID | 59.86 | 59.79 | 17.62 ms frame age |
| Native jitter/loss, 10s + 30s | PASS / INCONCLUSIVE (network loss) | 59.98 | 59.46 | 48.08 ms frame age |
| PyroWave LAN, 10s + 30s | PASS / VALID | 117.62 | 116.91 | 7.97 ms ready-to-submit |

For jitter, netem applied 20 ± 15 ms, independent 1% loss and 0.2% reorder;
407 drops were counted over 38,308 offered packets (1.06%), and the verified
video UDP counter advanced by 21,847 packets. The largest measured display
progress gap was 100.06 ms, with no one-second stall and no capacity flush.
The prior impaired run used a different random seed, so comparisons below are
individual observations rather than a controlled causal estimate.

All three audio short reads in the new impaired run occurred before measurement;
there were none during its 30-second measurement. Across the entire session,
short-read concealment was 384 frames (8 ms) and there were nine active silent
256-frame grains (48 ms), about 56 ms together. The preceding run had five short
reads, 864 concealed frames (18 ms), and six active silent grains (32 ms), about
50 ms together. Rebuffer wait rose from 42.78 to 64.08 ms. First PCM output was
2.79 ms after the first packet callback, versus 2.00 ms previously; the 535 ms
from starting AudioOut to first PCM includes time before the first packet and is
not the buffer delay. Thus fewer short reads did not demonstrate less total
silence or satisfy the earlier two-short-read target. Hardware listening and
physical audio latency were not measured.

The new PyroWave run has no network packet losses or flip errors, but two stale
frames, and its ready-to-submit p99 was 7.97 ms compared with 0.80 ms in the
previous candidate's separate run. Host FPS was similar (117.62 versus 117.60),
but frame phase and timing differed. This is an unresolved possible latency
regression, not evidence that the new audio policy caused it: PyroWave backend
source remains identical to main and no same-session main comparison was run.
Do not claim absence of a PyroWave regression or a physical latency result from
these runs. Receipts and every collected artifact were hash-checked locally.
