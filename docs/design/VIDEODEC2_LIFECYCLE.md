# Stream-aware VideoDec2 design

The native pull decode worker owns parameter acquisition, planning and decoder
replacement. Session negotiation and the loading display do not require a decoder.
There is no additional frame queue. The worker holds the existing first access
unit while creating its decoder; UDP receive continues through the bounded
moonlight-common-c queue. Cancellation is checked between allocation stages.

Parsed SPS facts are separate from `Plan` and native API configuration. H.264
uses Table A-1 MaxDpbMbs divided by coded macroblocks, capped at 16, without +1.
HEVC uses sublayer buffering and its own level-dependent MaxDpbSize. Missing H.264
VUI restrictions require the standard bound, rather than guessing encoder refs.
Native support is progressive 4:2:0 H.264 baseline/main/high 8-bit and HEVC
Main/Main10 matching the negotiated pixel format. Unsupported visible sizes fail
rather than silently presenting with the negotiated crop incorrectly.

## API evidence and assumptions

Public ABI declarations in this repository expose `max_dpb_frames`, not its
normative accounting contract. Context7's available homebrew documentation did
not resolve that contract. Standard DPB facts must not be described as native
hardware limits. The initial native plan reserves one separate output/current
picture beyond stream buffering. This is an explicit conservative API accounting
assumption requiring hardware confirmation; it is NOT part of the H.264 formula.
The operational ceiling is 17 (standard 16 plus that margin), subject to native
Query/Create acceptance. No claim that every capacity is supported is made.

A capacity error permits one distinct configuration using the stream's standard
DPB ceiling plus the same margin, only when larger than the active allocation.
It never retries an identical capacity. Total local recovery budget is two;
consecutive ordinary corruption is bounded to eight IDR recoveries, reset by a
successful output. Unsupported configuration/allocation failure fails closed.
No full session reconnect is attempted: this avoids restarting/terminating host
applications under uncertain ownership.

## Replacement barrier

Decode submissions are serialized on the decode worker. Replacement stops and
joins the presentation worker, retires native AGC GPU reads, invalidates mailbox
and decoder-held slots, deletes the old decoder and then releases its allocations.
An unsuccessful retirement/delete retains allocations and faults the session.
New frame references are only published after a new generation becomes ready.
No CPU copy of decoded pixels or persistent replacement presentation surface is
introduced. PyroWave bypasses this entire owner.

## References

- [PR17](https://github.com/LedyBacer/ProsperoLight/pull/17)
- [PR51](https://github.com/LedyBacer/ProsperoLight/pull/51)
- [H.264 level table](https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/h264_levels.c)
- [H.264 parameter parsing](https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/h264_ps.c)

The six-slot workaround and reported 13-slot VAAPI result are historical evidence,
not independent reproduction. Intel VAAPI/QSV 1440p60 remains a pre-merge test.

## Test Lab reproduction and merge gates

Templates are in `tools/test-lab/profiles/video-lifecycle/`. They deliberately use
an invalid hostname and zero source identity. Copy outside the repository, fill
in the already-paired host endpoint, exact source SHA/build label and validated
build manifest. Follow [Test Lab](../TEST_LAB.md) and [smoke/recovery](../TEST_LAB_SMOKE.md)
for normal-exit, deployment readback, configuration/pairing and lease safeguards.
They are inputs to the existing coordinator, not a new benchmark runner.

Use `decoder-smoke` first, then supported `decoder-matrix` profiles (10s warmup,
20s simple / 40s noise-motion / 20s recovery). `pyrowave-matrix` is only a limited
compatibility check. HDR/120Hz need matching display/host support; skipped rows
must be reported as unverified. Do not alter host encoder settings automatically.
Intel templates name an expected encoder but do not select one: the contributor
must independently record actual VAAPI/QSV backend, encoder version/settings and
matching authenticated host evidence before claiming that regression reproduced.

The existing `performance-last.json` gains lifecycle/DPB/allocation counters and
PS5-local first-ready/decoded/presented timestamps. `analysis.decoder_lifecycle`
keeps these separate from RTSP negotiation. Old builds explicitly report lifecycle
evidence unavailable. Recovered native faults remain measurement-invalid, even
when recovery completed functionally; they cannot become performance improvements.

For startup/cancel/reconnect use coordinator normal stop with an owned run and
repeat with a fresh UUID. Unit/mock tests deterministically exercise parameter
changes and capacity errors; no corruption/injection is enabled in production.
Live encoder parameter switching is not manufactured by a separate payload.
Unsupported visible-resolution changes currently fail explicitly: the negotiated
presentation mode remains fixed, so changing crop/VideoOut without renegotiation
would be unsafe. Coded-size/DPB/profile changes within that mode are replanned.

For performance use existing `pltest paired` with >=3 baseline/candidate pairs,
identical flags/stream settings/stimulus/environment and complete session exclusion
evidence. Compare startup separately from steady-state decode/queue/frame-age
p95/p99, actual received bitrate, drops and memory. Report NOT_COMPARABLE or
INCONCLUSIVE honestly. Software timestamps are not physical input-to-photon data.

Pre-merge requirements: native PS5 functional runs on available matrix rows,
independent Intel VAAPI and QSV 1440p60 evidence, and a valid paired performance
comparison. Historical logs and host mocks alone do not satisfy these gates.

The supplied historical VAAPI log was located and inspected locally: it records
successful decoder creation followed by repeated `811d0302`, 84 refreshes and
zero decoded/presented frames. It contains no independently parsed SPS fixture;
the nine-reference claim remains contributor-reported. No private raw log is
published by this change.

Mode-specific historical levels are not hardware ceilings: the planner permits
SPS levels up to the highest values already requested by the native 4K120 path
(H.264 6.0 / HEVC 5.2), with actual Query/Create acceptance still mandatory.
A valid higher declared level at a smaller resolution must not be rejected just
because the previous resolution table used a lower default.

The developer-only `hevc-capacity-recovery-probe` profile injects exactly one
synthetic `0x811d0302` result after active decoding/presentation. It exercises the
real native retirement/recreation, IDR and resumed-output path. This is not an
observation of a spontaneous hardware fault. Its telemetry records the injected
count; functional recovery must pass, but measurement remains inconclusive and
cannot enter a performance comparison. Production builds cannot enable the probe.
Fatal lifecycle failures and failed local recovery attempts have separate counters;
a startup rejection is not reported as a failed recovery. HEVC reference telemetry
is a buffering-derived upper bound, not a count of explicit reference pictures.

The final review adds bounded PPS/slice-selected pending SPS transitions, effective
decode/present affinity application behind the existing join barrier, and packet-
independent startup/transition deadlines. See [the targeted review report](../VIDEODEC2_REVIEW_VALIDATION.md)
for the conservative parameter-ID policy, cancellation guarantees, reconnect ownership
blocker, real encoder fixtures and synthetic first-AU recovery hardware evidence.
