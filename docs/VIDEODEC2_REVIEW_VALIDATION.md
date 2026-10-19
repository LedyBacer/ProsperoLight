# PR61 targeted review hardening

This follow-up reuses the prior [17-run NVENC validation](VIDEODEC2_VALIDATION.md).
No full decoder matrix, A/B rerun, endurance test or unchanged PyroWave/HDR/VRR/4K120
validation is scheduled. Independent Intel VAAPI/QSV testing is delegated and
remains a pre-merge requirement.

## Pending parameter policy

The Moonlight pull AU exposes SPS/PPS NAL entries and picture fragments, not an
active SPS identifier. The bounded tracker parses PPS-to-SPS and early slice PPS
IDs (H.264 and HEVC); it does not attempt full slice decoding. At most 32 SPS facts
and 256 PPS mappings are retained, within the codec-specific ID bounds.

An unreferenced future SPS/PPS is withheld from the native compressed-input copy;
known pictures using the existing SPS continue with the current decoder. A new or
reused SPS that a picture needs outside an IDR is held. Request one IDR per unresolved
transition, retain the old instance, and enforce a ten-second deadline independently
of packet arrivals. Returning old pictures cannot reset that pending request/deadline.

A new, recreated, or reset native instance requires the selected SPS and its matching
PPS in the same access unit as an actual IDR slice (H.264 type 5, HEVC type 19/20).
The unit's IDR label alone is insufficient: Moonlight can promote an AU beginning
with parameter sets to IDR, and its SPS/PPS ordering assertions do not repair missing
sets. Missing/unknown/conflicting mappings wait under the existing bounded deadline;
there is no sole-SPS fallback and no replay of cached raw NALs.

Resource equality and parameter activation identity are separate. Identical repeated
sets are a no-op. Different SPS content invalidates PPS bindings to that SPS ID;
a fresh PPS must bind the new generation. Resource-equivalent activation uses the
existing native Reset at a complete IDR, clearing reference/submission state without
reallocating decoder memory. Resource changes retain the existing replacement path.
The conservative Reset is needed because native in-place reference compatibility
for changed POC/scaling syntax is not established. CRA/BLA alone do not activate a
new configuration under this policy.

Learned mappings are distinct from sets successfully submitted to the native
instance. Creation/reset clears submitted knowledge; only successful Decode records
fresh selected sets. Unselected future PPS versions remain filtered. Cancellation
clears the bounded tracker after joining the decode owner. No new queues, threads,
raw-NAL caches, or pixel copies are introduced.

Parameter identity normalizes framing for one bounded SPS/PPS NAL: three-/four-byte
Annex-B prefixes and Annex-B trailing_zero_8bits are excluded. SPS/PPS RBSP ends
with rbsp_trailing_bits, so the final payload byte must be nonzero; unlike VCL NALs,
these parameter NALs have no CABAC zero words. This rule is restricted to explicitly
Annex-B-framed parameter sets. Bare NAL trailing bytes are retained conservatively.
The hash preserves the complete NAL header and remaining EBSP, including escape
bytes; this is not RBSP/semantic canonicalization. SPS still requires its existing
full supported-syntax/trailing-bit validation. PPS retains bounded ID parsing plus
invalid/dangling escape checks, not a full PPS parser. Invalid PPS semantics remain
subject to native decode rejection and existing bounded recovery. Framing changes
therefore preserve bindings/native knowledge; real POC/PPS/header/content changes
still follow the existing generation/activation rules. HEVC IDR-only activation
(types19/20, not CRA/BLA) is intentional and unchanged.

This final hardening is validated by focused parser/tracker and native-owner mocks,
not a new PS5 hardware run. Earlier hardware reports below retain their original
source/build identities. Intel VAAPI/QSV 1440p60 remains an external premerge gate.

## Effective affinity

Creation fallback still accepts only the requested decoder mask or historical
classic mask. Session receive/audio/control workers retain a stable reservation
outside the union of both masks. This is intentionally conservative: their current
thread-only placement API cannot safely migrate live foreign workers by changing a
configuration struct. The decode owner recalculates layout from the accepted mask,
applies and reads back its own placement before publishing new resources; the joined
and recreated presenter applies its new placement at thread entry. Telemetry describes
actual stable receive/other placement and effective decode/present placement separately
from requested decoder affinity. No unsupported thread-ID registry or live foreign
thread migration is introduced.

## Early failure, cancellation and timeouts

The developer-only `hevc-first-au-recovery-probe` injects one synthetic capacity
error at the first decode attempt. It records decoded/presented counters at injection;
both must be zero. Actual native retirement/recreation and resumed decoding are real
hardware operations when this profile runs, but the fault itself is synthetic.

The same replacement barrier handles a presenter still waiting for its first image
and an active loading worker. Cancellation is checked between Query, flexible CPU
allocation, direct GPU/shared allocation, Create, input/frame pools and Reset.
Deterministic mocks exercise eight interruption points and cancellation before SPS
and before first output. No asynchronous attempt to interrupt an in-flight SDK call
is made; cancellation takes effect when that synchronous call returns.

The existing decode worker uses nonblocking queue polling with bounded 1-ms waits
only while waiting for initial parameters or an unresolved transition. It checks the
deadline without another video packet. The steady-state worker retains blocking
`LiWaitForNextVideoFrame`; no timer thread or new frame queue is added.

## Session reconnect decision

Automatic reconnect remains **not implemented**. This is a supported-semantics
blocker, not an unbounded retry policy:

- `gs_start_app()` chooses global `current_game`/`resume`, generates fresh input keys,
  and requests a new RTSP URL. It has no exclusive session ownership/resume token.
- `LiStopConnection()` tears down local transport. Replaying its old RTSP URL/keys
  has no supported freshness or idempotency guarantee.
- Re-entering the full session path can call `gs_quit_app()`; Test Lab deliberately
  requires a fresh application session and performs paired `/cancel` cleanup.
  This is incompatible with a transport-only retry that must preserve the host app.

The future integration point is the session owner after `LiStopConnection()` and
before final host cleanup, never the decoder worker. It requires a host-owned session
identifier, an app-preserving reconnect/lease contract, and a session-wide one-attempt
budget. Until those guarantees exist, local bounded recovery ends with diagnostics
and normal owned cleanup. No automatic host launch, cancel or unrelated disconnect
has been added to satisfy a reconnect checkbox.

## Alternative encoders

Installed host metadata identifies Vibepollo 2.0.1 commit
`66e169c325dab73e4e780fe34ac3c613af4cdf52`. Its matching
[software encoder source](https://github.com/Nonary/Vibepollo/blob/66e169c325dab73e4e780fe34ac3c613af4cdf52/src/video.cpp)
defines `software` with `libx264` H.264. Encoder selection consumes `config::video.encoder`;
[configuration](https://github.com/Nonary/Vibepollo/blob/66e169c325dab73e4e780fe34ac3c613af4cdf52/src/config.cpp)
also permits persisted app/client overrides, including `encoder` and software presets.
Thus software streaming is possible in source, but a per-session encoder option is
not exposed by ProsperoLight's existing launch request. No app/client/global host
configuration was edited and no software-streaming hardware claim is made. Optional
coverage stops at real encoded fixtures to avoid expanding host configuration work.

FFmpeg 8.1.1/libx264 encoded six synthetic black 1080p frames for each additional
fixture, with `ref=1:bframes=0` Baseline level4.2 and `ref=8:bframes=2` High level5.1
(`preset=medium`, `keyint=60:scenecut=0:threads=2`). Independent `trace_headers`
reports `(refs, buffering, reorder) = (1,1,0)` and `(8,8,2)`; planned capacities are
2 and 9 including the explicit native margin. Existing independently encoded x264
1440p nine-reference and x265 Main/Main10 SPS fixtures remain covered. These establish
parser/planner behavior only, not native acceptance of capacities9/10/17 or Intel
compatibility. The native ceiling remains an operational bound subject to Query/Create.

## Final targeted results

Runtime code: `4709463d7c8870120a384e0de0ae55001bd19fa9`.
Build: OpenGL SDK 1.0.2, `test-lab-review-4709463`.
Eboot SHA256: `85594298a09653924a0a325d478a99aac8330e1555803639ca58fe9b1726ad02`.

Focused software validation:

- `make test-video-lifecycle`: SPS/parser/planner and parameter-ID cases under ASan/UBSan,
  including the added real x264 fixtures and retained malformed-input corpus.
- Existing `tests/test_decoder_pipeline.cpp`: pending advertisement/activation/no-op/cancel,
  requested/fallback/recreated affinity, first-AU failure, eight cancellation checkpoints,
  no-packet timeout, waiting-for-SPS/first-output cancellation and retained ownership guards.
  The corrected recovery IDR latch is asserted after resumed output.
- The owner test also passed ASan/UBSan with RFI enabled. As with the previous included-source
  test, `-mllvm -asan-globals=0` avoids retaining unrelated callback sections; heap/stack
  instrumentation remains enabled. The standalone parser uses normal ASan/UBSan.
- 28 targeted Python tests: profile (compiles C++ with TEST_LAB=0 and 1), lifecycle evidence
  and Test Lab analysis. Broad regression coverage comes from successful code CI at `4709463`.
- Final developer SDK image built successfully. An earlier `4beb84b` build was superseded,
  without deployment, after review found an IDR request latch that needed clearing on resume.
  Only the corrected image was deployed and only one new hardware run was performed.

Real PS5 Test Lab run: **`6bc088f1-ae52-40c1-b831-6407101443b3`**, HEVC SDR
1080p60 / 20-Mbps ceiling, 10-second warmup / 30-second measurement.

| Observation | Result |
|---|---|
| Execution / functional / measurement | COMPLETED / PASS / INCONCLUSIVE |
| Synthetic failures | 1; decoded=0 and presented=0 at injection |
| Accepted decoder configurations | generation 1 capacity3; generation 2 capacity7 |
| Create attempts / refresh requests | 2 / 1 |
| Local recovery attempts / successes / failures | 1 / 1 / 0 |
| Reconfiguration time | 39.441 ms, PS5 software timestamp |
| First decoded and first presented after recovery | verified |
| Decode / present affinity application | readback success; masks 0x1000 / 0x800 |
| Normal title exit | verified |

The measurement and overall verdict are intentionally INCONCLUSIVE because the run
contains an injected fault and recreation. It is a functional recovery result, not
performance evidence or independent Intel validation. Software times are not physical
actuator/scanout/input latency.

All 133 installation files passed final-destination SIZE/SHA256 readback. Matching
receipt and raw/export artifact integrity were checked. Saved configuration/pairing
are unchanged, the original shader cache was restored/read back, the remote lease is
absent and host inventory is zero clients/RTSP sessions. The owned Windows worker
exited normally (task Ready, result0). Private raw evidence remains in ignored storage.
See [the redacted evidence and report/export hashes](VIDEODEC2_REVIEW_VALIDATION.json).

No separate cancellation hardware run was needed: the eight deterministic interruption
cases and real first-output replacement barrier cover the changed ownership paths.
The previous startup-cancel run is retained only as normal cancellation evidence;
it did not prove interruption inside native initialization. Previous nine-mode NVENC
matrix and three-pair screening are reused for unchanged paths, with their original
source identity and limitations. No matrix, A/B, HDR, VRR, 4K120 or PyroWave run was repeated.

Remaining boundaries: Intel VAAPI/QSV is delegated and pending before merge; no live
encoder-driven SPS change or software-encoded Moonlight session is claimed. Safe
session reconnect remains unavailable under the existing ownership/API contracts.
Native capacities above the observed range remain Query/Create-dependent assumptions.
