# PR6 — analyzers, suites and paired comparisons

The legacy decoder benchmark and packaged Test Lab now share `decoder_analysis.py`.
The old CLI, timestamp translations and tests remain supported. Distributions add
median alongside mean, nearest-rank p95/p99 and maxima. `analysis.json` keeps
functional verdict, measurement validity and performance delta separate. Final
CLI exit uses `summary_verdict`: functional PASS with invalid comparison evidence
exits 3, not 0. A fake PASS remains explicitly simulated.

Suite definitions are bounded JSON-compatible YAML (`src/pltest/suites.yaml`):
smoke, 90s decoder-matrix (10s warmup, 20s simple, 40s noise-motion, 20s recovery),
bitrate-ramp points, pacing-vrr, PyroWave and segmented soak. Each actual run uses
one explicit profile. Native decoder controls are rejected for PyroWave. Soak
uses repeated <=120s measured segments; no claim that a truncated ring covers a
whole long session. The browser reuses preallocated patterns and records phases;
PS5 and browser timing remain independent, with approximate handshake alignment.

Native analysis filters samples to the receipt's PS5 measurement bounds. It
reports received/unique-completed FPS, actual measured Mbps (distinct from the
requested ceiling), decode/ready/queue/frame-age and flip interval distributions,
pending, superseded and refused samples. The existing five-second windows are
relative to trace start and identify partial windows. No raw unsynchronized
clock subtraction, GPU fence-as-scanout or input-to-photon claim is made.
PyroWave uses its own fresh output trace, backend-specific ready-to-submit and
post-present CPU timing; `observed_us` is not completion of that specific picture.
Confirmed unique measurement counts come from matching VideoOut shown arguments
against bounded developer-only request evidence. Repeats and skipped observations
are not manufactured into unique completed pictures. Production pacing feedback
and renderer behavior remain unchanged.

`POST /api/v1/compare` accepts either baseline/candidate UUIDs or `pairs` containing
3–20 distinct A/B pairs. `pltest compare --pairs pairs.json` and `pltest paired
--baseline A.json --candidate B.json --repetitions 3` expose the same screening.
Missing/invalid traces, network loss, faults, incomplete negotiated settings or
fingerprints prevent comparisons. The fingerprint requires identical stream,
renderer, firmware, build flags, host/encoder settings, selected display mode,
scene seed/content hashes and network class. Source/build identities are recorded
separately so changed code can be compared without changing the build flags.
`analysis_environment.verified=true` is explicit operator attestation of the
listed environmental facts, not automatic detection; it cannot override missing
measured/negotiated evidence. Never assert it for guesses.

Screening uses >=3 pairs: >2% FPS drop, or >10% and >1ms p95 latency increase.
The report includes ordered pair deltas, medians, range and first-to-last drift.
Mixed directions yield NO_CLEAR_CHANGE. These heuristics are not confidence
intervals or statistical proof. New functional decoder/present faults are FAIL.
Different codecs, chroma/HDR or renderer environments are NOT_COMPARABLE.

Each completed run archives profile, events, host report, stimulus settings,
analysis JSON, escaped HTML and a SHA256 manifest including the immutable final
report. Failed/cancelled runs receive bounded forensic archives and incomplete
classification. Nothing is uploaded automatically. See [onboarding](../../docs/TEST_LAB.md).

## Evidence

Real HEVC 1080p60/20Mbps smoke `c3dd9c49-bb08-4108-8dac-c84f71a2e6e8`
returned functional PASS, fresh artifacts and confirmed title exit: 59.815 unique
completed FPS, decode p95/p99 1746/2368us, callback-to-ready 1752/2384us.
Real 90s decoder-matrix `2d8a81b9-dc83-449a-9d7d-d94b6a037db5` also passed
functionally: 80.088s measured, 59.909 unique FPS, callback-to-ready p95/p99
3637/4147us; Windows stimulus measured 239.925 rAF FPS on the 240Hz virtual display.
Both used the previously verified `test-lab-pr5-420a820` developer app. Their
measurement validity is INCONCLUSIVE and delta NOT_COMPARABLE because the app
reports `negotiated:null`. These are diagnostic observations, not a proven
performance regression between different scenes.

First PyroWave attempt rejected unsupported native decoder controls; the corrected
profile reached stream-ready but receipt failed NO_MEASURE_FRAMES. This exposed
single-slot output evidence losing lagging feedback. Developer-only evidence and
backend-appropriate receipt artifacts were repaired; fresh-build hardware
acceptance is recorded below when executed. Synthetic tests cover three paired
regression/improvement cases, missing/mismatched environments, reuse, faults,
trace truncation, clock offsets, PyroWave repeat semantics and immutable hashes.

Implementation checkpoint: 62 portable lab tests and the legacy five decoder
benchmark tests pass on macOS. Linux full `make test` passed 158 Python tests
(3 Node skips) and native/mock/sanitizer checks. Fresh-build validation follows
the developer-only PyroWave repair; hardware results are not assumed.

## Final host verification and fresh-build boundary

Portable checks passed: 63 lab tests, five legacy decoder tests and two pacing
tool tests. Linux `make test` passed 159 Python tests (three Node skips), plus
native/mock/sanitizer checks. Full Clang Tidy passed before the final Python-only
review fixes; final source/metadata/shell lint passed for 270 attributed files.
The 0.7.0 wheel includes all dashboard assets and the six executable suites.

The fresh `test-lab-pr67-25157d2` developer app was built, signed, uploaded and
read back on PS5. Eboot SHA256:
`f9e13d7d77cfcb69a3c155c4b4e0134c655b25b91e415f2719f728cda3fd3848`.
Production config and pairing hashes remained unchanged. Two fresh-build
PyroWave attempts (`8eeae8cd-1c4a-442b-9d5e-4c25e57c87c9`,
`2655a7ac-94f4-45b1-8cd7-341021f07c97`) stopped before title launch at
`RAF_BELOW_TARGET`: fullscreen, visible and focused Chrome measured about
170 FPS on the unchanged 240 Hz virtual display. Cleanup completed without
errors. These runs do not validate the new PyroWave evidence counter or a
fresh-build stream. The earlier real native smoke/matrix results above remain
valid functional evidence; real paired performance validation remains pending
complete negotiated settings and a stable host stimulus.

Fresh-build acceptance after the operator closed the competing Moonlight session:
PyroWave `be167b38-667a-4c67-a978-1b6789aeab83` and HEVC
`5da15671-c158-4136-84ea-dc5edbaf2754` both returned functional PASS, fresh
artifacts and confirmed normal title exit. PyroWave confirmed 1802 unique
measurement pictures over 30.074s (59.920 FPS); host stimulus measured
239.897 rAF FPS. Ready-to-submit p95/p99 were 404/1576us, and the post-present
CPU observation timing was 26/40us, not physical scanout latency. These results
validate the repaired bounded developer-only output evidence on real hardware.
Preparing the virtual-display stimulus after the own PS5 stream became ready
removed the dependency on a separate Moonlight session. Measurement validity
remains INCONCLUSIVE: negotiated settings are missing and global host session
exclusion was not authenticated. Optional session guard behavior is host-tested;
its real authenticated disconnect has not been executed without API credentials.

## Published integration acceptance

Installed developer app: `test-lab-pr67-5fd48df`, native source
`5fd48df645979939a0125d5d31d994d05e6c3761`, TEST_LAB=1,
PERFORMANCE_DETAIL=1, LAN_TELEMETRY=0. Eboot SHA256:
`59c76198bb0c0aca545eba4ffc0167140ab5e4260fdfbdc4e194be82b050a185`.
All 131 deployed files passed readback. PS5 moved from 192.168.1.137 to
192.168.1.134 during validation; production settings were not edited for that move.

Final HEVC smoke `41dcf9c9-bd37-4e2b-9e09-adcf3f347aed` and PyroWave smoke
`31b1ba2b-f12c-47f2-9805-0acf55744aa2` returned functional PASS, fresh archived
artifacts and confirmed normal title exit. Both receipts confirm paired host
cleanup before launch and on exit, despite the profile containing quit_host=false.
The existing paired GameStream identity performs this cleanup; no administrator
API token was used. Before the final native run the host retained a virtual
1080p/240 Hz display. After each final run Windows returned to the physical
1440p/180 Hz display. Thus normal title exit and host-app closure are verified
separately, rather than conflated.

HEVC confirmed 1802 measurement pictures at 59.917 FPS. PyroWave confirmed
1786 pictures at 59.393 FPS over 30.071s; its trace contains 1802 output samples,
which are not all confirmed completed pictures. PyroWave ready-to-submit p95/p99
were 4053/4342us; post-present CPU observation p95/p99 were 44/58us.
These are software observations, not physical scanout or input-to-photon latency.
Measurement remains INCONCLUSIVE and comparisons NOT_COMPARABLE because
negotiated settings and authenticated global host-session exclusion are absent.
Three-pair regression decisions are verified with synthetic fixtures, not claimed
as a real baseline performance proof.

Real doctor returned PASS in an idle-host check. CLI/API returned identical
immutable persisted reports after coordinator restart; the CLI correctly returns
3 for the real INCONCLUSIVE summary. Both final bundled exports passed content
hash checks and left raw artifacts unchanged. The dashboard was exercised in an
in-app browser against the fake API: connect, doctor, create/start, terminal
classification/events/artifacts; no console errors were observed.

The final portable target passes 68 lab, five legacy decoder and two pacing
tool tests. Linux full tests passed 164 Python tests (three Node skips), plus
native/mock/sanitizer checks. Full Clang Tidy passed for the installed native
source; subsequent Python-only fixes passed source/metadata/shell lint.
Pairing and user-setting hashes were preserved; the existing
config/self-update-sequence bookkeeping file changed across developer builds.
Optional administrator API disconnect remains host-tested only because no
Vibepollo administrator token was provisioned.
