Round-2 ProsperoLight legacy-firmware test builds are ready:
https://github.com/LedyBacer/ProsperoLight/actions/runs/39515915112

Guide and exact checksums:
https://github.com/LedyBacer/ProsperoLight/blob/fix/legacy-fw-agc-round2/docs/LEGACY_FW_AGC_TESTING.md

FW 3.00: **07-compute-only → 08-codecs-only → 09-agc-first**.
FW 4.x (please report the exact version): **00-control → 07 → 08 → 09**.

Install one complete PPSA99002 folder at a time; preserve `/data/prosperolight`.
Wait up to 90 seconds and report whether the actual menu is visible and responsive.
**Do not start a stream.** Save logs immediately after each launch:
`prosperolight-launcher.log`, `fwcompat-startup.jsonl`, and `crash-last.txt` only
if newly generated. Include console output if startup fails before logs are saved.
Report exact FW, exploit/enabler, ShadowMountPlus version, TV mode, build label,
and exit/crash status. Redact credentials, pairing and private network details.
If a build cannot launch at all, stop and report; no repeated attempts needed.

These builds passed compilation and host safety checks; new firmware compatibility
and streaming remain unverified. Please restore your normal release after testing.
