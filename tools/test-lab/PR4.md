# PR4: Windows interactive worker

Run the worker as the logged-in user on an unlocked Windows desktop. SSH often
runs in Session 0; the worker detects that and refuses GUI measurement. A temporary
Task Scheduler entry using InteractiveToken can run in the existing logged-in
session without saving a password or changing display configuration. The reference
validation used this route in Session 3 and removed temporary test tasks afterward.
The worker is not a Session 0 service or an autostart installation.

Install an isolated environment on Windows:

```powershell
py -3.13 -m venv .venv
.venv\Scripts\python -m pip install -e 'tools/test-lab[worker,gpu]'
.venv\Scripts\python -m playwright install chromium
.venv\Scripts\pltest worker --mode managed --bind 127.0.0.1 --port 8096 `
  --token-file .local/worker-token --output build/host-runs
```

Run from repository root. A managed browser is headed Chromium with a fresh
isolated context and selected monitor placement. `--browser-channel chrome` or
`msedge` explicitly selects an installed branded browser instead; there is no
silent fallback. The actual managed browser version is recorded. The worker closes
only its owned browser. `--mode attached` serves the same local page but does not
open or close an operator's browser. Open `http://127.0.0.1:8096/`, prepare from the
controller, then click Ready. That gesture enters fullscreen and starts a measured
preflight. The worker's lifetime is bounded by `--duration` (default one hour).

`--bind PRIVATE_LAN_ADDRESS` additionally allows an authenticated controller on
that network; the browser page/authentication still uses Windows loopback. A bearer
token is created in the supplied local token file and never stored in profiles,
reports, browser URLs or access logs. Page WebSocket access requires loopback,
matching Origin and an in-memory page token; cross-origin token fetch is refused.
Control WebSocket access uses the Authorization header and allows only one owner.
If local firewall policy blocks LAN access, an existing SSH tunnel can reach
loopback without changing firewall policy. POST `/shutdown` requires the same
bearer token and performs normal cleanup.

Read-only probes report current display device/driver identity, geometry, current
integer refresh, interactive session/input desktop and HDR capability. HDR is
currently null with an explicit reason; a required HDR profile fails preflight.
Select an exact `\\.\DISPLAYn` ID, or `virtual` only if exactly one driver identifies
itself as virtual. Ambiguity is an error, not a guessed display. No settings API
changes the OS/Sunshine/Vibepollo modes.

A Ready ACK requires fullscreen, visible/focused page, the selected display's
physical geometry and measured rAF within ±5% of the requested **host** target.
Ready expires when fresh page observation is missing or no longer meets the gate.
Host target accepts 30–240 FPS for display-only tests; PS5 TestProfile remains
30–120 FPS. A 240 Hz host trial does not establish 60/120 FPS stream comparability.
Hidden, misplaced, minimized or locked desktop observations stop the test normally
and yield INCONCLUSIVE. Heartbeat/overall deadlines provide the same bounded stop
when a page/controller is lost; there is no command to kill an unrelated browser.

The deterministic scenes are low-motion, noise, scrolling, static-keepalive,
stress and recover. Four seeded textures and interval storage are allocated before
a measurement; no random textures are allocated per frame. Ready and measured
windows use browser `performance.now()` / rAF callback timestamps, not PS5 clocks
or physical latency. Reports contain actual rendered frames, bounded intervals,
phase clock boundaries, content SHA-256 and a texture pixel-sample SHA-256. Rate,
coverage and overflow gates prevent a short/truncated run being declared VALID.
A sample overflow remains explicit INCONCLUSIVE; soak segmentation belongs to PR6.

The original `tools/pacing-test/decoder.js` now uses the same noise filler through
its existing server. The byte-level fixture verifies identical old seeded texture
bytes; existing decoder benchmark APIs and tests are retained.

Optional NVML samples are collected at 1 Hz, outside per-frame callbacks. Encoder
utilization and its sampling period are recorded as utilization, with no encoding
latency claim. Unsupported metrics are null with reasons. Read-only process/file
metadata describes the running server when the interactive user's permissions
allow it; otherwise version is null with a reason. `--host-log PATH` archives a
bounded host-local log slice, with credential redaction and rotation/truncation
flags. Authenticated host API logs and per-frame host timestamps are explicitly
unavailable unless a future documented adapter is configured. No credentials are
searched for or copied to the coordinator.

Standalone host test from any controller:

```sh
PYTHONPATH=tools/test-lab/src:tools/test-lab python -m pltest.host.smoke \
  --worker http://WINDOWS_OR_TUNNEL:8096 --token-file LOCAL_WORKER_TOKEN \
  --settings tools/test-lab/profiles/host-virtual-240.json --output build/host-tests
```

For an installed package omit PYTHONPATH. Windows uses `;` rather than `:` when
setting PYTHONPATH; package installation avoids that platform difference. The
settings example explicitly targets the validation machine's existing 240 Hz
virtual display. Select values matching your read-only inventory. This command
validates host stimulus only; PR5 will synchronize it with PS5 readiness.

## Verification (2026-10-12)

- Interactive inventory: Session 3 / Default desktop, Sunshine Virtual Display
  Driver `\\.\DISPLAY15`, 1920×1080, current mode 240 Hz. No display mode changed.
- Managed run `0d05002d-502e-4c8d-81c8-1065e597c2cb`: VALID, measured rAF
  239.983984 FPS, 1,200 total rendered frames including warmup, normal browser exit.
  Explicit Chrome channel, actual version 154.0.8037.98. Bundled Chromium download
  failed twice with TLS EPROTO; this is recorded as infrastructure evidence.
- Owned-window minimize `3e189f34-7d8b-42e9-b78a-e946519211b1`: INCONCLUSIVE;
  native helper matched exactly the run-specific title before minimizing it.
  Browser geometry moved to (-32000,-32000), rAF dropped; no other window changed.
- Attached run `b933ac50-b2f6-46b0-94e9-f0648933ddfe`: VALID, measured rAF
  239.989990 FPS. A separately launched browser connected to the page and an
  automated trusted Ready-button click exercised operator mode in Session 3.
  This was real Windows/browser evidence but no human-assisted click is claimed.
- Fake aiohttp session covers authenticated prepare/Ready/start/final report
  without a PS5, plus unauthorized access refusal. Pure tests cover locked/hidden
  desktop, wrong/ambiguous monitor, rate mismatch, unsafe command IDs, interval
  validation and log redaction. Node fixtures cover unchanged old noise bytes and
  the asynchronous texture-preparation race discovered on hardware.

Final managed run `9f07103f-3d42-4390-9080-bedefa7fd7d7` measured 239.745739 FPS,
VALID, with successful resource cleanup. Terminal status is published only after
owned-browser cleanup and atomic report persistence; a cleanup failure produces
INCONCLUSIVE. The preceding final-check run `ef963cd2-7e45-4063-8593-953e8859f060`
exposed premature terminal publication; its client result is INCONCLUSIVE and is
retained as failure evidence. A regression fixture verifies the corrected ordering.
Temporary interactive tasks were removed after normal worker shutdown.

The host version was unavailable to the unprivileged interactive worker, despite
read-only SSH inspection finding Apollo Sunshine.exe ProductVersion 2.0.0. These
are distinct privilege contexts; the worker does not substitute that value.
Raw evidence is under ignored `build/test-lab-pr4/` and Windows host-local run
folders. No coordinated PS5 workload/performance comparison or HDR/VRR claim is
made by this milestone.

API documentation used: [Playwright Python](https://github.com/microsoft/playwright-python/blob/main/playwright/sync_api/_generated.py),
[aiohttp server](https://github.com/aio-libs/aiohttp/blob/master/docs/web_quickstart.rst),
[Windows display enumeration](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-enumdisplaydevicesw),
[interactive task logon](https://learn.microsoft.com/en-us/windows/win32/taskschd/principal-logontype),
[NVML device queries](https://docs.nvidia.com/deploy/nvml-api/latest/api/group__nvmlDeviceQueries.html).
Context7 resolved Playwright/aiohttp; it returned unrelated results for NVML, so
its interfaces were checked against NVIDIA's primary documentation.
