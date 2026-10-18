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

An IDR with the selected SPS supplied activates a supported configuration. Semantic
repeats do not recreate; a different parameter identity with identical requirements
updates identity at the safe boundary without allocation. Ambiguous slice prefixes
can only fall back to a single unambiguous SPS in an IDR. Multiple unresolved sets
wait/fail instead of guessing. Unsupported non-IDR advertisements retain the instance
and request a complete IDR; malformed syntax and unsupported active IDRs fail closed.
No raw cached SPS/PPS is replayed into a new instance. Recovery requests fresh sets;
cancellation clears pending maps only after joining the decode owner.

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

Results and exact hardware identity/run ID are added after focused tests and the
single final developer SDK build. The previous startup-cancel run occurred after
first output and is not evidence of cancellation inside native initialization.
