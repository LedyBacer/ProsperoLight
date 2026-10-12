# PR2: lifecycle and terminal receipts

Development builds only (`TEST_LAB=1`). The title owns one bounded control worker;
normal launches do not create it. The worker polls a fixed per-run JSON mailbox
at 100 ms intervals, publishes atomic status transitions, and requests the
existing cancellation/normal teardown path. It does not execute filesystem I/O
in decode, presentation, or input callbacks.

The lifecycle distinguishes connecting, waiting_frame, stream_ready, warmup,
measuring, cooldown and stopping. Native readiness uses completed unique flips;
PyroWave readiness uses confirmed output feedback for a particular unique picture,
excluding repeats and unconfirmed submissions. No Ready merely on RTSP connect.
Startup/ready/hard deadlines are monotonic, and phase durations come from the
profile. Measurement completion requires additional unique frames during its
interval. Hard deadline starts when the validated profile enters Test Lab, before
the launcher selects the saved host. Cleanup and artifact exports follow stopping;
final receipt is published after stream teardown, exports and debug-log flush.

Controls at `control/ID.json` use schema `prosperolight-test-control/1`, matching
run_id, a fresh safe command_id and action `start_measure`, `stop` or `cancel`.
Upload `.tmp`, verify bytes, then rename. Commands are bounded to 2 KiB and 64
unique IDs per run. Replays are ignored. Status retains command_id, command_action
and command_result (`accepted`/`rejected`) across later transitions. Wait for a
matching ACK; an early start_measure is rejected. Stop/cancel during handshake
interrupts HTTP and the existing connection-loading worker calls
LiInterruptConnection. The stream owner performs LiStopConnection and cleanup.

Receipts at `receipts/ID.json` are immutable. They include the result, terminal
reason, PID, unique completed frames, monotonic phase boundaries, resolved
requested options and artifact descriptors. Negotiated settings remain explicitly
null; fresh performance diagnostics describe actual format/output. Functional
PASS requires timed completion, ready output, real frame progress, successful
cleanup and fresh exports. Measurement validity remains INCONCLUSIVE and
performance delta NOT_COMPARABLE until stimulus/analysis milestones exist.

## Console smoke

1. Close the title normally; require the PR0 preflight helper to return TITLE_IDLE.
   Hash config/pairing; build/deploy with `TEST_LAB=1
   BUILD_LABEL=test-lab-pr2-78947e2`, verify installed file hashes.
2. Launch with the route-four helper and this profile:

```sh
python3 tools/test-lab/launch_spike.py --host 192.168.1.137 \
  --payload-manager-port 8084 --pc 192.168.1.5:47989 \
  --elf path/to/test-launcher-route4.elf \
  --test-profile tools/test-lab/profiles/pr2-native-smoke.json \
  --output build/test-lab-pr2/evidence --execute
```

3. Copy the reported run_id into the read-only collector. Leave controls alone:

```sh
python3 tools/test-lab/collect_run.py --host 192.168.1.137 \
  --run-id REPORTED_ID --output build/test-lab-pr2/REPORTED_ID
```

Expect a real Desktop stream, 30 seconds in measuring, normal title exit, matching
receipt and archived artifacts. This is functional evidence, not an FPS benchmark.
Check config/pairing bytes again. The collector verifies disappearance of the
receipt's PID through Payload Manager, not arbitrary title termination.

Negative checks: cancel during connecting, stop during handshake, wrong run ID,
replayed command, malformed control, missing fresh artifacts, no presented frame,
host loss, unsupported mode, missing receipt/crash and disconnected PS5. Use fake
transport/observations where a real disruptive test is inappropriate, label each
result accordingly. Do not fake a PS5 PASS from mocks. No forced process kill.

## Current verification boundary

Hardware lifecycle gate passed on 2026-10-12. The latest installed developer
build has eboot SHA-256
`f67d1c7c4ce6c6b28ba9b9b1f5ba9f4ef806c20419b32ec15eef8a488982f896`;
all 131 installed files passed FTP SHA-256 readback. Read-only preflight
`5ffb3e94-7e4d-40be-b21b-b53572faa3d9` returned TITLE_IDLE before the final deployment.

| Case | Evidence and result |
| --- | --- |
| Autonomous timed stream | `6f129b74-1a07-46aa-a6c3-9aeb500b229a`: functional PASS, 30.061920 seconds measuring, 546 unique completed frames, fresh JSON/debug/CSV exports; PID 551 absent after normal teardown. |
| Final controller-barrier smoke | `0c541f98-1d88-49ec-9e1a-662052904a9b`: functional PASS, 30.063194 seconds measuring, 561 unique completed frames, all required exports fresh, PID 560 absent. Latest installed build. |
| Repeated autonomous runs | `c24ea5d0-5ed3-4c0e-b18b-806449e63112` and `9caf3bef-c645-4d9f-a392-4fed87053fdf`: functional PASS on the earlier PR2 build. |
| Connecting cancellation | `015932d6-15b3-4278-9b49-876fa108c24d`: accepted cancel ACK, NVHTTP Connection cancelled, no Ready/frames, CANCELLED, PID 542 absent. Earlier build. |
| Stop before first presentation | `e3b0a91b-3405-42d7-9d7c-d561cda876d5`: accepted stop ACK, zero presented frames, CANCELLED, PID 545 absent. LiStartConnection had already returned; this does not prove hardware interruption inside LiStartConnection. Earlier build. |
| Unsupported profile | `a4c8f4cc-1221-407f-96b8-250e21aa6d31`: native H264 HDR rejected as UNSUPPORTED_COMBINATION, terminal FAIL with measurement UNSUPPORTED, no Ready or stream, PID 554 absent. Old exports marked fresh=false and not collected. Before controller-barrier-only correction. |

All these console runs retained identical config/pairing hashes. Timed-run logs
show normal audio/video cleanup and renderer cleanup before final receipt; PID
absence establishes that no application workers survived title exit. No forced
kill or reboot was performed. Runtime diagnostics confirm HEVC SDR 1080p, requested
60 FPS / 20 Mbps, classic three-core decoder, VSync and paced output. Actual unique
cadence reflects uncontrolled Desktop content; performance comparison remains
NOT_COMPARABLE and measurement validity INCONCLUSIVE.

Host tests exercise actual Context worker/receipt code with fake observations:
startup/ready/hard deadlines, output loss, no measurement progress, control parsing,
replay/terminal refusal, immutable receipts, unchanged old exports, NO_CONTROLLER,
HOST_LOST and UNSUPPORTED_MODE failure reasons. An extracted existing connection
loading worker with fake HTTP/Limelight calls verifies LiInterruptConnection during
pending setup. The same test extracts the new cancellation callback and verifies
that controller writes wait for its initialization barrier. These are host
simulations, not hardware claims. Crash simulation
joins without publishing a final receipt; fake FTP covers missing receipt and
PS5 disconnect, plus bounded collection of fresh partial diagnostics. A real host
network drop, crash and controller unavailability were not induced on the console;
PyroWave lifecycle was compiled but has no hardware run in this milestone.

Raw evidence, installed hashes and provenance are retained locally under ignored
`build/test-lab-pr2/`. Each run has an immutable local receipt and summary; capture
it before another launch. Earlier cancellation evidence includes separately
labelled forensic debug logs. Historical local source snapshots are retained under
`build/test-lab-milestones/`; milestones now remain logical stages in one branch.

Exports retain the existing shared filenames. Collect before the next launch;
PR3 will manage archive-before-launch and executor ownership. Missing final receipts
are incomplete/INCONCLUSIVE and do not authorize an automatic relaunch. A hard
expiry requests bounded normal shutdown; it cannot promise an exit time when
firmware or a join is itself stuck. A collector timeout remains INCONCLUSIVE.

Final verification: full Linux `make test` passed (110 Python cases, one existing
Node-dependent skip, plus native and self-update checks); `make lint` passed
229 attributed code/tool files. PS5 developer and ordinary `TEST_LAB=0` app builds
passed. Fifteen focused Test Lab cases passed on macOS and Linux.
