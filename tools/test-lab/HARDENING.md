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

No hardware run has validated these new changes yet. The earlier PR7 receipts
are historical evidence and do not satisfy this task's acceptance scenario.

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
