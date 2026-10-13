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
