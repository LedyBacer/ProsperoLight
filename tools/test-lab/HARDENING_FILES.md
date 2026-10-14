# Hardening changed files

Scope: changes after `89e89b4e5b2a627a90304ff5e0e0a09460f7f856` on `codex/test-lab`. The acceptance ledger is `HARDENING.md`; this file enumerates the implementation scope.

## Native evidence and control

Successful backend initialization supplies settings; control-worker polling and bounded clock commands preserve frame-loop isolation.

- `include/test_lab_lifecycle.hpp`
- `src/moonlight_stream.cpp`
- `src/pyrowave/stream_backend.cpp`
- `src/pyrowave/video/pyrowave_video_backend.cpp`
- `src/test_lab_lifecycle.cpp`
- `src/test_lab_profile.cpp`

## Host worker and authenticated telemetry

Interactive browser execution, deterministic GPU workload, physical display observation, scoped TLS-authenticated session exclusion and explicit unknown telemetry.

- `tools/test-lab/src/pltest/host/advanced_color.py`
- `tools/test-lab/src/pltest/host/client.py`
- `tools/test-lab/src/pltest/host/display.py`
- `tools/test-lab/src/pltest/host/logs.py`
- `tools/test-lab/src/pltest/host/metrics.py`
- `tools/test-lab/src/pltest/host/protocol.py`
- `tools/test-lab/src/pltest/host/session_guard.py`
- `tools/test-lab/src/pltest/host/static/gpu-scene.js`
- `tools/test-lab/src/pltest/host/static/stimulus.html`
- `tools/test-lab/src/pltest/host/static/stimulus.js`
- `tools/test-lab/src/pltest/host/worker.py`
- `tools/test-lab/src/pltest/session_evidence.py`

## Analysis, CLI and coordinator

Prepared manifests and safe cross-build execution, immutable reports, canonical verdicts, bounded phase metrics, per-metric noise screening and evidence views.

- `tools/test-lab/collect_run.py`
- `tools/test-lab/control_run.py`
- `tools/test-lab/ps5/launcher/Makefile`
- `tools/test-lab/ps5/launcher/main.c`
- `tools/test-lab/src/pltest/analysis.py`
- `tools/test-lab/src/pltest/build.py`
- `tools/test-lab/src/pltest/cli.py`
- `tools/test-lab/src/pltest/comparison.py`
- `tools/test-lab/src/pltest/coordinator/adapters.py`
- `tools/test-lab/src/pltest/coordinator/api.py`
- `tools/test-lab/src/pltest/coordinator/cli.py`
- `tools/test-lab/src/pltest/coordinator/models.py`
- `tools/test-lab/src/pltest/coordinator/scheduler.py`
- `tools/test-lab/src/pltest/coordinator/static/app.js`
- `tools/test-lab/src/pltest/coordinator/static/index.html`
- `tools/test-lab/src/pltest/coordinator/store.py`
- `tools/test-lab/src/pltest/environment.py`
- `tools/test-lab/src/pltest/export.py`
- `tools/test-lab/src/pltest/phases.py`
- `tools/test-lab/src/pltest/progress.py`
- `tools/test-lab/src/pltest/remote_builder.py`
- `tools/test-lab/src/pltest/report.py`
- `tools/test-lab/src/pltest/sweeps.py`
- `tools/test-lab/src/pltest/verdicts.py`

## Tests and build targets

Current-checkout import binding, contracts, backend evidence, resource ownership, high-refresh timing, comparison and export security.

- `Makefile`
- `tests/test_test_lab_analysis.py`
- `tests/test_test_lab_collect_run.py`
- `tests/test_test_lab_coordinator.py`
- `tests/test_test_lab_dashboard.py`
- `tests/test_test_lab_executor.py`
- `tests/test_test_lab_lifecycle.py`
- `tests/test_test_lab_sweeps.py`
- `tests/test_test_lab_worker.py`

## Documentation

Operational and hardware acceptance boundaries, with exact identities and retained failures.

- `tools/test-lab/HARDENING.md`

This index additionally introduces `tools/test-lab/HARDENING_FILES.md` and the sanitized machine-readable acceptance index `tools/test-lab/evidence/hardening-2026-10-14.json`. Ignored build artifacts, machine-local scripts and credentials are outside the Git change set.
