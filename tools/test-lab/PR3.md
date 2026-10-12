# PR3: portable PS5 executor

PR0–PR4 are logical milestones in the same `codex/test-lab-pr0` branch. This
milestone adds a portable Python executor; it does not require a native PS5 SDK on
macOS or Windows to reach the console. Build capability is reported separately:
prebuilt/external artifacts are accepted, Linux needs a configured installed SDK,
and Windows can use an operator-managed WSL builder. No implicit remote build,
credential discovery or silent deployment occurs.

Install from this directory with `python -m pip install -e .`, or use
`python3 tools/test-lab/pltest_cli.py`. Package installation provides `pltest`.
The executor uses only Python standard-library transports. Worker dependencies
are optional and installed with `[worker]`; NVML additionally uses `[gpu]`.

Compile route-three and route-four helpers from `ps5/launcher`. The executor
checks the embedded `PLTEST_HELPER_ROUTE=N` capability before executing an ELF,
then checks the matching receipt after execution. A route-four ELF cannot be
used accidentally as the idle probe. Helper hashes, installed eboot hash and
mounted application build-label validation are distinct evidence.

```sh
pltest doctor --ps5 192.168.1.137 --probe-elf probe.elf --launch-elf launch.elf
pltest run --ps5 192.168.1.137 --probe-elf probe.elf --launch-elf launch.elf \
  --profile tools/test-lab/profiles/pr2-native-smoke.json \
  --eboot-sha256 EXACT_DEVELOPER_EBOOT_SHA256 --output build/test-lab-runs
```

`--deploy path/to/PPSA99002` is explicit and optional. Deployment requires the
executor lease, an authoritative TITLE_IDLE preflight and the expected local
artifact hash. Every uploaded file is read back before atomic rename. Never run
deploy concurrently with a human opening the title. No production configuration
or pairing is uploaded.

A remote FTP directory lease prevents overlapping executor ownership across
machines/processes. A completed run releases its own lease only after the receipt
PID is confirmed absent. Unknown launch/deploy state retains the lease; there is
no TTL-based takeover, kill, reboot or automatic launch retry. Read-only artifact
collection can reconnect within a bounded deadline. A partial archive is retained
and requires a new destination for another collection attempt.

Before launch, the executor copies allowlisted existing diagnostics to a
`prelaunch` directory and hashes them. After completion it collects the matching
immutable receipt and declared fresh diagnostics, indexes artifact SHA-256/size,
and rejects an unchanged required artifact as INCONCLUSIVE. Shared diagnostic
filenames therefore cannot be mistaken for an earlier run. Per-run UUID directories
are never overwritten. The original PS5 receipt is preserved; executor conclusions
are recorded separately in `executor.json`.

CLI exit codes: 0 PASS, 1 FAIL, 2 infrastructure error, 3 INCONCLUSIVE, 4 CANCELLED.
`stop` waits for the matching run/control ACK. `collect` is read-only. If an uncertain
run left a lease, inspect its `executor.json`, collect the original run if possible,
close the title normally, then explicitly release only the recorded ownership:

```sh
pltest release-lease --ps5 PS5 --probe-elf probe.elf --launch-elf launch.elf \
  --run-id RECORDED_ID --owner RECORDED_OWNER_UUID
```

Recovery verifies ownership twice and requires a fresh TITLE_IDLE probe; it does
not launch again. An unconsumed helper mailbox blocks recovery and requires
operator inspection rather than an automatic overwrite.

## Verification

Fake FTP tests cover shared exclusion, retained uncertain lease, owner-checked
idle recovery, route mismatch before execution, no deployment writes when busy,
build hash/label mismatch, bounded allowlisted archives, read-only reconnect,
immutable destinations and exactly one launch attempt after transport failure.

Real macOS executor run `d799b2dc-975f-454d-832b-d80c61cc8812` completed with
functional PASS, normal title exit, fresh artifacts and no stale hashes. Installed
eboot matched `f67d1c7c4ce6c6b28ba9b9b1f5ba9f4ef806c20419b32ec15eef8a488982f896`,
label `test-lab-pr2-78947e2`. Config/pairing hashes stayed unchanged. Evidence lives
under ignored `build/test-lab-pr3/`. This proves the portable executor over the
validated manager/FTP route, not a decoder benchmark or a coordinated host workload.
Windows executor run `41e5e63c-c8f0-4026-a5f0-e3071e5df3e7` independently
completed with the same functional PASS, normal exit and unchanged configuration.
Linux preflight `2b838390-0ca7-4b5f-9e9d-d82ce2710ed3` confirmed TITLE_IDLE and
the same installed hash/label. Linux build capability is reported unavailable
unless its SDK environment is configured, even when executing prebuilt helpers.
The full Linux suite and static analysis passed; Windows selected portable tests
passed with native compiler-dependent checks explicitly skipped.
