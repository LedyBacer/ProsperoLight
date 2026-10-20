# FW 3.00 — query isolation and AGC snapshots (round 3)

This expands [round 2](LEGACY_FW_AGC_TESTING.md). It is experimental launcher
validation only. No firmware compatibility or streaming success is claimed.
Download links/checksums and exact developer evidence will be recorded in
[the round-3 artifact inventory](LEGACY_FW_AGC_ROUND3_ARTIFACTS.md).

## Builds

All retain the original module preload, cache/display settings, codec parameters
and one-argument AGC init ABI. Normal production defaults remain unchanged.

| ID | Queries / instrumentation |
|---|---|
| 00-control | Matched full warm-up, ordinary AGC order; no early getter |
| 09-agc-first | Matched full warm-up, one early init; defaults before/after warm-up |
| 10-query-snapshots | Same as 09, also defaults after compute, H.264, HEVC, Main10 |
| 11-h264-only | H.264 SDR only |
| 12-hevc-only | HEVC SDR only |
| 13-main10-only | HEVC Main10 HDR only |
| 14-compute-h264 | Compute → H.264 SDR |
| 15-compute-hevc | Compute → HEVC SDR |
| 16-compute-main10 | Compute → HEVC Main10 HDR |
| 17-codecs-before-compute | H.264 → HEVC → Main10 → compute; ordinary later AGC init |

Only 09/10 initialize AGC early, and both reuse the SDK's actual existing init
state. No second init, ABI/version experiment, synthetic defaults or recovery.
All new variants **10–17 refuse streaming**, including forwarded launches.
Do not stream with the controls either during this test procedure.

10 logs `agc.after_query.compute`, `.h264_sdr`, `.hevc_sdr`, and
`.hevc_main10_hdr`. Values are the actual returned pointer, not its contents.
It does not dereference these pointers. A NULL marks sticky graphics failure;
the remaining selected queries/snapshots still execute to expose later changes,
but rendering is blocked afterwards even if a later pointer becomes non-NULL.
If early init/defaults fail, warm-up is skipped and failure reported safely.
An early crash may require console debug output before storage logs open.

**Observer boundary:** 10 observes queries *after early AGC initialization*.
A first NULL after a particular query localizes a transition in that ordering;
it does not prove that query independently causes the original late-init failure.
The extra getter/API-binding calls may affect state/timing. Compare 09 versus 10
from this same run; independent 11–16 have no early AGC/getter instrumentation.

## Minimize tester launches

Keep the prior 07-compute-only / 08-codecs-only results from round 2. If missing,
run those two using the prior [verified downloads](LEGACY_FW_AGC_ARTIFACTS.md)
first. Record that they belong to the earlier source/SDK patch identity.

1. Run **09 → 10** from this new run. Compare visible menus, init count and
   defaults pointers. Run new **00** if the original failure needs reconfirmation
   or launch conditions changed. Never treat “no crash” as a working menu.
2. If **08 failed**, run **11 → 12 → 13** to isolate a codec query.
3. If **07 and 08 passed but control failed**, run **14 → 15 → 16** to find a
   sufficient compute/codec pair. These can also distinguish interactions when
   all single-codec variants pass. Do not infer anything from an untested pair.
4. Run **17** when combined warm-up fails and order is still in question. A
   working reversed order implicates ordering, not a complete compatibility fix.
5. If **07 alone fails**, compute is already independently implicated under
   those conditions; prioritize 09/10 rather than requiring every pair.

If a build cannot launch at all, stop and report; no forced retries. Report any
nonzero query result separately even if a menu opens. A valid pointer alone is
not proof of a usable register table or a functioning decoder.

## Installation and logs

Follow the full [installation/recovery guide](LEGACY_FW_AGC_TESTING.md): close
normally, back up the installed app outside `/data/homebrew`, preserve all of
`/data/prosperolight`, install **one complete PPSA99002 folder at a time** (no
old-file overlay or duplicate title), check ZIP SHA256 and `build-label.txt`.
Wait **90 seconds**, then record actual visible responsive menu versus failure.

**Immediately after each launch**, save `prosperolight-launcher.log`,
`fwcompat-startup.jsonl`, and only a **newly generated** `crash-last.txt`.
Include console output for pre-storage failures (fallback logs `/download0`).
Match variant, source SHA and process timestamp; old crashes are not new failures.
Report exact firmware, exploit/enabler and version, ShadowMountPlus version,
TV resolution/refresh/HDR/VRR and exit/crash status. Redact credentials, pairing,
private host names and addresses. Restore the normal app folder when done.

| Variant | Exact FW | Visible responsive menu | Query errors / first NULL stage | Exit/crash | Notes |
|---|---|---|---|---|---|
| 00 | | | | | |
| 09 | | | | | |
| 10 | | | | | |
| 11 | | | | | |
| 12 | | | | | |
| 13 | | | | | |
| 14 | | | | | |
| 15 | | | | | |
| 16 | | | | | |
| 17 | | | | | |

## Developer verification

Focused host tests execute actual warm-up code for every new subset, parameters,
error propagation, compute-last order and snapshot positions. Invalid production
flags and snapshots without early AGC are rejected at compile time. Actual SDK
hooks/target-state/reuse are exercised under ASan/UBSan, including snapshot stage
identity, valid→NULL transitions, no extra init and missing early-state failures.
Native builds and byte-level artifact validation remain separate from physical
PS5 results. No deployment, release, main merge or full performance suite.
