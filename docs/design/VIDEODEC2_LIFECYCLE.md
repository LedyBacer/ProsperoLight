# Stream-aware VideoDec2 design

The native pull decode worker owns parameter acquisition, planning and decoder
replacement. Session negotiation and the loading display do not require a decoder.
There is no additional frame queue. The worker holds the existing first access
unit while creating its decoder; UDP receive continues through the bounded
moonlight-common-c queue. Cancellation is checked between allocation stages.

Parsed SPS facts are separate from `Plan` and native API configuration. H.264
uses Table A-1 MaxDpbMbs divided by coded macroblocks, capped at 16, without +1.
HEVC uses sublayer buffering and its own level-dependent MaxDpbSize. Missing H.264
VUI restrictions require the standard bound, rather than guessing encoder refs.
Native support is progressive 4:2:0 H.264 baseline/main/high 8-bit and HEVC
Main/Main10 matching the negotiated pixel format. Unsupported visible sizes fail
rather than silently presenting with the negotiated crop incorrectly.

## API evidence and assumptions

Public ABI declarations in this repository expose `max_dpb_frames`, not its
normative accounting contract. Context7's available homebrew documentation did
not resolve that contract. Standard DPB facts must not be described as native
hardware limits. The initial native plan reserves one separate output/current
picture beyond stream buffering. This is an explicit conservative API accounting
assumption requiring hardware confirmation; it is NOT part of the H.264 formula.
The operational ceiling is 17 (standard 16 plus that margin), subject to native
Query/Create acceptance. No claim that every capacity is supported is made.

A capacity error permits one distinct configuration using the stream's standard
DPB ceiling plus the same margin, only when larger than the active allocation.
It never retries an identical capacity. Total local recovery budget is two;
consecutive ordinary corruption is bounded to eight IDR recoveries, reset by a
successful output. Unsupported configuration/allocation failure fails closed.
No full session reconnect is attempted: this avoids restarting/terminating host
applications under uncertain ownership.

## Replacement barrier

Decode submissions are serialized on the decode worker. Replacement stops and
joins the presentation worker, retires native AGC GPU reads, invalidates mailbox
and decoder-held slots, deletes the old decoder and then releases its allocations.
An unsuccessful retirement/delete retains allocations and faults the session.
New frame references are only published after a new generation becomes ready.
No CPU copy of decoded pixels or persistent replacement presentation surface is
introduced. PyroWave bypasses this entire owner.

## References

- [PR17](https://github.com/blackbearreloaded/ProsperoLight/pull/17)
- [PR51](https://github.com/blackbearreloaded/ProsperoLight/pull/51)
- [H.264 level table](https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/h264_levels.c)
- [H.264 parameter parsing](https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/h264_ps.c)

The six-slot workaround and reported 13-slot VAAPI result are historical evidence,
not independent reproduction. Intel VAAPI/QSV 1440p60 remains a pre-merge test.
