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
Use a Linux gateway for return-path testing; avoid shaping the test coordinator
control connection. Packet-loss/jitter patterns are probabilistic, so identical
parameters do not produce bit-for-bit identical losses. Record actual conditions.

Commands (Linux with `tc` and `ip`; root/CAP_NET_ADMIN required):

```sh
PYTHONPATH=tools/test-lab/src python3 -m pltest.netem \
  --interface ps5-egress --scenario wifi-congested \
  --evidence build/netem-wifi-congested.json -- \
  python3 tools/test-lab/pltest_cli.py run \
    --suite network-jitter --profile tools/test-lab/profiles/network/h264-1080p60.json
```

Alternative: `pltest netem ...` if the package is installed. For non-Linux
hosts, run the impairment command **on the gateway**, then use your normal
Test Lab coordinator; the wrapper must remain alive until the test completes.
The wrapper collects the `tc -j qdisc` state, captures the test exit code,
deletes only its own qdisc on completion/error/Ctrl+C and writes the cleanup
receipt. For a hard kill or power loss, inspect `tc -j qdisc show dev IFACE`
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
Profiles and output receipts must label the actual egress direction.

## Test plan for Codex

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
   `audio_ring_overruns`, `audio_underruns`, `audio_output_errors`, RTP packet
   loss and audible dropout duration. A low queue-overflow count alone is not
   success; check late audio and A/V synchronization too.
5. For video, compare unique displayed FPS, lost/decode-drop/frame-age counts,
   ready-to-submit p95, stale drops and time to recover from each burst. Do
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
