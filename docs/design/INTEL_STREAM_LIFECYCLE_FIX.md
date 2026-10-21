# Intel lifecycle and Test Lab correctness follow-up

Based on independent PR65 evidence at `b186d4c` in
`pl-pr65-intel-20261020.zip`. No new architecture, performance tuning, codec,
quality/default CPU/depth changes, or native experimental gate enablement.
The streaming PC and PS5 are unavailable; this change has no hardware PASS claim.

## Verified client failure mechanism

Five QSV H.264 runs terminate with Parameters/-32 while an established native
instance has decoded hundreds of frames without a native error. In c2-r0,
creation completes at 6.286s, DR requests IDR at 6.302s, and the parameter deadline
fires at 16.312s after 561 decoded/560 presented frames.

Code and deterministic owner tests reproduce the client defect: an unknown or
incomplete AU arms `transition_wait_since` and causes DR_NEED_IDR. A later AU
can select the exact SPS/PPS generations already submitted to the existing
native owner and need no reset/recreation. That valid continuation previously
returned success without clearing the wait unless it was an IDR. The stale
wait eventually failed the session despite ongoing native output.

Clear the wait/request state only in that proven no-mandatory-operation branch.
A selected changed SPS/PPS, unknown/conflicting mapping, recreated decoder,
required reset or initial bootstrap still cannot use an ordinary P frame as a
safe boundary. Fresh complete matching headers and actual IDR remain required
for a new/reset native owner. Future advertisements do not activate by themselves.
Mandatory deadlines, recovery budgets and cancellation ownership remain intact.

Optional policy refresh is now best-effort on the retained valid owner: matching
pictures can continue while waiting for IDR; its timeout rejects only the optional
action. A genuinely changed selected configuration upgrades it to mandatory
waiting with a fresh bound. The existing experimental depth-trial gate stays off.

Limelight's DR_NEED_IDR completion calls requestDecoderRefresh (queue flush and
receive-state recovery), then LiRequestIdrFrame signals the control worker. The
archive's `IDR frame request sent` confirms the send path returned success;
it does not prove encoder receipt/acknowledgment or production of an IDR.
The archive lacks NAL payloads and host-side IDR handling logs, so the exact
initial rejected AU/host response remains unproven. IDR_calls excludes the first
Decode call, which has its own timing bucket; zero does not mean no initial IDR.

## Effective launch selection and immediate refusal

Model::ResolveStreamOptions copies saved options and resolves an active lab
profile without persistence. RequestStream validates capabilities against those
effective options using one pure shared validator. FillSelection uses the same
resolver rather than a separate overlay path. Normal launches retain saved-option
capability checks and translated notices.

A synchronous rejection stops the existing lab context and publishes failed
status immediately with a stable specific reason, such as HEVC_UNSUPPORTED,
PYROWAVE_PROFILE_UNSUPPORTED or HDR_UNSUPPORTED. The existing launcher observes
the stopped context and exits; final receipt preserves that reason rather than
waiting for STARTUP_TIMEOUT. No saved codec/chroma/HDR/pairing settings are edited.

## Delivery evidence and scope

A small additive pinned Limelight patch exposes relaxed diagnostic counters.
No decoder callback ABI/transport algorithm changes, per-frame allocation or
logging are added. Independent 64-bit atomic counts are read once for the final
native summary. Live snapshots are explicitly nontransactional; conservation is
checked only after transport/queue shutdown. The current attempt's Limelight-clock
start floor rejects stale previous-session counters after an early start failure. The build input and provenance
include the instrumentation patch hash. Interrupted shutdown offers release their
unowned AU rather than leaking it; streaming queue semantics are unchanged.

| Stage | Collection point and limits |
| --- | --- |
| Host sender | Existing independently verified host samples and bounded interval; unknown otherwise. Not inferred from callbacks. |
| Video packets/bytes | Positive reads on the video RTP UDP socket, before decrypt/FEC; includes parity, malformed datagrams and protocol headers. Not compressed bitrate. |
| Completed frame events | Depacketizer end-of-frame after payload checks, before recovery filtering. Does not count every transmitted frame or failed FEC assembly. |
| Recovery filtering / empty AU | Separate post-assembly recovery filtering and empty eligible chain counts. |
| Eligible assembled AU | Nonempty chain before allocation and queue/direct offer. |
| Offered / queue accepted | Allocated complete DU at offer, successful queue insertion separately. Direct callbacks are identified explicitly. |
| Local discards | Actual freed queue entries plus rejected current AU, separately attributed to overflow, refresh, interrupted offer and shutdown; allocation failure separate. |
| Dequeued AU | Actual Wait/Poll removal or direct callback, before native validation/submission. Peek does not count. |
| Native submission / output | Existing access_units / decoded; a dequeued AU can be rejected before submission. |
| Unique presentation | Existing confirmed unique completion evidence, not physical scanout. |
| Frame-ID gap | Discontinuity only; no automatic network/decoder attribution. |

Whole-session conservation at quiescence checks:

- completed = eligible assembled + recovery filtered + empty;
- eligible assembled = offered + allocation discard;
- offered = dequeued + overflow discard + refresh discard + interrupted discard + shutdown discard.

Session totals are not aligned measurement-phase rates. Reports keep scopes
separate, do not substitute them for missing arrival timestamps, and do not
compare them numerically against a different host interval. Positive verified
queue loss rules out a blanket complete-delivery claim. Host production below
target together with native backlog/session local loss is concurrent-or-ambiguous,
not exclusively host-limited. Unattributed frame-ID gaps stay inconclusive.

Frame CSV schema/columns are unchanged; an additional comment identifies its
scope as dequeued AUs only. Legacy `metrics.received_fps` remains a compatibility
alias of dequeued trace rate; `dequeued_au_fps` and trace_scope make this explicit.
`receive_us` describes only retained rows, excluding upstream discards. Unknown
host/upstream data remains null/unavailable. Test Lab acceptance thresholds are
unchanged. Existing independent Intel results are not relabeled VALID; receipts
in that archive have measurement_validity=INCONCLUSIVE.

## Validation and limits

Focused owner/lifecycle, effective profile/refusal, conservation and bottleneck
regression tests cover the changes. One final SDK build and the PR's CI provide
compilation and broader validation; exact results are recorded in the PR.
New PS5 launches: 0; no streaming-host connections, benchmark matrix or external tester contact.
Native optional depth reconfiguration remains gated and unverified on hardware.

Future Intel work needs host-side request/IDR logs and, for the initial trigger,
a retained/reproducible compressed AU. QSV 4K already shows both client queue
loss and native service costs above the 60FPS budget; this fix does not claim to
solve that throughput limit. Independent matched-window host/assembly/dequeue
measurements are required before attributing all low FPS to a single stage.
