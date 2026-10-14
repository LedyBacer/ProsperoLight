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

## Acceptance status

Verified physical smoke, high-entropy, phased decoder regression and a full
single-command three-pair cross-build A/B are recorded below with identities.
Implementation includes telemetry, progress, alignment, benchmark plans, A/A,
safe deployment, authenticated ownership checks and immutable export.

Fresh A/A calibration, calibrated cross-build A/B and physical
4K120/HDR/chroma/pacing/soak acceptance are recorded in the dated sections below.
Generated plans alone are never hardware PASS.
Unavailable capture/encode timestamps, encoder GPU identity and physical scanout
remain explicit missing evidence rather than inferred measurements.

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

The prepared-artifact path also supports `pltest build-variant` on a configured
Linux/WSL builder. Verified remote builds and physical cross-build acceptance
are recorded below.

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
PS5 minus browser time, including command delivery uncertainty. Legacy binaries only supply approximate alignment. Current binaries additionally
supply the independent end anchors described below, enabling conservative
steady-state windows with explicit synchronization uncertainty.
Browser reports use explicit run-qualified phase IDs and observed rAF boundaries.
Intervals crossing a transition are excluded from browser-local phase statistics.
Warmup duration comes from the profile rather than a fixed ten-second label.

Native traces now include longest received/completed no-progress intervals,
frame-gap distributions and stall frequency including measurement edges. One
second without completion while callbacks continue is a client presentation
stall. Missing callbacks remains input starvation or reduced host capture;
static content remains explicitly ambiguous. A few early frames followed by a
freeze cannot produce VALID evidence. Legacy PyroWave receipts lack completion continuity. Current receipts additionally
record the confirmed unique completion counter sampled by the control worker, as
described below. Post-present CPU timestamps never substitute for unique
completed-picture times.

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

## Clean remote builds and observed environment

`pltest build-variant` now builds a requested full source SHA in a fresh detached
Linux SDK worktree, downloads its developer image and provenance, and prepares
the deployment manifest. It never builds native PS5 code on the Mac coordinator.
The explicit builder JSON has schema `prosperolight-linux-builder/1`, `ssh_host`,
`identity_file`, `repository`, HTTPS `repository_url`, `workspace_root`,
`dependencies`, `runtime_bin`, and `jobs` (1–4, use 2 on this builder). These paths
are machine-local configuration; credentials do not belong in profiles or Git.

```sh
pltest build-variant --builder builder.json --source-sha "$FULL_SOURCE_SHA" \
  --directory "$ROOT/B" --provenance "$ROOT/B-provenance.json" \
  --profile smoke.json --manifest-root "$ROOT" --output "$ROOT/candidate.json"
```

The command was validated against source
`8773a184082ead066da851859a0f2ad479bdd408` on the configured Linux builder.
Its signed eboot SHA256 was
`0096eb6ed8baa33c02f3f048ddb1bcb2b2584c83797130a0d74891e6b0aa7638`.
This identifies that tested artifact; later source changes require another build.

Environment evidence now uses the read-only PS5 `kernel_get_fw_version` probe,
authenticated Vibepollo metadata, accepted RTSP settings, and capture selection
from the current host log after a verified idle snapshot. Historical capture
entries and rotated/replaced log prefixes cannot prove this run's output.
Firmware is stored as its raw unsigned identifier, without an inferred human
version. Both endpoints use Ethernet, confirmed by the operator on 2026-10-13;
this is explicitly operator-supplied evidence rather than automatic link detection.
Missing independent fields make the fingerprint incomplete; profile assertions
cannot set `verified` or substitute for firmware, host version, or encoder data.

Report publication compares JSON-normalized values: tuple-valued internal
measurements cannot leave a committed terminal result stranded in a `.pending`
file. Recovery reconstructs report files and updates their immutable manifest
hashes from the committed journal. A/A calibration uses every pair's absolute
latency delta, rather than letting opposite large errors cancel in the median.
Progress diagnostics correlate overlapping input and completion gap intervals;
an unrelated earlier input gap cannot explain a later client presentation stall.

The first interleaved physical series yielded A1/B1/A2 PASS, then B2 stopped
with `HOST_SESSION_LOST_DURING_MEASUREMENT` and `CLEANUP_FAILED`. The recovered
PS5 receipt independently reported `COMPLETE`, functional PASS, and complete
artifacts; a read-only probe confirmed TITLE_IDLE. Full 131-file readback
matched B before releasing that exact run's lease, and config/pairing hashes
remained unchanged. This exposed a terminal-delivery race rather than proven
stream failure. The host monitor now accepts RTSP disappearance only after the
complete duration measured from its start ACK; disappearance earlier still
fails. Cleanup waits for the receipt when PS5 is already stopping with COMPLETE,
instead of sending an unnecessary cancellation. That first series stays
INCONCLUSIVE; its failed result is not relabelled as acceptance.

## Completion polling and bounded phase comparison

New receipts include backend-independent continuous progress from the existing
confirmed unique completion counter, sampled only by the control worker.
They record the longest no-progress span, stalled intervals, maximum polling
period, sample count, and a fixed logarithmic gap histogram. These are sampled
completion intervals, not individual frame jitter or HDMI timing. If the worker
itself is delayed beyond 250 ms, diagnosis is uncertain. For PyroWave, missing
progress evidence or unresolved sustained stalls prevent a streaming-health PASS
in analysis; intentional static content has its own unresolved classification.

New status files advertise `clock_probe`; the coordinator sends that read-only
command only to titles advertising the capability. Browser probes require a
matching run and command ACK. Late measurement probes bracket each local clock
with coordinator request/response timestamps. Reports retain separate clock
offset intervals, endpoint drift bounds, matching local start/end phase IDs,
and conservative phase windows that exclude transition uncertainty. They never
subtract raw timestamps from unrelated clocks. The clock rate is assumed stable
between the observed anchors; the interval is explicitly reported. Old binaries
retain approximate alignment and cannot validate decoder-matrix phase comparison.

Native steady-state phase metrics use only callback and completion timestamps
within the conservative PS5-local windows. Control mailbox replay now rejects
reuse of one command ID for a different action. Duplicate identical commands
remain idempotent. These changes require a newly built image and refreshed worker;
unit checks do not substitute for their subsequent hardware acceptance.

## Physical cross-build acceptance: 2026-10-13

A single `pltest paired --repetitions 3 --allow-deploy` completed the full
A1/B1/A2/B2/A3/B3 sequence with PASS/VALID for all six runs. Baseline source was
`b40671e74a035f0f6783321ccc461e68e2f580ed`, signed eboot
`307a5789ded53bf5b9c0dce52d86d2c3916fcdb71402d48e597e8e4f3815e8c4`.
Candidate source was `8773a184082ead066da851859a0f2ad479bdd408`, signed eboot
`0096eb6ed8baa33c02f3f048ddb1bcb2b2584c83797130a0d74891e6b0aa7638`.
Both used identical complete compiler definitions, HEVC 1080p60 SDR 4:2:0,
20 Mbps ceiling, and the same seeded WebGL2 entropy workload on DISPLAY15.

| Order | Run | Unique completion FPS |
| --- | --- | --- |
| A1 | `594cb77e-b985-41dd-b094-ae69fcfadd9f` | 59.745 |
| B1 | `964f14ce-8fe6-47bc-b162-bad77005b7af` | 59.910 |
| A2 | `7b8d7907-a1d2-4e85-aeac-0a94ce7747f8` | 59.669 |
| B2 | `54ee80c5-5388-498a-a9f5-d6bb28ba3461` | 59.874 |
| A3 | `386f0875-d185-4af0-894a-2d0586f606e9` | 59.912 |
| B3 | `decc820e-8b2c-474d-b73c-c67a054c8893` | 59.904 |

Comparison returned PASS, VALID, `NO_CLEAR_CHANGE`, three independent pairs,
reason `PAIRED_SCREENING`, CLI exit 0. This is screening under the documented
thresholds, not proof that the binaries are universally equivalent. Each run's
51-file archive manifest was independently verified. Configuration and pairing
hashes matched the original baseline after the sequence. Installation remains
the tested developer image; no production-restoration claim is made.

Completion polling and clock-probe changes in source `37659f6` were committed
after these artifacts were built and are not covered by this acceptance series.

## Sequential sweeps, mode plans and host HDR evidence

```sh
pltest sweep --profile smoke.json --bitrates 10,20,40,60,80
pltest benchmark-plan --profile smoke.json --name native-4k120 --output-dir plan-4k
pltest benchmark-plan --profile smoke.json --name pyrowave-chroma-hdr --output-dir plan-pyro
pltest paired --baseline "$ROOT/phase-native.json" --candidate "$ROOT/phase-native.json" \
  --manifest-root "$ROOT" --repetitions 3 --allow-deploy --calibration
```

Sweeps run high-entropy points in ascending requested Mbps, archive every report,
and stop on invalid health/evidence. An optional stop identifies a measured
throughput plateau accompanied by a >5% completion-FPS drop. A compressed/static
scene falling below its bitrate ceiling is not saturation evidence. This stop is
screening, not proof of an encoder bottleneck. Missing measured throughput is
explicitly inconclusive, including backends without a trusted byte measurement.
Plans include native and PyroWave 4K120, HEVC HDR10, PyroWave 4:2:0/4:4:4 SDR/HDR,
unpaced/paced/VRR, segmented soak, and WebGL2 20/40/20 decoder-regression phases.
Generated files request capabilities; their creation never establishes support.

Windows inventory now reads active paths with QueryDisplayConfig and requests
HDR-specific GET_ADVANCED_COLOR_INFO_2. This distinguishes HDR support, user
enablement, and current HDR mode from wide color gamut. Unsupported OS queries,
ambiguous cloned display paths, and missing topology stay null/unverified. No
DisplayConfigSetDeviceInfo or display configuration mutation is performed.
The definitions follow the [Microsoft SDK header](https://github.com/microsoft/win32metadata/blob/main/generation/WinSDK/RecompiledIdlHeaders/um/wingdi.h).
Older advanced-color capability bits are intentionally insufficient for HDR.

The dashboard separately displays installed identity, source SHA, build flags,
effective fields with evidence sources, environment, clock uncertainty, progress,
missing evidence and cleanup errors. Its comparison form supports explicit A/A
calibration and screening overrides. CLI reports retain the same structured data.

Source `37659f691513cea8deb2ec070ce79f2378b5c68f` built successfully, with signed
eboot `d4d7d6d8524c5f9d1920336b8ab6399bf397e9e5c799682fdbd7464bebff9a5f`.
Its new hardware smoke `4544b49a-4865-4b08-b4c8-d86515b1578a` was PASS/VALID:
300 completion polls, zero stalls, maximum polling interval 115868 µs, matching
start/late/end identities. The original broad FTP request timing gave a 1.096 s
clock uncertainty and correctly did not validate steady phase comparison. The
control transport now brackets mailbox rename through matching ACK read, excluding
pre-command upload/readback checks while retaining all actual execution uncertainty.
The historical smoke report is not retroactively changed to claim finer precision.

The subsequent 90-second decoder-regression run
`bd599b79-c882-4459-b2d0-489ca05ecf7c` was PASS/VALID with no issues, fresh
artifacts, and verified title exit. The actual WebGL2 phase program executed
20/40/20 measured seconds after 10 seconds warmup. Start clock uncertainty was
393899 µs, late clock uncertainty 292162 µs, and the conservative offset envelope
422078 µs. Steady-state comparison was verified, with that uncertainty excluded
at each transition. Native phase completion rates were 59.888/59.932/59.912 FPS;
received throughput was 11.934/15.156/11.596 Mbps. These are software measurements,
not physical scanout timing. The earlier coarse-bound smoke remains unchanged.

Export now checks the original immutable artifact manifest, complete file set,
byte lengths, and SHA256 before redaction. Missing, modified, or unindexed files
refuse export; redaction then produces a separate manifest for transformed bytes.
A real 52-file redacted export from the accepted A/B series passed independent
hash verification. Tests cover post-publication mutation rejection.

Latest Linux validation completed `make test-lab-test`, full `make test`, and
`make lint` successfully; lint covered 280 attributed files after the native
phase/progress changes. Linux lacks Node for one optional pacing fixture; that
fixture passed locally on the Mac. Subsequent Python/JS changes have their own
focused tests and do not expand this native build validation claim.


## Nominal refresh validation and recovery checkpoint

PyroWave run `1c4eea38-1f68-438d-9ee4-350296181ef8` completed with a functional
PASS, verified title exit, 59.941 unique FPS and 300 completion polls without
stalls. Its original overall result remains INCONCLUSIVE: the analyzer incorrectly
classified a verified 5994/100 Hz output as fallback for nominal 60 FPS.
The validator now accepts the standard 1000/1001 refresh alias while preserving
the exact measured value. A true 60 Hz output for a 120 FPS request still fails.
Separate read-only reanalysis is VALID; the original immutable report is unchanged.
Regression tests accept 119.88 Hz for nominal 120 and reject 119 Hz.

A/A attempt `18139222-3beb-4116-a362-58f061569751` stopped before launch with
`NO_USER`. After the operator selected a profile, all 131 installed C files were
read back and matched; TITLE_IDLE was verified and that exact owner's retained
lease was released. No launch was replayed. A fresh calibration uses new run IDs.


The subsequent A/A series retained four PASS/VALID measurements before its fifth
preflight was incorrectly expired by the coordinator's own recovery loop after
a heartbeat delay. Recovery now refreshes known live local owners first, then
recovers orphaned leases. It still cancels if another owner deleted/replaced the
lease, and never takes over an uncertain PS5 lease. Tests cover both a delayed
live heartbeat and genuine ownership loss. The failed run and original reports
remain unchanged; any continuation creates fresh independent run IDs.

Packaging validation built a wheel and verified the new host/display modules,
WebGL2 assets, dashboard assets, recipes and control transport are included.


## High refresh readiness and HDR checkpoint

The first 4K120 attempt `cb837002-b337-4d0d-9272-b9a8dbc8654f` safely ended
INCONCLUSIVE before measurement: the actual virtual display was 3840x2160 at
480 Hz, but a 600-frame readiness buffer could never span its required two
seconds. Ready sampling now retains a bounded time window and tests cover
60/240/480/1000 Hz. The rAF upper bound uses the independently inventoried
current display refresh, while retaining the requested workload minimum and
reporting actual rAF separately from content updates and stream FPS.
A subsequent PyroWave 4K120 attempt `a734bfba-ad74-46e0-8d82-44fd9425eb71`
refused the old upper rAF check; neither historical attempt is relabelled PASS.
Blank timeout errors now carry a machine-readable exception code.

Fresh PyroWave smoke `2276f302-bb05-44e7-91a8-07210e092d67` completed PASS/VALID
with the nominal-refresh correction. After graceful worker update, HEVC HDR10
run `e8bb7e2c-505c-4a05-bcd0-ffdf9041da25` also completed PASS/VALID with verified
title exit and fresh artifacts. This proves that requested mode on this setup,
not physical HDR display calibration or HDMI scanout.


## Calibration and metric-specific screening

Six independent C A/A measurements were collected, with an interruption before
the final pair: `d12e4d0a-e178-4c47-b4d9-d44b8eb8060a`,
`61331ffc-4b05-4467-b56d-fbb010920398`, `18a39cce-1855-41da-9fb6-21816680f310`,
`f0f10638-1e7a-42df-9ba2-4ad35f6c09fb`, `f3641558-8e4c-4f4a-9c31-31e3e980c47d`,
and `639c2644-c411-4c18-8a11-b253692e5db4`. Every measurement was PASS/VALID,
but default calibration was INCONCLUSIVE: maximum paired FPS variation 0.105%,
callback-to-ready variation 1.858%, and frame-age p95 variation up to 12864 µs.
The defaults are usable for FPS and callback-to-ready, not for frame-age.
This is observed software timing noise, not proof of physical latency changes.

Comparison now reports noise usability per metric and allows explicit thresholds
per metric using a suite-specific JSON file, for example:
`{"metric_thresholds":{"frame_age_us":{"fraction":0.10,"absolute":15000}}}`.
Those values are illustrative, not universal defaults or recommended TV latency.
All measured fields and validity requirements remain enforced. A consistent FPS
regression is screened independently of an unrelated noisy latency metric.

`pltest compare` and `pltest paired` accept `--calibration-pairs aa-pairs.json`
(and `--screening suite-screening.json`). The file identifies archived independent
A/A pairs in the same coordinator. A reference must have valid measurements,
matching environment and the exact baseline or candidate build. Reused runs,
incompatible references and noise above configured thresholds make the A/B
result NOT_COMPARABLE; paired preflight rejects known unusable noise before
any deployment. The dashboard exposes the same reference and threshold inputs.
Without a reference, calibration status is explicitly NOT_SUPPLIED and results
remain threshold screening, not causal or statistical proof.

A separate read-only reanalysis of the historical A/B identifies a frame-age
screening regression under the initial thresholds (paired differences
9347/4189/5385 µs). Its original NO_CLEAR_CHANGE archive remains unchanged.
The later A/A variability exceeds those differences, so a causal presentation
regression is not established by that uncalibrated reanalysis. A fresh matched
calibration and A/B are required to assess the current tool and workload.


Paired CLI validates screening files before creating any run, including falsey
non-object values. Invalid configuration cannot consume six hardware runs before
being rejected by comparison. Focused tests preserve this fail-closed boundary.


Fresh-checkout verification exposed a test invocation issue: an existing editable
installation could cause discovery to import Test Lab from an older checkout.
Make test targets now prepend absolute current-checkout module paths, including
subprocesses launched in another working directory. The initial stale-package
Linux results are not accepted as validation of the new source; checks are rerun
with the repository-bound import path.

High-refresh final measurement now uses the same rate gate as Ready: the minimum
is the requested stimulus cadence and the upper bound is the independently
observed display refresh. A 480 Hz display driving a 240 Hz content update is
reported as such. Measurement interval capacity uses that observed refresh;
overflow, missing duration, hidden/fullscreen events and incomplete measurements
still invalidate the result. Final reports expose the specific validity reasons.

Windows verification found a stale flat `pltest/` directory shadowing the editable
`src/pltest/` installation when the worker started from its old source directory.
Workers are now installed into a fresh commit-specific directory with complete
tracked-file hash readback. Served JavaScript hashes and the resolved runtime
module path are verified separately. Runtime reports include seven resolved source
hashes at worker creation (disk identity, not memory attestation). Comparison also
includes worker identity, the enforced authenticated host TLS fingerprint and GPU
observation selection/inventory, preventing mixed-environment comparisons.

Authenticated RTSP frame/byte counter observations now expose estimated sender
rates with coordinator-clock uncertainty bounds. They are not capture FPS, wire
throughput or exact PS5 measurement-phase rates. Missing counters remain unknown;
counter resets and session identity changes invalidate continuity. Dashboard
exports expose these observations and the worker runtime identity.

## Extended physical acceptance, 2026-10-13

All modes below were actually executed on PS5 `192.168.1.134` and the interactive
Windows/Vibepollo host `192.168.1.5`; both connections are operator-confirmed
Ethernet. Source C is `37659f691513cea8deb2ec070ce79f2378b5c68f`, signed eboot
`d4d7d6d8524c5f9d1920336b8ab6399bf397e9e5c799682fdbd7464bebff9a5f`.
Every listed run is PASS/VALID, with verified title exit and fresh artifacts.

| Mode | Run ID |
| --- | --- |
| PyroWave 1080p60 | `2276f302-bb05-44e7-91a8-07210e092d67` |
| HEVC HDR10 | `e8bb7e2c-505c-4a05-bcd0-ffdf9041da25` |
| PyroWave 420 HDR | `30cd7f80-d0d3-4c66-aeb2-b400fc27fcb6` |
| PyroWave 444 SDR | `05c97135-e581-475f-ba94-a385c8722c4e` |
| PyroWave 444 HDR | `c2490355-3428-44ce-9594-86184d230626` |
| Unpaced | `a911c758-4732-4097-8744-43232ea9f398` |
| Paced | `754b2e35-03ca-48fa-86be-a9d0499eaa35` |
| Paced + VRR software policy | `fdceaae5-6adb-45bf-a389-0441b6d2c8b3` |
| 120-second soak segment 1 | `aeddf527-2cc8-47a2-8a0b-dc2d5ed34695` |
| 120-second soak segment 2 | `97acacb3-aa70-44f0-afab-6ffcb236e531` |
| HEVC native 4K120, refreshed worker | `12f63acd-0b4d-453a-99eb-57e176f8f9e6` |
| PyroWave 4K120, refreshed worker | `7860a2d3-9678-4af9-ab18-7232d96a2acd` |

The first ten modes retain `build/test-lab-hardening/advanced/acceptance-summary.json`.
Fresh 4K120 retains `4k-final-worker-summary.json`, using worker source
`04e65ec41f02f3295b044fc7c1d34b7ae08f0d2c` and independently checked runtime
module/served asset hashes. Native measured 116.9696 unique FPS and 87.0691 received
Mbps; PyroWave measured 119.8794 unique FPS, while received Mbps is unavailable.
Host counter rates are separately bounded observations, not substitutes for those
backend metrics. Neither run observed a sustained completion stall. Earlier 4K
Ready/final validation failures remain INCONCLUSIVE in their original archives.
HDR acceptance verifies negotiated bit depth and the captured display's active
color mode; it does not establish physical TV color accuracy. VRR acceptance is
software-policy evidence, not physical scanout verification.

An exact changed-file inventory and subsystem explanations are in
[HARDENING_FILES.md](HARDENING_FILES.md).

## Final tool verification and interrupted sweep

Worker/coordinator source `04e65ec41f02f3295b044fc7c1d34b7ae08f0d2c` passed
`make test-lab-test` locally: 97 Test Lab + 5 decoder benchmark + 2 pacing checks.
The configured Linux builder also completed `make test-lab-test`, `make test`
(193 integration checks, four optional Node checks skipped), and `make lint`
(280 attributed files). Native unit, performance ownership guards and self-update
checks are included in `make test`. Linux Test Lab had three optional Node skips
and pacing one skip; their local counterparts ran. SDK signed native C images
were built and deployed as recorded above. The final Python wheel includes 58
entries; required workload/worker/dashboard assets were checked byte-for-byte.

Sweep point 20 Mbps, run `1dfe7705-5ebc-487d-81a9-4c0f230e932c`, passed with
59.9295 unique FPS and 15.1734 measured received Mbps. Point 40 Mbps,
`02e5febe-6a4b-4d70-91ff-27238a5d64dd`, timed out in preflight; 80 Mbps was not
started. This is not full sweep acceptance. Original evidence remains
INCONCLUSIVE. Following the operator pause, all 131 installed C files were
read back and verified, TITLE_IDLE was confirmed, and only that exact run's lease
was released; evidence is `bitrate-sweep-final/reconciliation.json`.
Config and pairing hashes still matched the original baseline on 2026-10-14.
A temporary Windows IP change was reverted by the operator. No endpoint override
was retained in source and no saved PS5 configuration was changed.

## Fresh matched calibration, 2026-10-14

A single six-run A/A command completed with every individual run PASS/VALID,
using source C and worker `04e65ec`. Independent pairs were:

- `1f7dbc58-381d-460b-86bd-14df1cc76048` / `20a9464e-55aa-4ec1-a4de-a7d8f8beec44`
- `b150f7cc-1338-4eff-8613-587197b2bbcd` / `9ad8a3fe-dfe8-4242-b45c-2c5b7b58d774`
- `718ca04b-2646-4fb5-a116-e69c336399cf` / `97fceb96-f19b-411c-9acf-8d4e78e1bc14`

The initial screening was not usable for frame age; its immutable CLI result is
exit 3 / AA_NOISE_CALIBRATION. FPS noise was 0.0438174%, so the initial 2%
threshold is unchanged. The calibrated latency absolute margin is maximum
observed absolute paired A/A p95 difference plus the initial 1000 µs margin:
8044 µs for frame age, 1280 µs for callback-to-ready, each with the initial 10%
fraction retained. Measurement-validity gates were not changed. The three-pair
calibration is usable under these explicit screening thresholds; it is an
empirical sample noise floor, not a population confidence bound. Presentation
changes smaller than the floor remain unresolved.

Commands, pairs, screening parameters and their provenance are retained under
`build/test-lab-hardening/calibrated-final/` (`aa-command.json`, `aa-pairs.json`,
`observed-noise-screening.json`, `screening-provenance.json`, `aa-calibrated.json`).
The subsequent A/B must match this exact environment, use independent run IDs and
include the verified calibration reference; otherwise comparison is rejected.

## Final calibrated cross-build acceptance, 2026-10-14

A fresh **single paired command**, following the independent six-run calibration,
completed A1/B1/A2/B2/A3/B3 without manual PS5 operation. Baseline source was
`8773a184082ead066da851859a0f2ad479bdd408`, eboot
`0096eb6ed8baa33c02f3f048ddb1bcb2b2584c83797130a0d74891e6b0aa7638`.
Candidate source was `37659f691513cea8deb2ec070ce79f2378b5c68f`, eboot
`d4d7d6d8524c5f9d1920336b8ab6399bf397e9e5c799682fdbd7464bebff9a5f`.
Full manifests, build flags, effective settings and a common environment were
verified for every run. This comparison used HEVC SDR 420 1080p60, 20 Mbps
configured ceiling, GPU entropy, ten-second warmup and thirty-second measurement.

| Pair | Baseline run | Candidate run | FPS baseline / candidate |
| --- | --- | --- | --- |
| 1 | `663fa527-c0d2-4a6c-8af1-78355d2bab93` | `ccf6c843-8213-4c56-b89e-58487968bdbd` | 59.9078 / 59.8822 |
| 2 | `61f746ac-f36f-4a54-bda5-05d98ea1bb9c` | `5d646b45-7672-4e33-8d51-fdfba9985c8c` | 59.9029 / 59.9160 |
| 3 | `5e2f706b-c9d6-4db6-b23b-94aeff31c35d` | `02d3abd8-684d-4ee0-a5ba-bb6c74cb0746` | 59.8802 / 59.9150 |

All six runs were PASS/VALID with verified title exit. Final CLI exit was **0**:
`overall_verdict=PASS`, `measurement_validity=VALID`,
`performance_delta=NO_CLEAR_CHANGE`, `calibration_status=VERIFIED`, three pairs.
Median FPS delta was +0.0219%; frame-age deltas were -312/-17735/+2438 µs and
callback-to-ready deltas -14/-111/+238 µs. No metric had a consistent change
exceeding its calibrated threshold across the three pairs. Ordered deltas and
first-to-last drift remain in the result rather than being hidden by an average.

The exact paired invocation (against the coordinator retaining the calibration
runs in `calibrated-final/coordinator`) is:

```sh
ROOT="$PWD/build/test-lab-hardening"
pltest paired --manifest-root "$ROOT/variants" --repetitions 3 --allow-deploy \
  --baseline "$ROOT/variants/candidate-native.json" \
  --candidate "$ROOT/variants/phase-native.json" \
  --calibration-pairs "$ROOT/calibrated-final/aa-pairs.json" \
  --screening "$ROOT/calibrated-final/observed-noise-screening.json"
```

The canonical machine-readable result is `calibrated-final/acceptance.json`;
`acceptance-verification.json` independently checks all twelve A/A + A/B archive
manifests, each installed identity, clean title exit and lack of stale artifacts.
A compact public evidence index, including every archive digest, is checked in as
[evidence/hardening-2026-10-14.json](evidence/hardening-2026-10-14.json).
Raw archives remain local ignored artifacts. They were not rewritten by analysis.

This demonstrates trustworthy automatic cross-source regression **screening for
the exercised profile**. NO_CLEAR_CHANGE does not prove absence of smaller
presentation changes, regressions at other resolutions, or universal performance
non-regression. The separate 4K120/HDR runs prove mode acceptance on source C;
they are not an A/B comparison at those modes.

## Remaining boundaries

- **Implementation gaps, optional backends:** video-file playback and a native
  Windows workload renderer are not implemented. Deterministic WebGL2 entropy,
  Canvas compatibility scenes and an independent test-pattern scenario exist.
  No identified mandatory implementation blocker remains for the demonstrated
  prepared-build A/B workflow.
- **Unavailable evidence:** the installed host exposes no trustworthy per-frame
  capture timestamps or isolated GPU encode completion timestamps. Capture FPS
  and GPU encode latency remain null. RTSP sender rates and processing-through-
  packetization latency are separately labelled. Explicit NVML GPU selection
  proves which GPU was sampled, not which adapter the encoder actually used.
- **Hardware measurement limits:** software VideoOut/HDR/VRR state cannot prove
  physical TV scanout, color accuracy, VRR cadence or input-to-photon latency.
  Additional external measurement equipment would be needed.
- **Statistical limits:** three paired samples support threshold screening, not
  a universal causal guarantee. The observed frame-age noise floor prevents
  conclusions about smaller changes. Other profiles require their own matched
  calibration and paired execution; the tooling accepts them without assuming
  compatibility with this profile.
- **Deployment state:** the last developer variant remains installed. Production
  restoration was not requested and is not claimed. Persistent pairing and
  settings are protected and independently hash-checked.

## Completed sweep and final resource verification, 2026-10-14

The fresh single-command sweep (`pltest sweep --profile profile.json --bitrates
20,40,80`) completed all three points, exit 0 / ALL_POINTS_COMPLETED, PASS/VALID:

| Requested ceiling Mbps | Received Mbps | Unique FPS | Run ID |
| --- | --- | --- | --- |
| 20 | 15.1749 | 59.8791 | `31775d83-22b9-4694-b8e2-b51e238c2b82` |
| 40 | 31.4634 | 59.8854 | `19733d59-e77b-40be-b9a9-a24387d85e88` |
| 80 | 62.5591 | 59.9127 | `d40945b9-dc7b-41ba-b3ea-716766264625` |

All three 51-file archive manifests were independently hash-verified. No plateau
with FPS loss triggered an early stop; this is not proof of an encoder's maximum
capacity. The original interrupted sweep remains INCONCLUSIVE, separately
retained. Fresh evidence is under `build/test-lab-hardening/bitrate-sweep-resumed`;
summary, point metrics and archive digests are also in the public evidence index.

Final verification confirmed TITLE_IDLE, zero connected host clients and RTSP
sessions, no remote executor lease, and unchanged original config/pairing hashes.
Installed C label and eboot were independently read back. The owned worker was
shut down cooperatively; scheduled task Ready, exit result 0, no listener on
8096. The coordinator and temporary Mac sleep inhibition were stopped. No forced
PS5 termination/reboot or production restoration occurred. The complete local
record is `build/test-lab-hardening/final-cleanup.json`.
