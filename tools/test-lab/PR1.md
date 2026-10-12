# PR1: session profiles

Build the development client with `make app TEST_LAB=1 BUILD_LABEL=test-lab-pr1-78947e2`.
This enables detailed local diagnostics and disables LAN telemetry. Release builds
keep `TEST_LAB=0`; they ignore `--test-run` and retain ordinary forwarded launching.

The development client accepts `--test-run ID --exit-after-stream` and reads only
`/data/prosperolight/test-lab/inbox/ID.json`. Profiles use schema
`prosperolight-test/1`; see `profiles/pr1-native-smoke.json`. Explicit client
settings override copies of saved stream and host preferences. Omitted settings
inherit saved values. Pairing requires an existing saved host and valid identity;
missing or corrupt identities are rejected without generating credentials.
Host extensions remain disabled unless explicitly enabled or inherited enabled.
A different running host application is refused rather than terminated.

Parser limits: 16 KiB, depth eight, 512 values, ASCII strings up to 256 bytes,
unique keys and strict allowlists. Escapes, arrays, floating numbers, negative
numbers and unknown fields are rejected. Use an address for non-ASCII host names.
`source_sha` is provenance metadata, not an attestation of modified source.
A supplied `build_label` must match the mounted client's build-label.txt.
Native H.264 HDR and native 4:4:4 are rejected; PyroWave cannot accept native-only
pipeline/core overrides. No unsupported setting is silently substituted.

## Console validation

1. Close the title normally. Run the PR0 preflight helper and require `TITLE_IDLE`.
2. Record config and pairing hashes, deploy the development app, verify deployed
   files by FTP readback. Never deploy over an active title.
3. Compile the helper with `PR0_ROUTE=4`, then run:

```sh
python3 tools/test-lab/launch_spike.py --host 192.168.1.137 \
  --payload-manager-port 8084 --pc 192.168.1.5:47989 \
  --elf path/to/test-launcher-route4.elf \
  --test-profile tools/test-lab/profiles/pr1-native-smoke.json \
  --output build/test-lab-pr1/evidence --execute
```

The sender assigns a fresh run ID and uploads a verified profile atomically.
Archive `/data/prosperolight/test-lab/status/ID.json` and fresh local performance
artifacts. `launching` records resolved requested settings, not negotiated settings
or a presented frame. Confirm HEVC/1080p/60/20 Mbps and pacing in runtime diagnostics,
observe the real stream, then stop normally. Check title disappearance and unchanged
config/pairing hashes. Repeat an ordinary launch without test arguments.

PR1 has no automated stop, ready gate, hard deadline or final lifecycle receipt;
timing fields are parsed for PR2. Sender exit code 3 means INCONCLUSIVE even if
launch was accepted. Never classify transport acceptance as a passed stream.

## Validation status

Host parser/resolver tests cover overrides, inheritance, malformed profiles,
negative combinations and test argument gating in development and release builds.
Video output tests check session policy independence from persistent preferences.
PS5 development and ordinary release compilation succeeded. Full host checks passed
(103 integration tests, one skipped), and lint passed 222 attributed files. On the
PS5, run `b105a721-6fc7-452e-8010-04a61c944e64` correctly rejected a mismatched
build label and exited. Run `a7a87158-2953-45c3-8284-226ba8c7aabe` reached a real
stream; runtime diagnostics confirmed HEVC Main 4:2:0 SDR, 1920x1080, requested
60 FPS, 20000 kbps, classic pipeline/three cores, VSync and pacing mode one.
This is profile propagation evidence; uncontrolled Desktop content is not a
performance benchmark. Config/pairing hashes remained unchanged while streaming.
The operator observed Desktop and stopped normally: diagnostics reported user_stop=1,
new performance artifacts were written, the process disappeared, and config/pairing
hashes remained unchanged. This completes the PR1 console propagation gate.
Status directories use 0755 so the FTP service can read non-secret receipts
written by the title, which runs under a different user.
PR0 hardware launch, argv and operator-assisted normal stream exit passed separately.
