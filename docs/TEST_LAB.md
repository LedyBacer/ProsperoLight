# Developer Test Lab

This is a local hardware test tool, not a production app or an external relay.
CLI/API and the dashboard control a coordinator. The deterministic fullscreen
stimulus executes on the Windows streaming host; PS5 runs an explicitly marked
ProsperoLight test build. Manually pair the saved host once through the existing
ProsperoLight UI. Tests never create pairing credentials or change persistent v8
configuration, OS display modes, HDR settings or Sunshine/Vibepollo configuration.

## Install and check the portable tools

Use Python 3.11+ in a virtual environment on the Mac/Linux coordinator:

```sh
python3 -m venv .venv-test-lab
. .venv-test-lab/bin/activate
python -m pip install -e tools/test-lab
make test-lab-test
pltest coordinator --data-dir build/test-lab-coordinator
```

Open `http://127.0.0.1:8765/`, Connect, Check environment. The initial coordinator
uses **fake** adapters; fake PASS is simulated and never valid performance data.
A first no-hardware run is reproducible with:

```sh
pltest doctor
pltest run --suite fake-smoke --profile tools/test-lab/profiles/fake-smoke.json
pltest report RUN_UUID --json
pltest export RUN_UUID --output build/fake-result.zip
```

Stop the fake coordinator before starting real mode on the same port/data directory.

## Windows interactive worker

In an unlocked, logged-in Windows terminal (not a service/Session 0):

```powershell
py -3 -m venv .venv-test-lab
.\.venv-test-lab\Scripts\Activate.ps1
python -m pip install -e 'tools/test-lab[worker,gpu]'
python -m playwright install chromium
New-Item -ItemType Directory -Force build\test-lab-worker | Out-Null
python -c "import secrets,pathlib; pathlib.Path('build/test-lab-worker/token').write_text(secrets.token_urlsafe(48))"
pltest worker --bind 127.0.0.1 --port 8096 --token-file build/test-lab-worker/token --output build/test-lab-worker/runs --mode managed
```

Protect the token file with account-local permissions; never commit it or paste it
into profiles/logs. Provision the same token to a private coordinator-side file
through your existing trusted transfer route. Installed Chrome is an explicit
alternative with `--browser-channel chrome`. Attached browser/Ready setup and
read-only display selection are described in [PR4](../tools/test-lab/PR4.md).
Confirm the streamed display is the display containing the scene; unknown or
ambiguous display/refresh/HDR evidence prevents a valid measurement. Minimized,
hidden, unfocused, nonfullscreen or locked-desktop runs are inconclusive.

Use an already configured SSH/TLS tunnel. For example, with existing SSH access
and worker listening on Windows loopback:

```sh
ssh -N -L 127.0.0.1:18096:127.0.0.1:8096 WINDOWS_USER@WINDOWS_HOST
```

No firewall change is needed. Direct LAN binding is opt-in and bearer-protected;
use trusted encrypted transport. The API token and worker token are separate.
The worker has a bounded lifetime (`--duration`) and owns only its managed browser.

## Build/deploy a developer app

A Linux/WSL machine with the project's existing PS5 SDK/dependencies is required.
Mac tooling does not imply a Mac-native SDK. Build an explicitly labeled image:

```sh
BUILD_JOBS=2 CMAKE_BUILD_PARALLEL_LEVEL=2 make app TEST_LAB=1 PERFORMANCE_DETAIL=1 LAN_TELEMETRY=0 BUILD_LABEL=test-lab-MY_REVISION
```

Use the repository's build environment, pinned dependencies and native toolchain
PATH. Build launcher helpers using [PR3](../tools/test-lab/PR3.md); route-marked
probe and launch helpers differ. Inspect the generated provenance and eboot hash.
An optional WSL/external builder is operator-managed; the coordinator does not
install WSL, start SSH sessions with embedded credentials or deploy implicitly.

Close ProsperoLight normally before deployment. The portable executor offers an
explicit guarded build deployment/run path:

```sh
pltest run --ps5 PS5_HOST --probe-elf /path/probe.elf --launch-elf /path/launch.elf \
  --profile /private/profile.json --eboot-sha256 EXACT_NEW_EBOOT_SHA256 \
  --deploy /path/dist/PPSA99002 --output build/deploy-smoke
```

`--deploy` requires title-idle proof, exclusive remote lease, marked build,
atomic uploads and hash readback. This command performs a standalone PS5 run;
for combined acceptance, then start the configured real coordinator. To repeat
with a changed build, close the coordinator, use this guarded path with a new
label/hash/output directory, update the real coordinator's expected hash/profile
label and restart. Preserve previous archives. Do not overwrite a running title.
Installing a developer image does not claim to restore production bits.

## Real coordinated smoke and 90s regression

Copy `tools/test-lab/profiles/pr2-native-smoke.json` into ignored local storage.
Set `source_sha` and `build_label` to the exact installed developer build, saved
paired host/app, codec/resolution/FPS and explicit transient options. Use a unique
run UUID assigned by the coordinator. Example extra coordinator fields:

```json
{
  "host_stimulus": {"scene":"low-motion","seed":137,"fps":240,"monitor":"virtual"},
  "timing": {"startup_seconds":30,"ready_seconds":30,"warmup_seconds":10,
             "measure_seconds":30,"cooldown_seconds":0,"hard_seconds":240,"stage_timeout_seconds":120}
}
```

This example assumes a verified 240Hz selected display and 60FPS PS5 stream.
Stimulus rAF target is the display refresh rate; never assume the target is 120 or
240 on a different monitor. The worker checks actual rAF within ±5%. Use a unique
monitor ID when virtual display selection is ambiguous.

```sh
export PLTEST_PS5_HOST=PS5_HOST
export PLTEST_PS5_PROBE_ELF=/absolute/path/probe.elf
export PLTEST_PS5_LAUNCH_ELF=/absolute/path/launch.elf
export PLTEST_PS5_EBOOT_SHA256=EXACT_INSTALLED_EBOOT_SHA256
export PLTEST_HOST_WORKER_URL=http://127.0.0.1:18096
export PLTEST_HOST_WORKER_TOKEN_FILE=/private/worker-token
pltest coordinator --mode real --data-dir build/test-lab-coordinator
```

In another activated coordinator terminal:

```sh
pltest doctor
pltest run --suite decoder-smoke --profile build/smoke.json
pltest report RUN_UUID --json
pltest export RUN_UUID --output build/smoke-result.zip
```

No PS5 controller interaction is needed after pairing when the normal app's
controller requirements and worker auto-Ready preflight are satisfied. Dashboard
Connect → paste the same profile → Create and start uses the same API, normal
stop and artifacts. Native controller availability remains an app prerequisite.

For `decoder-matrix`, set warmup=10, measure=80 and
`host_stimulus.scene="decoder-regression"`: total workload is 90s, divided into
10s warmup / 20s simple / 40s noise-motion / 20s recovery. For PyroWave, choose
`pyrowave-matrix`, omit native `decoder_pipeline`/`decoder_cores`, and explicitly
request codec, chroma/HDR and bitrate that the host supports. Never compare native
HEVC timings with PyroWave timings as equivalent codecs.

For pacing/bitrate sweeps, create explicit single-point profiles and run them
sequentially under the corresponding suite. `pltest matrix --profile ...
--repetitions N` repeats one point. Stop a sweep on loss, continuous backlog,
unsupported mode or invalid measurement; inspect actual measured Mbps rather than
a requested ceiling. High bitrate/network exposure is an operator decision.
Soak is repeated <=120s segments, each with its own receipt/trace and warmup.
Longer than 240s full-trace profiles are rejected; there is no hidden ring overflow.

## Comparison and exports

Run interleaved A/B rather than all A then all B:

```sh
pltest paired --baseline build/A.json --candidate build/B.json \
  --manifest-root build --repetitions 3 --allow-deploy
pltest compare --pairs build/pairs.json
```

Pairs JSON is `[["A1_UUID","B1_UUID"],["A2_UUID","B2_UUID"],["A3_UUID","B3_UUID"]]`.
Two single UUIDs still return INCONCLUSIVE/INSUFFICIENT_REPEAT_PAIRS. Each pair
requires distinct completed real runs, valid measurements, matching fingerprints
and verified environmental facts in `analysis.environment`: firmware,
host version, encoder settings, network class, build flags and renderer.
Firmware, host version, encoder settings and build flags come from observed
evidence/provenance; network topology remains an explicit operator attestation.
Missing negotiated settings cannot be repaired by that attestation. Screening
reports pair distributions and drift; it is not statistical proof.

Reports separate functional PASS/FAIL, measurement VALID/INCONCLUSIVE/UNSUPPORTED
and delta IMPROVED/REGRESSED/NO_CLEAR_CHANGE/NOT_COMPARABLE. Exit codes: 0 summary
PASS, 1 FAIL, 2 infrastructure/API error, 3 INCONCLUSIVE, 4 CANCELLED. A functional
hardware PASS may still exit 3 for invalid comparative measurement. Receipt schema
`prosperolight-test-receipt/2` records successful decoder setup. Analysis cross-checks
independent host session and backend runtime evidence. Each analyzed negotiated/
effective field includes its source and verification status; missing or mismatched
required fields remain INCONCLUSIVE.
Schema 1 receipts cannot establish verified negotiation.

Local raw archives include profile/events/PS5/host/stimulus/analysis/JSON/HTML and
SHA256 manifest. Exports default to private-address and credential-field redaction;
export manifests hash exported bytes, while raw local files remain unchanged.
`--no-redact` is an explicit local raw export. Review sensitive diagnostics before
sharing. No report is automatically uploaded outside the LAN.

## Stop, recovery and verification

```sh
pltest stop RUN_UUID
pltest status RUN_UUID
pltest artifacts RUN_UUID
```

Stop is cooperative and uses normal stream teardown. Device I/O settles before
ownership can release; nominal stop deadlines are not forced process kill timers.
Never reboot PS5 or terminate unrelated titles. Restarting the coordinator does
not replay launches. An uncertain launch retains the remote lease; use the PR3
explicit recovery command only after confirmed title exit and matching run/owner
identity. Infrastructure/cleanup failure is INCONCLUSIVE and preserves evidence.

Run `make test-lab-test`, existing `make test` and Linux SDK `make lint` for changes.
See [PR6](../tools/test-lab/PR6.md) and [PR7](../tools/test-lab/PR7.md) for exact
hardware evidence and remaining measurement boundaries. Tokens/raw run archives
belong in ignored `build/`, never tracked source.

## Existing streams and virtual display lifecycle

Developer Test Lab launch uses the existing PS5 paired GameStream `/cancel`
before starting a fresh app session, then requires an acknowledged cancel and
successful status refresh with no current game. A refused/uncertain cleanup
fails with HOST_CLEANUP_REJECTED/HOST_CLEANUP_UNCONFIRMED; launch is not replayed.
Test Lab also always invokes paired cancel on exit, independently of the saved
Quit Host preference. The receipt records `paired_host_cleanup_confirmed` and
`paired_host_exit_cleanup_confirmed`; unconfirmed exit cleanup prevents PASS. This does not change ordinary
production launch or claim visibility of every Remote Input/Monitor session.
No administrator token is required for this paired path.

For a per-session virtual display, PREPARE_STIMULUS is deferred until the own
PS5 stream is ready and has created its display. Browser Ready is still required
before warmup/measurement. A display belonging to an unrelated Moonlight stream
is no longer required. Do not keep another Moonlight session running for tests.

An optional authenticated Vibepollo guard provides a broader paired Moonlight
inventory before PS5 launch. Configure PLTEST_HOST_API_URL (HTTPS origin),
PLTEST_HOST_API_TOKEN_FILE (private local file), optional PLTEST_HOST_API_CA_FILE
(trusted server certificate/CA; TLS validation stays enabled), and
PLTEST_HOST_SESSION_POLICY=reject or disconnect. Default reject blocks a busy host.
Explicit disconnect calls `/api/clients/disconnect` once per connected paired
client UUID and polls until clients and RTSP inventory are empty. Unidentified
owners, unsupported schema, refusal or timeout stop the run; no process kill,
unpair, app deletion, configuration edit or API mutation retry occurs.

To provision the optional token, use Vibepollo Web UI API Tokens with GET
`/api/clients/list`, GET `/api/rtsp/sessions`, and POST
`/api/clients/disconnect`. This is separate from the Windows account password.
Never pass an RTSP history UUID as a paired client UUID. Without this adapter,
reports retain HOST_SESSION_EXCLUSIVITY_UNVERIFIED, even when paired cancel
is acknowledged. This guard covers Moonlight RTSP, not WebRTC or all remote roles.


Final deploy checks read the destination inventory and hashes, not just upload
temporary files. Unexpected files/directories fail with `INSTALL_UNEXPECTED_*`:
inspect them manually while TITLE_IDLE; the executor never deletes unknown data.
Unmodified drakmor/ftpsrv is supported automatically through bounded Unix LIST;
MLSD-capable servers retain typed MLSD inventory. Requests/profiles and diagnostic
archiving use the same helper, without NLST/FEAT. Final expected-file SIZE and
SHA256 readback are verified separately from enumeration. Reports identify
`LIST_READBACK` or `MLSD_READBACK` and retain enumeration limitations: drakmor LIST
uses stat(), follows symlinks and may omit dangling/unstatable entries. LIST is
visible-entry verification for a trusted managed installation, not complete
filesystem attestation. Signed SELF extraction is disabled only on the known
ftpsrv connection so readback hashes cover stored bytes.
The lease is retained after uncertain helper execution, deployment or launch;
provably read-only/local pre-write failures may release it. Verified inventory
and its source/limitations are archived with each deployed run. Comparison requires COMPLETED execution, overall PASS,
clean cleanup and VALID measurement; calibrated noise remains workload-specific.
An early saturation stop is INCONCLUSIVE / SATURATION_DETECTED, not a completed
sweep; individual point reports keep their independent verdicts.

Upstream `136ac41` is integrated by a normal merge: OpenGL SDK 1.0.2 and
`opengl-1.0.2` shader-cache versioning. Existing physical acceptance evidence
retains its original SDK identity. See [integration smoke](TEST_LAB_SMOKE.md)
for the bounded new-build acceptance procedure and recovery commands.
