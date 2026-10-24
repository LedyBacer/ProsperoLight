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

Commands (Linux with `tc` and `ip`; root/CAP_NET_ADMIN required):

```sh
PYTHONPATH=tools/test-lab/src python3 -m pltest.netem \
  --interface ps5-egress --scenario wifi-congested \
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
through a packet capture/routing check. The wrapper records the test exit code;
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
python3 -m unittest tests.test_test_lab_netem tests.test_network_jitter_replay \
  tests.test_frame_pacing_policy tests.test_ps5_pacing_replay -v
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
- The audio output worker starts with 20 ms of PCM prebuffer, increases by
  10 ms after closely repeated underruns (up to 50 ms), then releases one step
  after 45 seconds of quiet and subsequent 30-second intervals. The bounded
  ring remains 64 ms; no unbounded buffering and no callback blocking.
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
- `av_startup_observation.offset_us` is the PS5 audio-output **call start**
  relative to first video-presentation **observation**. It helps identify
  gross startup regressions, but is **not measured lip sync**, since it lacks
  a shared source media PTS and physical audio/display output timestamps.
  Check synchronization with an external A/V flash/clap recording.

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

## Test plan for Codex

Keep `NATIVE_MAX_QUEUE_MS=0` and omit
`capture.native_queue_limit_ms` throughout the WAN/Wi-Fi matrix. The native
overload experiment from PR #67 is a separate test with known heavy decoder
load; combining its opt-in IDR resynchronization with network impairment would
confound the attribution of video gaps. The combined build retains both
policies, with native-overload recovery disabled by default.

1. On PS5 13.60 and (if available) legacy 3.x, do three interleaved **A/B**
   repetitions per condition: PR #70 base commit `a180a56` (A), current branch
   head (B). Confirm the same host encoder and display settings; record build
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
   `audio_prebuffer_increases`, `audio_prebuffer_decreases`, RTP packet loss and
   audible dropout duration. The wait counters measure time until enough PCM
   returns, **not exact HDMI silence**. Check A/V synchronization too.
5. For video, compare unique displayed FPS, lost/decode-drop/frame-age counts,
   ready-to-submit p95, `video_network_jitter_peak_us`,
   `video_network_jitter_bursts`, `video_arrival_p98_us`, sample/gap counts,
   `rtp_video_fec_recovered`/`rtp_video_fec_failed`, reordering,
   `passive_link_quality`, stale drops and time to recover from each
   burst. Distinguish network enqueue variation from decoder/GPU stalls; do
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
