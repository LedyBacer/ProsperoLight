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
