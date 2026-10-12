# ProsperoLight developer Test Lab

Implemented as logical stages in one branch based on upstream/main
`78947e272566bbac31a5baab8965bf73cea87740`:

- [PR0](ps5/README.md): one-shot registered-title launch and real argv proof.
- [PR1](PR1.md): transient session profiles without persistent preference changes.
- [PR2](PR2.md): lifecycle, normal stop/cancel and immutable completion receipts.
- [PR3](PR3.md): portable PS5 executor, ownership and hashed run archives.
- [PR4](PR4.md): Windows interactive browser worker and deterministic stimulus.
- [PR5](PR5.md): versioned local API, durable fake/real adapter scheduler and CLI.
- [PR6](PR6.md): clock-aware trace analysis, suites and paired screening.
- [PR7](PR7.md): local dashboard, read-only doctor and downloadable archives.

Use [the onboarding guide](../../docs/TEST_LAB.md) for setup, real runs and exports.
Standalone PS5 and host-worker commands remain available; only coordinated runs
provide combined evidence. Performance comparisons require separately valid measurements.

Python 3.11+ is required. Use an isolated virtual environment and
`python -m pip install -e 'tools/test-lab[worker,gpu]'`. Coordinator dependencies install by default; worker dependencies are
optional on an executor-only machine. Use `pltest --help`, `pltest worker --help`,
or the source-tree entry point `python3 tools/test-lab/pltest_cli.py`.

Test builds explicitly use `TEST_LAB=1`, a `test-lab-...` build label,
`PERFORMANCE_DETAIL=1`, `LAN_TELEMETRY=0`. Ordinary releases keep Test Lab disabled.
Never deploy while a title is running; pair hosts once through the existing UI.
No test creates credentials or changes saved v8 configuration/display modes.
Keep raw logs, worker tokens and per-run data outside tracked source, for example
under ignored `build/`. Tokens and host-local API credentials never belong in
profiles or reports. LAN worker connections require bearer authentication; use an
SSH tunnel if local firewall policy prevents direct access rather than altering it.

Host-only checks:

```sh
python3 -m unittest discover -s tests -p 'test_test_lab*.py'
python3 -m unittest discover -s tests -p 'test_decoder_benchmark.py'
python3 -m unittest discover -s tests -p 'test_pacing_test_tools.py'
make test
make lint
```

Run these from repository root, using the virtual environment interpreter to run
optional aiohttp fake-worker tests. Browser pattern fixtures need Node. SDK app
builds and full lint need the existing Linux/WSL development environment.

Final PR0–PR4 verification: Linux full suite 121 tests (3 explicit Node skips),
native/mock lifecycle suites and full static analysis passed (250 attributed
files). Final worker ordering regression passed on Linux; macOS portable Test Lab
suite passed 26 tests with Node and optional aiohttp enabled. Final packaged
wheel imports and browser assets were verified. Windows portable checks passed
with unavailable native compiler checks skipped. See PR3/PR4 notes for exact
hardware run IDs and limits. All milestones share the `codex/test-lab` branch. PR0–PR5 are logical stages;
publication and exact validation evidence are recorded in the stage documents.

PR5 supports a durable SQLite coordinator with explicitly separate fake and
configured real-adapter modes. A real coordinated Windows/PS5 smoke run and
cancellation during measurement passed on 2026-10-12. See [PR5](PR5.md) for exact
build/run identities, validation boundaries and reproduction settings.
