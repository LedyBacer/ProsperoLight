# Test Lab hardening acceptance ledger

This ledger tracks the full hardening task on `codex/test-lab`. A checked
implementation item is not hardware acceptance.

## Decoder setup evidence and result semantics

Receipt schema `prosperolight-test-receipt/2` records successful Limelight
renderer setup parameters after the native renderer validates its decoder
configuration or PyroWave successfully initializes its backend. The raw format
identity determines codec, chroma and bit depth. Width, height and FPS describe
the negotiated stream configuration, not measured capture or presentation FPS.
Failed setup leaves negotiation null. Requested options never substitute for
this evidence. Schema 1 receipts remain readable but cannot prove negotiation.

The analyzer checks every material negotiated field and reports
`NEGOTIATED_FIELD_UNVERIFIED:<field>` or `NEGOTIATED_FIELD_MISMATCH:<field>`.
For VideoDec2, the final renderer snapshot supplies accepted pipeline depth,
affinity, cores, VSync and output dimensions/refresh. Bitrate is labelled a
configured ceiling; `actual_mbps` remains measured received bytes per second.
Software output state does not prove physical TV scanout or VRR.

Terminal results distinguish:

| Field | Meaning |
| --- | --- |
| `execution_state` | Completed workflow, failed workflow, cancellation or uncertain infrastructure |
| `functional_verdict` | Streaming health evidenced by receipt and backend diagnostics |
| `measurement_validity` | Whether required performance evidence is trustworthy |
| `performance_delta` | Comparison conclusion; individual runs are `NOT_COMPARABLE` |
| `overall_verdict` | Agent-facing conclusion combining the preceding fields |

A completed real workflow can have functional PASS and overall INCONCLUSIVE.
`verdict` and `summary_verdict` are compatibility aliases of `overall_verdict`
for newly archived results. Simulated success is `SIMULATED`, with CLI exit 3;
only a verified real PASS returns exit 0. Historical immutable archives are
preserved. API run views expose separate classifications without rewriting them.

## Remaining acceptance work

- Complete backend-specific effective pacing/HDR evidence and host ownership evidence.
- Per-variant manifests and safe interleaved deployment of distinct binaries.
- GPU adapter selection, trustworthy host telemetry and WebGL2 workloads.
- Phase acknowledgements, uncertainty windows and continuous progress diagnostics.
- Expanded sweeps, 4K120/HDR/pacing suites and A/A calibration.
- Recovery/security audit and complete checks.
- Real 1080p60 acceptance followed by supported advanced modes.
- Single-command three-pair physical A/B across two source commits.

The current physical acceptance checkpoint below validates one new native smoke.
The earlier PR7 receipts remain historical evidence and do not satisfy this task's
new acceptance scenario. Advanced modes and cross-build acceptance remain pending.

## Prepared cross-build workflow

The coordinator opts into deployment by setting `PLTEST_BUILD_ROOT` to a local
prepared artifact root. Keep baseline and candidate app directories and their
`build/build-provenance.json` files independently under that root. Build on the
configured Linux/WSL SDK, using separate clean source checkouts and the same
flags (`TEST_LAB=1 PERFORMANCE_DETAIL=1 LAN_TELEMETRY=0`); Mac coordinators can
consume these prebuilt artifacts without an SDK. A manifest binds every file,
source commit, developer label, eboot hash and complete compiler definitions.

```sh
pltest prepare-variant --directory "$ROOT/A" --provenance "$ROOT/A-provenance.json" \
  --profile smoke.json --manifest-root "$ROOT" --output "$ROOT/baseline.json"
pltest prepare-variant --directory "$ROOT/B" --provenance "$ROOT/B-provenance.json" \
  --profile smoke.json --manifest-root "$ROOT" --output "$ROOT/candidate.json"
pltest paired --baseline "$ROOT/baseline.json" --candidate "$ROOT/candidate.json" \
  --manifest-root "$ROOT" --repetitions 3 --allow-deploy
```

The coordinator must have the same root-relative manifests and artifacts.
The CLI validates both variants before creating any run. Equal source commits,
equal binaries, incompatible flags, missing provenance, symlinks and altered
file manifests are refused. Each run revalidates the local artifact manifest,
acquires the remote lease, proves TITLE_IDLE, archives prior evidence, deploys
with full FTP readback, and verifies installed label/hash before launch.
Execution order is A1/B1/A2/B2/A3/B3. Each receipt archive includes installed
identity and the selected manifest. An uncertain deployment retains the remote
lease and executor journal; no automatic retry or production restoration occurs.
A terminal invalid measurement stops the sequence with exit 3 rather than
reporting a successful performance comparison.

This prepared-artifact path does not yet automate obtaining clean source
checkouts and invoking remote Linux/WSL builds. Hardware cross-build acceptance
has not been run.

## Host telemetry and workloads

NVML inventory is enumerated without assuming the encoder is device 0. Optional
`PLTEST_GPU_UUID` selects an adapter explicitly. Missing or unmatched selection
leaves utilization null with a reason. Explicit selection identifies the sampled
adapter but does not independently prove that Vibepollo encoded on it. Samples
record host monotonic time, actual sampler interval and NVML's encoder sample
period. Utilization never becomes per-frame latency.

Host log slices optionally extract `Video encode call duration (min/max/avg)`
CPU summaries when Vibepollo already logs them. No logging configuration is
changed. Per-frame capture/encode completion timestamps remain unavailable;
periodic CPU call duration is not GPU encoding latency. Audit of the installed
Vibepollo 2.0.0 source (`8a8c4b03a280ab9f567beb380110abb80f5220b8`)
confirmed authenticated RTSP inventory and hashed stored session tokens. Logs
show disconnect/end events, which alone do not establish all-client exclusivity.

`host_stimulus.scene="gpu-entropy"` uses WebGL2 fragment shader uint hashing;
only scalar uniforms change per frame. No full-frame CPU upload or readback
occurs during measurement. Preparation reads a small deterministic pixel sample
for hashing. Optional `entropy` (0..1), `movement` (0..8), `update_hz` (1..240),
`render_width` and `render_height` control workload; reported rAF frequency is
separate from content update frequency and requested stream FPS. Unsupported
WebGL2/shaders explicitly reject the scenario; compatibility Canvas scenes
remain available. `test-pattern` supplies independent color bars with a moving
marker. Video-file playback remains optional and is not implemented.

## Authenticated runtime session evidence

The optional coordinator host adapter reads `PLTEST_HOST_API_TOKEN_FILE` and
connects to `PLTEST_HOST_API_URL` over verified HTTPS. For a self-signed host
certificate without a hostname SAN, `PLTEST_HOST_API_CERT_SHA256` pins its DER
SHA256 fingerprint obtained through an independently trusted channel. No TLS
verification bypass is used. Keep the token outside Git with owner-only access.
The needed read scopes are `/api/clients/list` and `/api/rtsp/sessions`;
`/api/metadata` and `/api/logs` allow diagnostics. Only explicit `disconnect`
policy uses the additional POST `/api/clients/disconnect` scope. Default policy
rejects a busy host and does not disconnect clients.

During measurement the coordinator samples the authenticated RTSP inventory,
records monotonic request bounds and sanitized session configuration, and rejects
multiple sessions, identity replacement or loss after a session was observed.
Decoder callback dimensions/FPS originate in client StreamConfig and alone do
not prove independent server acceptance. VALID negotiation now also requires
matching server session dimensions, FPS, codec, HDR, chroma and bitrate evidence.
Server `encode_latency_ms` describes processing through packetization, while
`stream_gpu_model` identifies the capture display adapter; neither proves
isolated GPU encode duration or encoder adapter identity.

A scoped token has been provisioned and real authenticated idle inventory
confirmed on the configured host. This is API validation, not hardware benchmark
acceptance of the new workload or cross-build comparison.

## Phase and continuous-progress evidence

Worker start acknowledgements include a browser-local timestamp and a matching
page command ID. The coordinator archives both start ACKs with coordinator
monotonic request/response bounds. `phase_alignment` reports an interval for
PS5 minus browser time, including command delivery uncertainty. It does not
claim end-to-end clock drift verification; cross-clock steady-state phase
comparisons remain unavailable until an independent end anchor is recorded.
Browser reports use explicit run-qualified phase IDs and observed rAF boundaries.
Intervals crossing a transition are excluded from browser-local phase statistics.
Warmup duration comes from the profile rather than a fixed ten-second label.

Native traces now include longest received/completed no-progress intervals,
frame-gap distributions and stall frequency including measurement edges. One
second without completion while callbacks continue is a client presentation
stall. Missing callbacks remains input starvation or reduced host capture;
static content remains explicitly ambiguous. A few early frames followed by a
freeze cannot produce VALID evidence. PyroWave completion continuity still needs
backend-specific observation evidence; its post-present CPU timestamps must not
be substituted for unique completed-picture times.

PyroWave receipts additionally record successful backend output initialization:
accepted swapchain extent, display-mode refresh, FIFO/immediate behavior, HDR
colorspace and pacing policy. These fields describe driver configuration, not
physical HDR/VRR scanout. Missing fields or a 120-to-60 Hz fallback are rejected.
Native pacing is verified from the final renderer trace mode.

## Binary identity and A/A screening

Variant manifests bind the complete provenance file SHA256 and source-tree digest
as well as source commit, compiler definitions and every deployed file. Comparisons
require confirmed installed identity matching the selected build variant, stable
identity across repetitions, and different commits and binaries for A/B.
`pltest compare --pairs pairs.json --calibration` explicitly permits identical
builds for A/A, reports observed paired noise and whether the selected screening
thresholds exceed that noise. `--screening thresholds.json` overrides the validated
suite screening values; this remains a heuristic, not statistical proof.

## Current physical acceptance checkpoint

Real HEVC 1080p60 smoke `371bc3cb-ac4c-4dca-be78-90f4a38e75f9` completed with
`functional_verdict=PASS`, `measurement_validity=VALID`, `overall_verdict=PASS`.
It deployed baseline source `b40671e74a035f0f6783321ccc461e68e2f580ed`, eboot
`307a5789ded53bf5b9c0dce52d86d2c3916fcdb71402d48e597e8e4f3815e8c4`,
with all 131 files checked against the manifest. Independent RTSP session evidence
matched codec/HDR/chroma/dimensions/FPS. Observed unique output was 59.907 FPS;
requested bitrate was 20 Mbps, server encoder configuration 14.988 Mbps, and
measured traffic 1.388 Mbps for the low-motion scene. The title exited normally,
artifacts were fresh, and the analysis recorded no issues. This proves one native
smoke, not advanced-mode or two-build comparison acceptance.

Earlier failed attempts remain INCONCLUSIVE: one exposed a receipt /2 collector
compatibility gap, another exceeded its explicitly configured 120s deployment
stage budget. The latter's retained lease was released only after full 131-file
readback reconciliation and TITLE_IDLE proof; no launch replay occurred. Hardware
deployment runs now use an explicit 300s stage budget and 600s total budget.

Real WebGL2 high-entropy smoke `3e629107-56b0-46af-87f2-4b660a8f1123` also
completed PASS/VALID on the same verified baseline binary. Browser rAF measured
239.965 Hz with 1920x1080 rendering, entropy=1, movement=1 and 240 Hz content
updates. Confirmed unique output measured 59.878 FPS and traffic 15.073 Mbps;
analysis recorded no issues and title exit was verified. Config/pairing hashes
matched the pre-smoke snapshot. These are software timing observations, not
HDMI/input-to-photon measurements.
