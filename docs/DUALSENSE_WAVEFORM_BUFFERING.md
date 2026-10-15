# Waveform buffering follow-up — 2026-10-15

## Changes

The initial native waveform MVP showed 81 drops and three sequence gaps during
an operator-observed seven-second left/right/both signal. The receive callback
could drop PCM while pad/diagnostic mutexes were held. Relative pacing also
added delays after immediate AudioOut returns, regardless of time already spent
in a previous blocking call.

The callback now copies into a bounded, owned 16-packet inbox without acquiring
pad/worker mutexes. Epochs prevent replay across disconnect, suppression and user
changes. The serialized Moonlight callback is the producer; a bounded atomic guard
rejects accidental concurrent producers with an explicit counter. Only the worker
consumes the inbox. Published packets are acquired before reading the clock, so a
concurrent packet cannot be falsely aged using an earlier timestamp.

An absolute 48kHz sample clock accounts for blocking AudioOut calls and permits
bounded catch-up. Playback initially accumulates for at least 16ms, targeting
768 frames (20ms timeout for a short fragment). Storage is capped at 3072 stereo
frames / 64ms, with a separate 16-packet inbox. These are storage/time policies,
not measured physical latency or an end-to-end latency bound. Inbox packets aged
60ms are discarded; a driver call stalled over 60ms clears pending playback.

Diagnostics separate overflow, full inbox, stale/rejected packets, concurrent
producers and output stalls. Input/dequeued frame totals detect missing samples.
`starvation` counts zero-filled frames followed by resumed PCM before native
ownership ends; final silence is kept in `padded` without becoming starvation.

## Verified evidence

- Linux lint and controller/performance/lifecycle guards passed.
- The deterministic regression models four 240-frame packets every 20ms and a
  driver blocking 20ms every fourth call. Final policy delivers all **48,000**
  accepted frames with no overflow, full inbox, contention, gaps or resumed-stream
  starvation. Existing sample-bit, sequence, failure, generation and cleanup tests
  also passed.
- Intermediate source `f5a887132731aa247eb1f2d7283cdee322fe52eb`, real PS5 +
  Vibepollo 2.0.1, HEVC SDR 1080p60, seven-second explicit source:
  run `64cfefdb-b8fe-4c4c-81ee-3a1451a6219c` completed PASS/VALID.
  RX 1404; drops/gaps/contention/overflow/full inbox/stale/rejected/stalls/output
  errors all **0**. Padded frames 2240, output calls 1326. That intermediate build
  did not yet distinguish resumed-stream starvation from final silence.
- Its deployment receipt verifies all 133 installation files by final-destination
  SIZE/SHA256 readback. Artifact/export integrity, idle title, released lease,
  zero host clients/RTSP sessions and unchanged config/pairing were checked.
- Physical vibration and distinguishable left/right sides were confirmed by the
  operator on the original native backend; see [initial report](DUALSENSE_WAVEFORM.md).

## Final hardware validation status

Final native source is `18b9c274280dec14451b50f2fb638453a81959df`.
Its additional priming reserve, frame accounting and packet-clock ordering fix
have passed host checks. Final hardware validation is pending; the intermediate
run must not be represented as verification of this final binary.

Private/raw evidence stays in ignored local build directories. No claim is made
about frequency response, physical input-to-photon latency, native-game fidelity,
multi-controller physical acceptance, older hosts, PyroWave, HDR/VRR or 4K120.
