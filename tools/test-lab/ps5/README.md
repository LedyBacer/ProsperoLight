# PR0: registered-title launch spike

Base: `upstream/main` at `78947e272566bbac31a5baab8965bf73cea87740`.
Branch: `codex/test-lab-pr0`. PR0 hardware gate passed on 2026-10-12; see evidence below.
This directory is independent of the production application and its Make defaults.
The helper does not change persistent configuration, pairing files or runtime preferences.
A current developer app was deployed after explicit user authorization, with hashes verified. No resident daemon, forced stop, reboot or automatic launch retry.

## Build and host tests

Use an **installed** PS5 Payload SDK (source checkout alone is insufficient):

```sh
make -C tools/test-lab/ps5/launcher PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
python3 -m unittest discover -s tests -p 'test_test_lab_launch_spike.py'
python3 -m unittest discover -s tests -p 'test_forwarded_launch.py'
make test
make lint
```

The payload uses the process enumeration / `sceKernelGetAppInfo` ABI from
[SDK samples/ps](https://github.com/ps5-payload-dev/sdk/blob/master/samples/ps/main.c).
Any uninspectable process makes the probe inconclusive and refuses launch.
This intentionally conservative policy may need adjustment based on hardware evidence.
The SystemService parameter ABI and foreground-user selection follow
[PS5 Forwarder Format launcher](https://github.com/heydemoura/ps5-forwarder-format/blob/main/launcher/launcher.c).
The selected route makes one direct launch attempt with size zero.
Build `PR0_ROUTE=1` for explicit size, `=2` for an existing PSFWD service, or
`=3` for preflight only; route-specific ELF names prevent stale build selection. It does not try a
second parameter layout after an ambiguous result. `0x80940010` is reported as
queued **unverified**, never success. Direct launch via Payload Manager is hardware validated. PSFWD was probed locally
and reported unavailable; no resident service was installed.

## Real-console procedure

1. Console must have registered `PPSA99002`, a saved paired host and `Desktop` app.
   Close ProsperoLight normally. Do not replace/deploy the installed application.
2. Check FTP 2121 and a remotely accessible ELF loader 9021. The SDK sender uses
   raw ELF over TCP, without HTTP or a length header. A loader process existing
   does not prove that its remote port is listening.
3. Preview with the saved host name or address, then execute explicitly:

   ```sh
   python3 tools/test-lab/launch_spike.py --host 192.168.1.137 --pc Gaming-PC \
     --elf tools/test-lab/ps5/launcher/test-launcher.elf
   # Add --execute to stage the bounded request and send the ELF.
   ```

   Only fixed paths under `/data/prosperolight/test-lab/pr0` are staged. FTP
   readback precedes atomic rename. Existing unconsumed request refuses overwrite.
   This spike requires a single operator; it is not a multi-coordinator lease.
4. Archive the request-ID JSON from that directory. Helper consumes request once,
   checks for existing title, resolves the foreground user, then requests
   `PPSA99002 --pc <saved host> --app Desktop --exit-after-stream --launch-proof <ID>`.
   Build the app with `LAUNCH_PROOF=1 PERFORMANCE_DETAIL=1 LAN_TELEMETRY=0`
   and mark it with a developer `BUILD_LABEL`. The default `LAUNCH_PROOF=0`
   compiles without the startup hook. `include/launch_proof.hpp` writes a small
   `<ID>-app.json` only after storage initialization, proving argv in the new process.
   Failure before a valid request / writable receipt returns nonzero without a
   receipt. No final receipt or stale `preflight` is INCONCLUSIVE, not success.
   With `--payload-manager-port 8084`, the helper ELF is staged/readback-verified
   under the fixed manager-owned `/data/pldmgr/payloads/prosperolight-test-launcher-pr0/`.
   Requests and receipts stay in the Test Lab root. With `--output <directory>`,
   archive launch/application receipts and the exact helper SHA-256 locally.
5. Verify **in the new application process**, using process arguments or existing
   startup logs, the exact argv. Starting an app is insufficient. Verify that
   the selected paired host and Desktop stream actually start, stop the stream
   normally and observe the application exit. Archive logs and config hashes
   before/after. The sender validates and archives the application receipt, but returns exit 3
   even with verified argv: stream start and normal exit need separate evidence.
6. Repeat while the title is running: expect ALREADY_RUNNING, unchanged PID,
   no second stream. Exercise missing user, invalid mailbox, stale request and
   unavailable host; do not force-kill anything. A normal forwarder failure may
   remain on the ordinary error screen in PR0; test-mode failure exit is PR2.
7. Compare direct launch with the PSFWD route only from a PS5-local helper.
   PSFWD port `127.0.0.1:10199` is console loopback and cannot be called remotely.
   Keep PR1–PR7 gated until argv, normal exit and already-running refusal pass.

## Hardware evidence (2026-10-12)

PR0 launch feasibility **PASS**, using direct SystemService (size zero) from a
one-shot ELF, executed through Payload Manager v0.5.2 (e4e72dc).

- Upstream base `78947e272566bbac31a5baab8965bf73cea87740`.
- FTP 2121 reachable. Remote loader port 9021 refuses connections, while manager
  `:8084` bridges ELF files to the console-local loader. HTTP 80 refuses connections.
- Existing mounted app originally lacked forwarded start. Direct zero-size and
  explicit-size calls launched it but only showed its menu, as the operator
  confirmed. This did not prove stream launch. It prompted the authorized update.
- PSFWD loopback probe request `c4ceb9a8-ee34-405e-b04a-9ebadae04edb`:
  `PSFWD_UNAVAILABLE`, errno 61. No daemon installed.
- Read-only preflight `072c7bba-0470-4763-b98d-c27b67f642ac`: `TITLE_IDLE`.
- Developer app built on the isolated Linux builder with `LAUNCH_PROOF=1`,
  `PERFORMANCE_DETAIL=1`, `LAN_TELEMETRY=0`. All 131 installed files passed FTP
  SHA-256 readback. Eboot SHA-256:
  `1278bdaa6703f3df8b313f244bbd8bb01ae8d4811d368d23eeb77b6bea1f1b7b`.
  The mounted `/mnt/sandbox/PPSA99002_000/app0/eboot.bin` matched that hash.
- Request `b7f50b3f-20a4-476a-b5ac-755ba6abfeed` returned `LAUNCH_ACCEPTED`
  (49176). Application PID 518 independently published the same request ID,
  PC `BacerPC`, app `Desktop`, exit-after-stream true and verified argv.
- Operator confirmed Desktop stream started and stopped it through the app's
  normal controls. Manager process inventory then confirmed PID 518 disappeared
  and no `eboot.bin` remained. This is operator-assisted stream/stop evidence;
  there was no automated timed stop, decoder-performance verdict or physical
  HDMI measurement in PR0.
- Existing-running-title requests `5aafdd1e-af33-442c-98e1-b0d1dd0ccda6` and
  `a218b37e-0725-43d5-bfb8-6685f7da2441` returned `ALREADY_RUNNING` before
  calling launch. Initial kernel-only checking missed mounted homebrew; fixed by
  authoritative shell active-app identity, with kernel enumeration as a second
  conservative guard. Host mocks cover failure/no-user/queued launch/one-shot
  consumption; unavailable user was not exercised on hardware.
- Config and pairing file hashes remained identical before deployment, after
  deployment and after the successful stream. Production defaults remain intact;
  the installed binary is now the explicitly authorized developer build.
- Six new host tests passed on macOS and Linux. Full Linux `make test` passed;
  101 Python integration cases, one existing skip. Baseline full lint passed;
  the final lint run including the startup proof hook is recorded separately.

Raw local evidence is in ignored `build/test-lab-pr0/` (receipts, installed-file
hashes, provenance, previous eboot/metadata, preservation hashes). Older standard
logs remained stale and were not used to prove the new stream or its metrics.
The selected direct route is validated; PSFWD and raw remote 9021 remain unavailable
on this console. No normal users' launcher behavior or configuration format changed.
