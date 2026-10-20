# Encoder-independent video: engineering and validation report

Branch: `codex/decoder-portability`. Review: [PR65](https://github.com/LedyBacer/ProsperoLight/pull/65).
See [design and pinned client-source research](ENCODER_AGNOSTIC_PIPELINE.md),
[Test Lab](../TEST_LAB.md) and [reproduction/recovery](../TEST_LAB_SMOKE.md).

## Scope and design review

PR61's single decoder owner, validated SPS/PPS activation and zero-copy AGC
surfaces are retained. Initial allocation comes from the actual codec stream,
not the encoder name. Optional bounded PPS-prefix diagnostics, actual sampled
slices, picture types and AU distributions complement existing timing counters.
Two-second snapshots add no presenter locks or per-frame logging. Test Lab
uses actual compressed bytes and independently bounded host sender counters
where available; those counters do not isolate capture time from encoding time.

The runtime policy requires sustained active backlog, not low presentation FPS
alone. It can try depth 1 once for an already pipelined, zero-reorder stream with
measured buffered latency, return to the prior accepted configuration once,
or request one IDR at depth 1 to discard obsolete queued pictures. Native faults
and SPS/PPS changes invalidate comparisons. Candidate Query rejection retains
the old decoder; creation failure attempts the previous accepted depth. Effective
DPB capacity and accepted CPU mask are preserved. Healthy streams take no action.

No extra frame queue, CPU expansion, upward depth speculation, software decoder,
quality reduction or proprietary bitrate protocol was added. Previous project
slice/depth research does not justify those changes. A client cannot split an
arbitrary compressed slice into independent slices; host cooperation may be
necessary for slow one-slice streams. HEVC tile/WPP facts are diagnostic, not
H.264 heuristics. PyroWave implementation is unchanged.

Final review covered repeated tuning, rollback limits, source-FPS ambiguity,
loss plus decode pressure, native fault invalidation, incomplete bootstrap,
resource retirement and effective resource limits. Deterministic tests exercise
these cases; hardware below exercises the healthy path, not forced recovery.

## Focused checks

- SPS/lifecycle and new policy tests: Linux ASan/UBSan PASS.
- Included-source native owner mock: ASan/UBSan PASS, including complete versus
  incomplete IDR bootstrap, Query refusal, failed candidate creation with restore,
  accepted CPU fallback retention, and PPS diagnostics surviving shutdown.
- New Python diagnosis/profile tests: 11 PASS. Existing analysis tests: 23 PASS.
- Initial CI exposed an existing short-TTL coordinator heartbeat test failure;
  that exact case passed once locally and the complete CI run for `955978b`
  subsequently passed. No scheduler code or verdict thresholds
  were changed to force a pass. Current CI is linked from the PR.
- SDK builds succeeded for the measured `955978b` image. Final report-only/Test Lab
  confirmation fixes are built separately; build identity is kept with each run.
  Correctness findings required additional builds; no hardware matrix was run.

## Hardware setup and original two runs

Real PS5 via existing Test Lab and native VideoDec2/AGC. Windows i5-14600KF,
RTX5070Ti, Vibepollo 2.0.1. Both devices use Ethernet, as confirmed by the operator.
H.264 SDR 1080p60, configured ceiling 20 Mbps, Classic/depth 1, three-core layout,
10 s warmup and 30 s measurement, deterministic low-motion browser stimulus.
Encoder names below come from current-session host creation logs, not GPU identity.

Measured commit: `955978bcc518d7ce526717cf53c3bdcf79a28053`.
Label: `test-lab-portability-955978b`.
Eboot SHA256: `9d82243371f3fd0959560da02214f0fd3fc50c1123d1714078b523faeda8dbe2`.
All 139 installation files were verified through the deployment adapter.

| Measurement | Software | NVENC control |
| --- | ---: | ---: |
| Actual encoder | libx264 | h264_nvenc |
| Run ID | b133a885-923f-4eaa-8b2a-a1961af70987 | 134b2771-3d6c-4967-beba-25bc2ad4654e |
| Functional / measurement / overall | FAIL / VALID / FAIL | PASS / VALID / PASS |
| Unique presented FPS | 59.887 | 59.913 |
| Dequeued AU FPS | 59.920 | 59.946 |
| Actual compressed Mbps (full measurement) | 0.809 | 0.797 |
| Decode call mean / p95 / p99, ms | 1.860 / 1.969 / 2.027 | 1.833 / 1.923 / 3.215 |
| Callback-to-ready mean / p99, ms | 1.985 / 2.152 | 1.958 / 3.372 |
| Callback-to-flip mean / p99, ms | 10.666 / 11.565 | 16.146 / 16.539 |
| Maximum pending AUs | 0 | 0 |
| Network frame loss / queue overflow | 0 / 0 | 0 / 0 |
| Decode / present errors | 0 / 0 | 0 / 0 |
| Depth trials / rollback / IDR resync | 0 / 0 / 0 | 0 / 0 / 0 |
| Stage diagnosis | healthy | healthy |

Both streams: coded 1920x1088/visible 1920x1080, High profile 100, Level 42, 8-bit,
one reference frame, no reordering,
stream DPB 1/native capacity 2, requested and observed eight slices, effective
CPU mask 0x3f. No B pictures were observed. These matching simple structures do
not reproduce the contributor's higher-reference or potentially one-slice QSV case.
The low-motion scene genuinely produces far less than the 20 Mbps ceiling.

Software host sender observation was phase-bounded with 97.998% coverage,
59.928 FPS nominal and59.898–59.958 FPS bounds. Its narrower diagnostic subwindow
reports 0.689 Mbps; the table uses the complete measurement for both runs.
NVENC's independently phase-bounded host observation was unavailable; its
healthy classification relies on client delivery/queue evidence, without claiming
independent capture/encode FPS. Software timestamps are not scanout or physical
input-to-photon measurements. Uncontrolled display phase makes the two flip-age
means unsuitable for an encoder latency comparison. Neither run is a main-versus-
branch performance A/B experiment.

### Software exit finding and targeted correction

The first software stream measured successfully and the title exited normally,
but its receipt correctly failed with `HOST_EXIT_CLEANUP_UNCONFIRMED`. The trace
shows `/cancel` acknowledged HTTP 200, then the paired serverinfo confirmation
exceeded its 2 s read budget. A subsequent authenticated inventory found zero
active clients and zero RTSP sessions. The original failure is retained unchanged.

The correction gives only active Test Lab's post-cancel confirmation the ordinary
7 s bounded HTTP budget. Cancellation itself is not retried, unconfirmed state
is still a failure, and production's existing exit timeout is unchanged. This
specific finding justifies one additional short software confirmation run.

Hardware also exposed a summary bug: shutdown cleared PPS caches before writing
CABAC/tile/WPP facts. The corrected summary snapshots the last successfully
submitted PPS facts, while still clearing live activation state on worker join.
The original reports' `stream_pps_features_known=false` means unknown, not CABAC
or WPP proven disabled; it must not be reinterpreted as a bitstream measurement.

### Corrected software confirmation

Hardware-validated code commit: `8e7747c3b02ea028d22ec1d9cf471abf66ba9fe2`.
SDK build completed successfully with label `test-lab-portability-8e7747c`,
eboot SHA256 `06c0bcf3d1a4af60b19ed43095cc702f8bbdf28e5e61de5fcd28da45c1d0fbf5`.

Run `00efbd8a-b571-42de-8b3f-2054b8fdc672`: **COMPLETED / functional PASS /
measurement VALID / overall PASS**. Confirmed libx264, same 1080p60 configuration,
10 s warmup and a deliberately shorter 10 s measurement to check the specific
exit/summary fixes. Unique presentation 59.693 FPS, dequeued 59.793 AU/s,
actual compressed 1.690 Mbps, Decode mean/p95/p99: 1.977/2.028/4.956 ms,
callback-to-ready mean 2.105 ms. Maximum pending 0, no queue overflow, network
frame loss, decode or presentation errors. Policy actions 0; diagnosis healthy.
SPS/reference/depth facts match the original software run; PPS diagnostics are
now known and confirm CABAC enabled. Normal title exit, paired preflight cleanup
and paired exit cleanup are all verified in the matching receipt. The original
failed report remains intact; it has not been relabeled PASS.

**Actual hardware test count: 3** (original software, NVENC control, one justified
software correction check). No additional NVENC run: the final changes only
preserve summary fields and alter active Test Lab's post-stream confirmation.
There were three local SDK build invocations during final validation because
review/hardware findings required correctness fixes; the earlier image was not
used to claim verification of later changes. No full matrix or endurance run.

## Evidence integrity and preservation

Raw reports, host evidence, trace CSVs, receipts and deployment provenance remain
in ignored local evidence storage; no credentials, pairing or raw private
artifacts are committed. SHA256 of the captured complete report JSON files:

- Software: `cb2f11db6e4c006cdcb61e36bb4b8d03388ac69a7eef7ffe5af50ce98874d9e6`.
- NVENC: `288ec833f9e9508a1b67b63d8f82272ffc0ba8ad428a2233f941c5742af50db9`.
- Corrected software: `7b18fa37e650b5349ebeb44e1049f54917f9f45983baca484ae8f4380df369b0`.

The prior working installation was backed up and restored after the original
pair: 139 files with final-destination readback, original eboot hash, all 12 saved
configuration and two pairing-file hashes unchanged. After the final correction
check, the same restoration/readback and configuration/pairing comparisons passed
again. Original host application objects/order and global API configuration were
read back unchanged; only the separate software lab app remains available for
reproduction. Final authenticated host inventory: zero active clients and zero
RTSP sessions. The task's worker was shut down normally and its SSH tunnel closed.

## Remaining independent validation

No Intel contributor was contacted. Mac VideoToolbox was skipped: independent
pairing/permissions setup would add work without resolving the observed issue.
No new HEVC/HDR/VRR,4K120, PyroWave, endurance or physical latency certification
is claimed. Those paths retain their existing design and deterministic coverage.

For the later VAAPI/QSV comparison, ask for the same active scene and settings,
actual host production and encoder logs, full client trace and native summary.
Compare actual slice counts, SPS profile/level/reference/reorder/DPB, CABAC,
AU-size/actual bitrate, native call and output-ready distributions, queue trend
and unique presentation. If the host sends 51 FPS and the client delivers 51 FPS
without growing backlog, investigate host capture/encode. If the host sends 60
and decode delivers 48 with growing queue and sustained native service pressure,
inspect actual slice complexity/concurrency. Do not identify the cause from the
encoder brand or configured bitrate alone. The bounded depth trial and rollback
need real pressure-path validation before claiming a hardware latency improvement.

## Delivery and review status

Ready for independent code review, not merged. Review the bounded policy and
read the hardware evidence with the limitations above; these tests demonstrate
software-encoder compatibility and healthy NVENC delivery, not improved Intel
throughput or a universal decoder speedup. Independent pressure-path and Intel
validation remain follow-up work. Latest CI status is shown on PR65.

Implementation history: `108d577` characterization/diagnosis; `61a7955` bounded
policy; `0d0291a` isolated host support and integration; `955978b` comparison and
effective-limit fixes; `8e7747c` final report/exit-confirmation fixes. The final
documentation commit records these results without changing the measured code.

## PR65 final review corrections

- `d0f2d49`: absolute 20s trial expiry is enforced before invalid/loss/low-sample
  filters, including sampler resets and the first safe resumed-source observation.
  One rollback is scheduled without interpreting invalid data as a performance
  regression; existing success, native-fault, IDR and retry rules are preserved.
- `b5edf1f`: missing/invalid arrival evidence produces null input FPS/bitrate,
  explicit unavailable/completeness fields and insufficient full-pipeline evidence.
  Client submission-to-presentation observations remain separate. No report
  consumer relied on arithmetic with these new diagnosis fields outside the
  diagnosis module; JSON exports preserve null. Verdict thresholds are unchanged.
- Focused final tests: 14 bottleneck Python cases PASS; policy and native-owner
  mocks PASS under ASan/UBSan. New cases cover loss, insufficient/invalid windows,
  uncomparable trials, timeout/one rollback/no oscillation, source pause, missing/
  partial/invalid arrivals, real zero observed arrivals and independent-host limits.
- Native reconfiguration was **not** hardware-validated in this pass. Existing
  Test Lab has capacity probes but no ready controlled depth-transition/rollback
  scenario, and deeper native creation cannot be assumed. Automatic depth trials
  are now disabled in production by the existing experimental decoder-controls
  gate. Reporting and retained-instance bounded IDR resync remain available.
  The native transition gap is therefore gated, not relabeled as a hardware PASS.
- New PS5 launches: **0**. Prior three-run evidence above is preserved unchanged;
  no software/NVENC benchmark, HDR/VRR,4K120, PyroWave or matrix was repeated.
- One final SDK build is performed for code `b5edf1f`; its result/build identity
  and current CI status are recorded in the PR description. No deployment is
  required to ship the gated implementation. A real native transition remains
  required before enabling automatic decoder recreation in production.

Recommendation: suitable for merge with the depth-trial gate disabled, after
current CI passes. No merge is performed by this task. Enabling the experimental
gate or claiming a hardware tuning benefit is not part of that recommendation.

The review CI repeated the previously recorded coordinator heartbeat test flake.
The test now deterministically ages leases before one real heartbeat/recovery
iteration, uses a fixed wall clock, and verifies both live-owner preservation
and orphan recovery. This removes the API-thread write race and 60ms runner-load
assumption; no production scheduler, TTL or verdict behavior was changed. Only
this affected coordinator case was rerun locally after the CI finding.
