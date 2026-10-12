# PR5 — coordinator API, durable scheduler and CLI

PR5 adds the local coordinator described in the Test Lab plan. Install the
package with `python -m pip install -e tools/test-lab`; coordinator
dependencies install by default.
The coordinator stores metadata in SQLite/WAL and each run's profile, report and
future artifacts in an immutable `runs/<uuid>/` directory. Do not put a shared
database on a network filesystem.

```sh
pltest coordinator --host 127.0.0.1 --port 8765 --data-dir build/test-lab-coordinator
pltest doctor
pltest run --suite fake-smoke --profile tools/test-lab/profiles/fake-smoke.json
pltest status RUN_UUID
pltest report RUN_UUID --json
pltest artifacts RUN_UUID
pltest stop RUN_UUID
pltest matrix --profile tools/test-lab/profiles/fake-smoke.json --repetitions 3
```

API v1 is rooted at `/api/v1`: health, capabilities, suites, create/start/stop a
run, read run state/events/report/artifact index, fetch an individual artifact,
and request a comparison. Request and report schema versions are explicit. The
comparison endpoint returns `INCONCLUSIVE/ANALYZER_DEFERRED_TO_PR6` until PR6
implements meaningful baseline analysis. `GET /docs` is generated from the
Pydantic schemas. When bound beyond loopback, set `PLTEST_API_TOKEN`; all API
routes except health require its bearer token, including the worker WebSocket. Keep the
token in the coordinator environment, never in a profile or report. For LAN
access, put the local server behind a trusted TLS tunnel or reverse proxy.

The scheduler enforces monotonic transitions, idempotent start/stop, a SQLite
PS5 lease shared across coordinator processes, and a 10-second lease TTL renewed
every two seconds. On expiry or coordinator restart, in-flight work becomes
`INCONCLUSIVE` with `COORDINATOR_RESTART`; unsafe deploy/launch steps are never
replayed. The scheduler stops work when it loses lease ownership and does not
run cleanup against a resource another coordinator may now own. A stop waits at
most eight seconds for its adapter, then cancels and waits a bounded cleanup
period. Cleanup failure becomes `INCONCLUSIVE`. Terminal report and event data
remain available after restart. Health clients poll at two seconds or slower.
Adapter `cleanup()` implementations must be idempotent; a run that loses its
lease skips cleanup because it no longer owns those resources.

## Adapter and hardware boundary

Fake adapters remain the default and are used only by `fake-smoke`; reports say
`execution_mode=fake`, and their PASS is simulated. Real mode uses the existing
PR3 PS5 lease/loader/FTP protocols and PR4 authenticated Windows worker. It
prepares the host stimulus, launches the PS5 title once, waits for the executor's
sticky stream-ready status, starts the host and authorizes PS5 measurement,
then collects the matching terminal receipt and artifacts. Real reports say
`execution_mode=real`; PASS requires a valid host measurement, a PS5 PASS receipt,
fresh artifacts and confirmed title exit. An uncertain launch retains the
remote PS5 lease; real work is never automatically replayed.

Enable real mode explicitly on the coordinator host. The installed title must
already match the build label and exact eboot hash in the run profile; the
coordinator does not deploy or modify the installed app. The probe and launch
ELFs must be built with the PR3 route markers. Keep the worker bearer token in a
local file readable by the coordinator process.

```sh
export PLTEST_PS5_HOST=192.168.1.137
export PLTEST_PS5_PROBE_ELF=/path/to/probe.elf
export PLTEST_PS5_LAUNCH_ELF=/path/to/launch.elf
export PLTEST_PS5_EBOOT_SHA256=EXACT_INSTALLED_EBOOT_SHA256
export PLTEST_HOST_WORKER_URL=http://192.168.1.5:8096
export PLTEST_HOST_WORKER_TOKEN_FILE=/private/path/worker-token
pltest coordinator --host 127.0.0.1 --mode real --data-dir build/test-lab-coordinator
pltest doctor
pltest run --suite decoder-smoke --profile tools/test-lab/profiles/pr2-native-smoke.json
```

Run the host worker in the logged-in Windows desktop session with
`pltest worker --bind 0.0.0.0 --token-file PATH --output PATH --mode managed`;
protect the token file and use a trusted tunnel/firewall policy for LAN traffic.
The supplied PS5 smoke profile expects its declared build label and host/display
configuration to match the installed test build. Set `host_stimulus.monitor` to
the unique display ID or `virtual` when the virtual display is uniquely
identified. `GET /api/v1/capabilities` and `/suites` report the configured mode.
The worker requires an interactive unlocked Windows session. No production
configuration, pairing or installed title files are changed by a real run.

The versioned `/api/v1/ws` route requires the same bearer token when configured
and ACKs schema-versioned event envelopes. It remains an event transport stub;
the real host adapter talks directly to the already implemented authenticated
worker WebSocket. Suite analysis/comparison thresholds and browser dashboard
remain PR6/PR7.

FastAPI lifespan and Pydantic response-model patterns were checked against the
[FastAPI lifecycle](https://fastapi.tiangolo.com/advanced/events/) and
[response model documentation](https://fastapi.tiangolo.com/tutorial/response-model/).

## Verification

`tests/test_test_lab_coordinator.py` covers fake end-to-end state progression,
API/CLI report parity, stage-specific failure, idempotent cancellation, lock
exclusion using two independent SQLite connections, restart recovery, terminal
persistence, path traversal rejection, API token/schema checks and event polling.
It also tests real-mode stage ordering, execution-mode reporting, and the sticky
PS5 stream-ready gate without accessing hardware. `tests/test_test_lab_lifecycle.py`
verifies that the PS5 executor waits for the coordinator's start-measure command
and preserves readiness in status after the transient ready state.
The live Uvicorn CLI smoke run `dcb75452-b0ac-45cd-aa98-4e7af29f9753` returned
PASS with `execution_mode=fake`; its API report matched the CLI JSON, and both
profile/report artifacts were indexed. Restarting the service with the same
SQLite directory returned the identical report. No PS5 or Windows worker was
used for PR5 acceptance.

The wider Test Lab suite also exercises PR0–PR4: 35 host tests passed on macOS.
On the configured Linux builder, `make test` passed 130 tests (3 environment
skips) and `LINT_SKIP_TIDY=1 make lint` passed source, format, metadata and asset
checks; `make tidy` separately passed Clang static analysis. PR5 adds Python developer tooling and a developer-only PS5 measurement gate.
The production build continues to default to TEST_LAB=0.

### Real-adapter hardening follow-up (2026-10-12)

The real coordinator now waits for in-flight blocking PS5 operations to settle
before cancellation cleanup can touch the same device. It records launch as
potentially active before sending the loader request, so a lost response cannot
release the remote lease or make a later replay appear safe. New tests cover
that cancellation boundary, real-mode API wiring, and the authenticated host
worker WebSocket command/report contract using a local protocol peer. The peer
is a transport fixture, not Windows desktop evidence.

The full Linux-builder `make test` run passed 135 tests with 3 environment
skips; the 14 coordinator tests, source/format/metadata/asset lint, and Clang
Tidy also passed. The Windows worker at the configured host was not listening
during this follow-up, so no coordinated Windows/PS5 run is claimed. The latest
PS5 app build attempt remains blocked by the builder's pinned Vulkan dependency
chain resolving `LLVMSPIRVLib` 19.1.0.0 where the dependency requires `<18.2`;
the dependency was not bypassed or changed.

### Review fixes (2026-10-12)

- CREATED → PREFLIGHT and SQLite lease acquisition now form one atomic claim.
  Starting the same UUID in two coordinators cannot fail the live run or release
  its lease. Stop requests are durable and observed by the owning coordinator.
- Pending report files have unique names. Publication checks the immutable SQLite
  report, and startup restores terminal report files after interrupted publication.
  Internal pending/temp files and symlink paths cannot be downloaded as artifacts.
- Real launch removes coordinator-only `host_stimulus` and stage timeout fields
  before staging the strict PS5 profile. It explicitly sets `coordinated=true`.
  The native parser supports that boolean, with false as the default: standalone
  PR3 runs retain autonomous timing, while coordinator runs require start_measure.
- Cancellation cannot detach an in-flight launch thread, even after repeated
  cancellation. Lease acquisition is recorded before returning to async code.
  Unsafe cleanup retains the remote lease and reports an error rather than a
  successful cancellation; recovery remains an explicit operator action.
- CLI run detects the server's real/fake mode, matrix uses the same suite selection,
  and stop HTTP timeout allows the scheduler's bounded acknowledgement period.
  The module server entrypoint works with `python -m pltest.coordinator.server`.
  Profiles have bounded nesting/node count as well as byte size.

Cancellation timeouts are cooperative: blocking device I/O must settle before
cleanup or lease release. The scheduler may remain nonterminal beyond the nominal
stop timeout while that bounded I/O settles; it never forcibly terminates the PS5.
The current coordinator acceptance is fake/transport evidence. A real coordinated
Windows/PS5 run requires a newly built TEST_LAB app that supports `coordinated` and
sticky readiness; the older installed PR2 hardware build is not evidence for PR5.

Review acceptance: 19 coordinator regression tests and all 45 portable Test Lab
checks passed on macOS. The isolated Linux review checkout passed the full
140-test suite (3 explicit environment skips) and native lifecycle/output/mock
checks. Live Uvicorn run `23d4613b-f9bc-420f-9f6a-e630b4a29b6d` returned fake PASS;
API/CLI JSON and the report after restart matched exactly. Full Clang Tidy and lint passed (259 attributed files), including a final
source/format/metadata pass after the CLI/server/CI updates. The packaged wheel
imports the coordinator and includes browser assets. Evidence is under ignored
`build/test-lab-pr5-review/`. All PR0–PR5 stages use `codex/test-lab`.

CI installs the coordinator package before discovering its FastAPI/Typer tests;
the shared `codex/test-lab` branch is included in the existing build workflow.
Without this dependency step, a clean CI runner cannot import the PR5 test module.
No browser download or Windows worker extras are required by coordinator CI.
