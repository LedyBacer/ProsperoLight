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

The final native source at this buffering stage was
`18b9c274280dec14451b50f2fb638453a81959df`.
Its additional priming reserve, frame accounting and packet-clock ordering fix
passed host checks. The final image SHA256 was
`0623f6056cd9d5e721a9e416a7b9369d7cd5b9f0c85a44e6819f4131ff0bd631`.
Run `ac69216b-dbc5-4f87-a7c9-2b7076aca213` stopped INCONCLUSIVE during deployment
with its lease retained. Matching-owner reconciliation read back all 133 final
files and verified the idle title before releasing that lease. A subsequent
no-deploy run `fb1095c9-6040-4fae-aaf4-0ad0b1b9d3b3` timed out during preflight.
Neither run is a final-binary hardware PASS.

On 2026-10-16 the operator reported that gameplay generally works and explicitly
stopped hardware testing, then explicitly authorized latency-profile comparisons.
The [latency/coexistence follow-up](DUALSENSE_WAVEFORM_LATENCY.md) now records
three independently identified real-PS5 runs on the newer native source, including
Balanced's improvement and Low latency's starvation counter. The intermediate run
above must not be represented as verification of later code.

Private/raw evidence stays in ignored local build directories. No claim is made
about frequency response, physical input-to-photon latency, native-game fidelity,
multi-controller physical acceptance, older hosts, PyroWave, HDR/VRR or 4K120.
