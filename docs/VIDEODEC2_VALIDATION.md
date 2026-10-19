# VideoDec2 hardware validation

This is the original validation at the source below. See [targeted final-review validation](VIDEODEC2_REVIEW_VALIDATION.md) for later lifecycle fixes and their dedicated hardware run.

Tested code: `74a5e8bba86a5826da73577a82fd3c6e18e67f4b`.
OpenGL SDK 1.0.2; developer label `test-lab-videodec2-74a5e8b`.
Candidate eboot SHA256: `c59ecc018c6a24d06bde2f783027f8346b5dc798addd993e65a0b19384df5b30`.

Real PS5 with drakmor/ftpsrv 1.16-ng-stable and Windows Vibepollo 2.0.1.
Current-session capture evidence identifies NVENC. Both endpoints use Ethernet (operator confirmed).
All hardware runs use the existing Test Lab coordinator. No private raw artifacts are published.

## Functional results

| Scenario | Execution / functional / measurement | Presented FPS | Run ID |
|---|---|---:|---|
| ab-candidate-1 | COMPLETED / PASS / VALID | 59.929 | `34fefc83-6090-40d6-ab28-fd1089bf2839` |
| ab-candidate-2 | COMPLETED / PASS / VALID | 59.903 | `47ede86a-bbf9-48e1-8853-c6c09c9626a1` |
| ab-candidate-3 | COMPLETED / PASS / VALID | 59.924 | `58755c8a-3941-4f5d-9f9c-1247acccb3c0` |
| capacity-recovery | COMPLETED / PASS / INCONCLUSIVE | 59.921 | `ec9b411e-e482-4858-841e-8a4475456423` |
| h264-1080p60 | COMPLETED / PASS / VALID | 59.913 | `8d49220a-2450-4c0d-8240-06279e4484b1` |
| h264-1440p60-nvenc | COMPLETED / PASS / VALID | 59.900 | `a29291aa-85d5-4180-aedc-b6cc8f78bc05` |
| h264-2160p60 | COMPLETED / PASS / VALID | 59.887 | `e3c630e9-43d4-4b83-b9c2-cd53f8f56cda` |
| hevc-1440p60-adaptive | COMPLETED / PASS / VALID | 59.920 | `dfed5bff-1aae-495a-85f0-79f8e66e5cae` |
| hevc-2160p120 | COMPLETED / PASS / VALID | 119.842 | `0d715456-ef2f-43c4-a863-2ec88d8a659a` |
| hevc-2160p60 | COMPLETED / PASS / VALID | 59.846 | `139ae794-5ec5-46a0-bec0-c15984663088` |
| hevc-main10-2160p120 | COMPLETED / PASS / VALID | 119.833 | `223d8ebc-e9b6-4b1d-98f6-af08298555e8` |
| hevc-main10-2160p60 | COMPLETED / PASS / VALID | 59.924 | `7f180529-63e9-4dbe-877b-3af1ccfdd84e` |
| pyrowave-1080p60 | COMPLETED / PASS / VALID | 59.053 | `2a9e0538-d224-4d3f-a24f-4f659d32646f` |
| startup-cancel | CANCELLED / CANCELLED / INCONCLUSIVE | — | `d266d484-a537-4b4d-b861-89f04d252b86` |

## Paired performance screening

Three interleaved baseline/candidate pairs: **PASS / VALID / NO_CLEAR_CHANGE**.
Baseline source: `b1b2c5d8cde17347419091b95c30fa95f22fedd0`; candidate source as above.
Identical build flags, stream settings and environment fingerprints were required by the existing comparison implementation. Thresholds were not changed.
No A/A calibration was supplied. The paired result is a screening heuristic, not statistical proof or evidence of a physical latency improvement.

The recovery probe injects one **synthetic** capacity-error result. Decoder retirement, GPU quiescence, native recreation and subsequent output occur on real PS5 hardware. This is not a spontaneous hardware-error reproduction; its timings are excluded from performance comparison.

Startup cancellation is expected to report CANCELLED, not PASS. Normal exit and cleanup must be verified independently.

## Cleanup and integrity

All 17 terminal runs have matching receipts and verified raw/export artifact manifests. Each installation has 133 files verified by final-destination SIZE/SHA256 readback; normal title exit is verified for every run.
Saved configuration/pairing hashes are unchanged, the original shader cache was restored and read back, the remote lease is absent, and the host has zero active clients/RTSP sessions. The owned Windows worker exited normally (scheduled task Ready, result 0).

## Evidence and limits

Per-run source/build identity, report/export hashes, lifecycle counters and metrics are recorded in [the redacted summary](VIDEODEC2_VALIDATION.json).
Software timestamps do not measure physical scanout or input-to-photon latency.
The baseline lacks the new initialization/allocation counters, so these runs do not establish a like-for-like startup-latency or allocated-byte delta. Candidate absolute values are retained in the summary.
These are short integration runs, not extended 4K120 endurance tests or visual HDR/VRR correctness certification.
HEVC reference telemetry is a buffering-derived upper bound. The separate native current/output-picture margin remains an SDK-accounting assumption, subject to Query/Create acceptance.

Independent Intel VAAPI and QSV 1440p60 reproduction remains a pre-merge requirement. NVENC at the same resolution does not satisfy it.
Visible-resolution switching outside the negotiated mode remains explicitly unsupported. Live encoder-driven SPS switching is not established by the synthetic recovery probe.

See [the design and Test Lab profiles](design/VIDEODEC2_LIFECYCLE.md) and [smoke reproduction/recovery](TEST_LAB_SMOKE.md).
