Additional ProsperoLight FW 3.00 diagnostics are ready: **12 verified builds**.

Downloads, SHA256 and matching developer evidence:
https://github.com/LedyBacer/ProsperoLight/blob/fix/legacy-fw-agc-round2/docs/LEGACY_FW_AGC_ROUND3_ARTIFACTS.md

Test order and installation/recovery instructions:
https://github.com/LedyBacer/ProsperoLight/blob/fix/legacy-fw-agc-round2/docs/LEGACY_FW_AGC_ROUND3.md

Start with **07 → 08 → 09 → 10**. Use fresh 00 to reconfirm the baseline failure
if conditions changed. Then follow the conditional order in the guide; you do
not need to run all twelve builds automatically.

10 records AGC defaults after each decoder query. 11–13 isolate individual
codecs, 14–16 test compute/codec pairs, and 17 reverses the query order.

Install one complete PPSA99002 folder at a time, preserve `/data/prosperolight`,
check the variant/source label and wait up to 90 seconds. Report the actual
visible, responsive menu. **Do not start a stream.**

Save `prosperolight-launcher.log`, `fwcompat-startup.jsonl` and only a newly
created `crash-last.txt` immediately after each launch. Include console output
for early failures. Report exact firmware, exploit/enabler version,
ShadowMountPlus version, TV mode and exit/crash status. Redact secrets,
pairing information and private network details. Restore your normal release
when finished. If a build cannot launch at all, stop and report rather than retry.

These binaries passed software/build checks; FW 3.00 compatibility and streaming
remain unverified until your logs and visible-menu reports are reviewed.
