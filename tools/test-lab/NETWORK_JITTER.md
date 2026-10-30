# Network impairment experiment (PS5, no hardware changes)

The PS5 uses an extremely latency-sensitive UDP stream. **RTT and jitter are
not interchangeable**: an extra fixed 50 ms RTT is not automatically an extra
50 ms of playout buffering. The aim is to measure smoothness, continuity and
stale-frame recovery under **delivery variation**, without masking slow hosts.

## Topology and safety

Run on a **dedicated Linux sender or Linux gateway egress toward the PS5**;
the default-route interface and existing custom qdiscs are refused. This
supports a Windows Vibepollo host when its PS5-bound traffic is routed through
that Linux gateway. Running `tc` inside an ordinary Windows or WSL environment
does **not** shape Windows host traffic unless it actually traverses that Linux
network interface. Verify direction with a packet capture or interface counters.

`netem` works on outbound packets: a 25 ms setting in one direction adds about
25 ms to path RTT, **not** 50 ms unless both directions are separately shaped.
For two directions, the two named egress interfaces must actually be distinct
interfaces carrying PC→PS5 and PS5→PC respectively (e.g. two routed gateway
links/veth interfaces); do not try two root qdiscs on the same physical device.
Avoid shaping the test coordinator control connection. Packet-loss/jitter
patterns are probabilistic; identical parameters do not produce identical
losses. Record measured PS5 RTT separately from the configured delays.

Commands (Linux with `tc`, `ip`, and `tcpdump`; netem needs
CAP_NET_ADMIN and tcpdump needs capture privileges). For PR #70 hardware
comparisons, set `PS5_IP` to the actual console address and
`VIDEO_UDP_PORT` to the **negotiated host→PS5 video UDP destination port**
(do not assume it is always 47998). An explicitly verified test is required
before claiming that the impairment affected the game stream:

```sh
PYTHONPATH=tools/test-lab/src python3 -m pltest.netem \
  --interface ps5-egress --scenario wifi-congested \
  --verify-video-udp "${PS5_IP}:${VIDEO_UDP_PORT}" \
  --evidence build/netem-wifi-congested.json -- \
  python3 tools/test-lab/pltest_cli.py run \
    --suite network-jitter --profile tools/test-lab/profiles/network/h264-1080p60.json
```

Two-way experiment, with independently selected return-path conditions:

```sh
PYTHONPATH=tools/test-lab/src python3 -m pltest.netem \
  --interface ps5-egress --scenario wan-50 \
  --return-interface host-egress --return-scenario wifi-light \
  --evidence build/netem-bidirectional.json -- \
  python3 tools/test-lab/pltest_cli.py run \
    --suite network-jitter --profile tools/test-lab/profiles/network/h264-1080p60.json
```

Alternative: `pltest netem ...` if the package is installed. For non-Linux
hosts, run the impairment command **on the gateway**, then use your normal
Test Lab coordinator; the wrapper must remain alive until the test completes.
The wrapper validates **both** interfaces before applying either condition;
captures per-direction configuration and `tc -s -j qdisc show` packet/byte/drop
statistics **at installation and before cleanup**. The owned netem packet
counter must **increase during the test in every shaped direction**; otherwise
the verdict is `NETWORK_TRAFFIC_UNVERIFIED` (`INCONCLUSIVE`, wrapper exit 3)
even if the wrapped test succeeded. A positive count proves egress activity,
**not** that the PS5 stream traversed that interface. Inspect flow identity
through a packet capture/routing check. For a confirmed video-path run, `--verify-video-udp PS5_IP:PORT`
checks the route to the console from the **same network namespace** and samples
up to 64 matching host→PS5 UDP datagrams on the shaped egress using tcpdump
(with a minimum of eight matches). No packet contents or captures are retained.
The evidence includes `video_flow_verdict=UDP_EGRESS_VERIFIED`, or
`VIDEO_UDP_FLOW_UNVERIFIED` / exit 3 if insufficient. Without that option,
`video_flow_verdict=NOT_REQUESTED`; rising qdisc packet counters alone never
prove the stream was shaped.

The wrapper now also checks configured loss against the `tc -s -j` netem
`drops` counter. Once the sample predicts 30 or more drops, observing fewer
than 10% of that expectation makes the scenario **INCONCLUSIVE** with
`NETWORK_LOSS_UNVERIFIED` (exit 3). Smaller samples are explicitly marked
`INSUFFICIENT_SAMPLE`, not falsely reported as 0% loss. These intentionally
loose thresholds detect gross misrouting/misaccounting, not small random
differences. A 1% configured-loss run with one qdisc drop out of ~100,000 packets
must never be treated as proof of real 1% wire loss.

Before a PS5 loss test, optionally qualify the Linux gateway's own `netem`
behavior with an isolated synthetic UDP run (requires CAP_NET_ADMIN):

```sh
python3 tools/test-lab/netem_calibrate.py --scenario wifi-congested --count 12000
```

The calibrator creates a random owned network namespace, uses **only its
loopback**, and removes that namespace afterward. It reports both received
UDP sequence numbers and `tc -s -j` drop counts. No Windows route, PS5 socket,
or host-facing NIC is changed. A `qualified=false` result blocks a physical
loss claim; `qualified=true` establishes only the local qdisc configuration,
not that the later video flow takes the shaped path. The latter still requires
`--verify-video-udp` on the actual run. The calibrator is intentionally opt-in
and not part of unprivileged CI.

The wrapper records the test exit code;
rolls back only its own qdiscs (including if the second installation fails) on completion/error/
Ctrl+C; and writes a per-direction cleanup receipt. An interrupted/failed run
is not automatically a performance failure: inspect the cleanup evidence.
For a hard kill or power loss, inspect `tc -j qdisc show dev IFACE`
and explicitly remove handle `51:` after verifying ownership. Never blindly
delete a foreign qdisc. `lan` runs a baseline without applying `tc`.

## Scenarios (one-way, on the configured egress)

| Scenario | Mean delay | Jitter (normal distribution) | Loss | Reorder |
|---|---:|---:|---:|---:|
| `lan` | 0 | 0 | 0% | 0% |
| `wifi-light` | 5 ms | 3 ms | 0.1% | 0% |
| `wifi-congested` | 20 ms | 15 ms | 1%, 20% correlation | 0.2% |
| `wan-50` | 25 ms | 10 ms | 0.3% | 0% |
| `wan-100` | 50 ms | 25 ms | 1% | 0% |
| `wan-burst` | 40 ms | 30 ms | 2%, 25% correlation | 1% |

Note: `wan-50/100` are scenario names, **not measured RTT guarantees**.
Profiles and receipts must label the actual egress direction. The native
PS5 performance summary now reports `network_rtt_valid`, `network_rtt_ms`,
`network_rtt_variance_ms`; only call this observed RTT when `valid=1`.
PyroWave does not yet export that summary: use its HUD/network statistics or
an external RTT measurement and label its measurement source explicitly.

## Repeatable pre-hardware checks

```sh
PYTHONPATH=tools/test-lab/src:tests python3 -m unittest \
  tests.test_test_lab_netem tests.test_network_jitter_replay \
  tests.test_frame_pacing_policy tests.test_ps5_pacing_replay \
  tests.test_audio_pcm_policy tests.test_audio_jitter_policy \
  tests.test_frame_phase_clock tests.test_test_lab_analysis \
  tests.test_video_reorder_integration tests.test_video_fec_queue_replay \
  tests.test_deferred_idr_recovery \
  tests.test_presentation_mailbox_replay tests.test_playout_budget \
  tests.test_netem_calibration -v
```

The deterministic host-only tests use synthetic arrival/decode timelines to
check reserve caps, epoch resets, queue starvation and recovery; they **cannot**
establish audible quality or physical display latency. `netem` is required for
actual UDP traffic, and is only effective on a verified routed path.

## Adaptive receiver changes and measurement limits

- Fixed-refresh video now derives an optional **bounded** (at most 30 ms)
  playout reserve from the rolling p98 of positive completed-frame arrival
  variations over two seconds, requires repeated outliers, and releases the
  reserve gradually. RTP frame gaps do not reset previously learned protection.
  This adapts Nonary's statistical tail/slow-release principle, not its entire
  VRR/FFmpeg buffering implementation. VRR and decoder/GPU timing remain
  independently paced. `video_arrival_p98_us` is **not packet transit jitter**:
  the host encoder and UDP reassembly can affect completed-frame times.
- The audio output worker **always** feeds 512-frame AudioOut grains on its
  own hardware clock, including silence during startup/recovery or a short
  zero-padded/faded grain on an underrun. It does not block the hardware
  clock while waiting for a full ring. A 2 ms per-channel S16 fade smooths
  discontinuities after trimming/resume. Silence is still audible loss, not
  packet recovery. It starts with 20 ms of PCM prebuffer and escalates in
  10 ms steps after repeated underruns *or* qualified imminent underruns, up
  to **80 ms**. Successful quiet intervals permit shrink probes; a dropout
  within five seconds rolls the probe back and exponentially delays another.
  The 180 ms ring is a **hard ceiling**, not the target or added latency.
  A slow EWMA only sheds a 5 ms packet of sustained excess after two seconds
  above target + 15 ms; short packet clumps are preserved as jitter cover.
- Native `performance-last.json` exports video FEC recovered/failed, out-of-
  sequence packets, arrival p98/sample count/gaps, and audio target/peak/
  adjustments. Test Lab's `passive_link_quality` combines these as *limited*
  diagnostic evidence. It **does not** estimate available Mbps or automatically
  renegotiate encoder bitrate. Nonary's active UDP capacity probe requires
  explicit compatible host protocol support, a hardware validated traffic
  budget, and matching test evidence before it can be brought over safely.
- A separate **opt-in pre-stream probe** now implements the authenticated
  Nonary/Vibeshine `pyrowave-udp-probe` wire contract at one explicitly chosen
  bitrate. It runs only when explicitly requested on an idle host, caps the
  trial at 100 Mbps for two seconds, pins the paired HTTPS server certificate,
  authenticates the client certificate and UDP token/source/port, and verifies
  the host's independent sent count and duration. It uses 100 ms burst-loss
  windows, sequence-aware loss and arrival p99/drift. The Python receiver's
  timestamps are **user-space**, so the probe is a *diagnostic rate trial*,
  not the precise kernel-timestamp bandwidth calibration in Nonary. Normal
  Moonlight connections never send this probe: 404/405/501 from Sunshine or
  another host is `UNSUPPORTED`, not a streaming failure. No bitrate setting
  is changed from its result. **The current receiver runs on the Linux Test
  Lab coordinator**: it measures host→coordinator routing, *not automatically
  the actual PS5 path*. Do not use its result to set PS5 bitrate unless the
  shared route has been independently verified. A future native PS5 receiver
  needs a separate hardware-validated implementation.
- `av_startup_observation.offset_us` prefers the PS5 first **real PCM**
  AudioOut call start (falling back to the first output call on older builds)
  relative to first video-presentation **observation**. It helps identify
  gross startup regressions, but is **not measured lip sync**, since it lacks
  a shared source media PTS and physical audio/display output timestamps.
  Check synchronization with an external A/V flash/clap recording.
- `cadence_lanes` reports independent p95 interval deviations for source PTS,
  network enqueue, decoder-ready, and presentation-submit. These are
  **separate clock domains**. Do not subtract absolute timestamps between
  them. A large network-enqueue p95 with stable source PTS suggests delivery
  jitter; decoder-ready/presentation variation can be local PS5 work.
- `video_source_due_shadow` v2 is **observation only**. Its clock-offset model
  learns at the video callback entry on the local monotonic clock; it must not
  compare absolute Common-C `enqueueTimeUs` against native `ready_us`.
  `post_callback_delay_max_us` is time from callback entry to decoder readiness,
  not packet transit, physical scanout latency or an enabled presentation mode.
- `video_reorder` v2 separately counts obsolete video data and FEC parity;
  `late_data_rejected`/`late_parity_rejected` describe packets too late to
  enter the current FEC block, **not** unrecoverable frame loss. The real
  Common-C reorder helpers are exercised by a sanitized synthetic UDP/FEC
  queue replay; full Reed–Solomon decoding and Wi-Fi hardware are not emulated.
- `audio_startup_silence_grains` and `audio_active_silence_grains` distinguish
  pre-first-PCM silence from playback gaps. `audio_first_underrun_us` and
  `audio_last_underrun_us` are monotonic timestamps of policy underrun events,
  not audible dropout durations. All are diagnostic counters only.
- `video_phase_clock` is an **experimental**, native fixed-Paced-only
  cadence-phase model. It derives a bounded hold from source PTS and network
  enqueue, at most one frame and never more than the existing network budget.
  The frame deadline is the *maximum* of this hold and the existing pacer
  deadline: never add the same jitter reserve twice. VRR, Unpaced, PyroWave
  and normal sessions are unchanged. This is an A/B candidate, **not** a
  production default and **not** an A/V lip-sync controller.

To compare the optional phase model, duplicate a native H.264 or HEVC
`client.pacing="paced"` profile and add `"experimental_phase_clock": true`
inside its `capture` object. Run the unmodified profile immediately before
and after each experimental run, keeping netem/host/output unchanged. `false`
or omitted is the control. If the report shows
`EXPERIMENTAL_PHASE_CLOCK_NOT_ACTIVE`, discard the comparison. Keep the same
binary in both legs so any difference is caused by the profile toggle.

Optional rate trial **before starting the stream**, using an already-paired
client identity configured on the Linux Test Lab coordinator (do not upload,
copy into git or share private keys):

```sh
PYTHONPATH=tools/test-lab/src python3 -m pltest.active_link_probe \
  --host https://192.0.2.15:47984 \
  --host-cert /secure/paired-host.pem \
  --client-cert /secure/paired-client.pem --client-key /secure/paired-client.key \
  --kbps 20000 --packet-size 1200 \
  --evidence build/link-probe-20mbps.json \
  --allow-active-traffic-on-idle-host
```

The `pltest link-probe ...` alias also works. The host HTTPS endpoint must
implement the optional extension and accept that client identity; the
coordinator must be able to receive its UDP traffic through the firewall.
Check no RTSP session is active first. Exit 0 means this one chosen rate passed
the limited qualification; 1 means it did not, 3 means unsupported/inconclusive.
Never interpret a failed high-rate trial as a game-stream decoder problem.

## Initial hardware gate for PR #70

Run **one LAN and one confirmed `wifi-congested` comparison** before expanding
the matrix. Use matching baseline/candidate builds (same PS5 firmware, host
settings and instrumenting flags within each pair), and record each exact
source SHA. The ordinary PR Actions artifact is built with `TEST_LAB=0` and
must **not** be passed to the automated Test Lab controller. The remote Test Lab
builder deliberately builds `TEST_LAB=1 PERFORMANCE_DETAIL=1 LAN_TELEMETRY=0`;
build both A and B that way. Do not infer equivalence from the build label.

Start with native H.264 1080p60, fixed Paced, `NATIVE_MAX_QUEUE_MS=0`,
`UNIFIED_PLAYOUT_EXPERIMENT=0` and phase-clock experiment disabled. Verify
the actual UDP egress with `--verify-video-udp`, capture qdisc loss receipts,
and compare frame age, presented FPS, true FEC recovery, reorder timeouts and
audio underruns/silence grains. If Test Lab metrics lack `video_reorder_*` on
a v2-enabled build, the measurement is incomplete and must not pass the gate.
Keep Draft until the paired measurements are reviewed.

## Extended qualification (only after the initial hardware gate)

Keep `NATIVE_MAX_QUEUE_MS=0` and omit
`capture.native_queue_limit_ms` throughout the WAN/Wi-Fi matrix. The native
overload experiment from PR #67 is a separate test with known heavy decoder
load; combining its opt-in IDR resynchronization with network impairment would
confound the attribution of video gaps. The combined build retains both
policies, with native-overload recovery disabled by default.

1. On PS5 13.60 and (if available) legacy 3.x, do three interleaved **A/B**
   repetitions per condition: PR #70 base commit `a180a56` (A), current branch
   head (B). For phase experiments, also compare B with/without the toggle.
   Confirm the same host encoder and display settings; record build
   SHA, applied netem JSON, packet counters, host FPS and actual RTT.
2. Start with `lan`, then `wifi-light`, `wifi-congested`, `wan-50`, `wan-100`,
   `wan-burst`. Stop after an unrecoverable decoder/audio failure, and save
   its packet/trace evidence. Avoid shaping a live production interface.
3. Each condition: H.264 1080p60, HEVC 1440p60, PyroWave 1440p60; if the
   initial matrix passes, additionally test 4K120 HEVC and PyroWave with VRR.
   Run both fixed Paced and Paced+VRR. Keep Unpaced as a LAN control.
4. For audio, compare `audio_pending_ms_high_water`, `audio_catchup_frames`,
   `audio_ring_overruns`, `audio_underruns`, `audio_underrun_wait_total_us`,
   `audio_underrun_wait_max_us`, `audio_startup_prebuffer_wait_us`,
   `audio_ring_mean_frames`, `audio_output_errors`,
   `audio_prebuffer_target_frames`, `audio_prebuffer_peak_frames`,
   `audio_prebuffer_increases`, `audio_prebuffer_decreases`,
   `audio_near_miss_events`, `audio_failed_shrink_probes`, `audio_silence_grains`,
   `audio_short_grains`, `audio_crossfades`, `audio_drift_shed_frames`,
   `audio_ring_ewma_frames`, RTP packet loss and audible dropout duration.
   Fixed AudioOut grains mean prebuffer/underrun wait is not hardware idle time;
   **silence and short-grain counters are now the better continuity signals**.
   The wait counters measure until real PCM is usable, **not exact HDMI silence**.
   Check A/V synchronization with external audio/video capture too.
5. For video, compare unique displayed FPS, lost/decode-drop/frame-age counts,
   ready-to-submit p95, `video_network_jitter_peak_us`,
   `video_network_jitter_bursts`, `video_arrival_p98_us`, sample/gap counts,
   `rtp_video_fec_recovered`/`rtp_video_fec_failed`, reordering,
   `passive_link_quality`, stale drops and time to recover from each
   burst. Compare `cadence_lanes` and optional `video_phase_clock` telemetry.
   Distinguish network enqueue variation from decoder/GPU stalls; do
   not mistake the source's low-motion frame rate for client frame loss.
6. Deliver immutable A/B paired reports, raw PS5 `performance-last.json`,
   host logs, network receipts, exact network topology and a brief subjective
   audio/visual check. Report numerical differences and keep uncertain/absent
   metrics as inconclusive rather than fabricating results.

**Acceptance:** no LAN regression beyond ordinary measurement noise; bounded
audio backlog without whole-queue flush; reduced audible underruns and video
stutter on impaired links; predictable recovery without growing end-to-end
latency over a multi-minute session. Physical input-to-photon latency still
requires external measurement.

### Independent loss qualification (LAN final hardening)

Keep `wifi-congested` unchanged for historical jitter comparisons: its correlated
1% setting has repeatedly produced near-zero measured drops. Use the additional
`wifi-loss-independent` profile for delay20±15ms, independent1% loss and0.2%
reorder. Run `netem_calibrate.py --scenario wifi-loss-independent` in its isolated
namespace first. Qualification requires the unique UDP deficit to match qdisc
drops within a bounded counting tolerance; unrelated receiver/socket loss must
not certify the model. This does not replace proving the actual PS5 video route.

A short LAN A/B must retain the same host capture device/topology, display mode,
scene, seed and version across the two runs. A host reboot or device renumbering
invalidates strict attribution of small FPS/timing changes. Preserve host rAF and
RTSP send-counter series separately from PS5 decoder throughput. Zero reorder
waiting with unchanged target deadlines is different evidence from a slower host.
