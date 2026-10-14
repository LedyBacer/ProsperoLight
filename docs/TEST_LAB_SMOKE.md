# One-build integration smoke

Run one native HEVC SDR 1080p60 / 20 Mbps scenario. This is functional integration
acceptance, not a performance comparison or physical scanout/VRR certification.
Deployment requires explicit operator authorization for this developer installation.
Close ProsperoLight normally, select a PS5 user and leave the controller connected.
Do not open the title while the executor owns it. Never overwrite an unexpected
or production installation. Keep configuration/pairing outside the app folder.

## Prepare artifacts and environment

Use the documented Linux SDK environment and the pinned dependencies in the
current checkout. OpenGL SDK 1.0.2 and its archive/manifest hashes are pinned by
`tools/fetch-opengl-sdk.sh`; shader cache uses `opengl-1.0.2`. Build once:

```sh
BUILD_JOBS=2 CMAKE_BUILD_PARALLEL_LEVEL=2 make app TEST_LAB=1 LAN_TELEMETRY=0 BUILD_LABEL=test-lab-smoke-MY_COMMIT
```

Copy `dist/PPSA99002` and `build/build-provenance.json` through your trusted
transfer route into `build/integration-smoke/artifacts/PPSA99002` and
`build/integration-smoke/artifacts/provenance.json`. Do not mix provenance from
production with a Test Lab folder. Build route-three probe and route-four launch
ELFs with [PR3](../tools/test-lab/PR3.md) and [launcher Makefile](../tools/test-lab/ps5/launcher/Makefile).
For a configured payload SDK, run `make -C tools/test-lab/ps5/launcher PR0_ROUTE=3`
and copy `test-launcher-route3.elf` as `probe.elf`, then run the same target with
`PR0_ROUTE=4` and copy `test-launcher-route4.elf` as `launch.elf`.
The helper capability markers are checked before execution. No helper rebuild
is needed just for the application SDK version change.

Install the package and configure the interactive Windows worker and SSH tunnel
as in [Test Lab](TEST_LAB.md). Worker must run in the unlocked user desktop;
loopback port 8096 is forwarded to coordinator port 18096. PS5 requires FTP 2121,
Payload Manager 8084 and an enabled ELF execution route. RTSP inventory requires
the provisioned scoped Vibepollo token and trusted TLS certificate/pin. Stop other
streaming sessions normally. Default host policy rejects a busy host.

Set paths/addresses to the actual environment (secrets are private files):

```sh
. .venv-test-lab/bin/activate
export PLTEST_API_MODE=real PLTEST_API_URL=http://127.0.0.1:18766
export PLTEST_PS5_HOST=192.168.1.134
export PLTEST_PS5_PROBE_ELF="$PWD/build/probe.elf"
export PLTEST_PS5_LAUNCH_ELF="$PWD/build/launch.elf"
export PLTEST_HOST_WORKER_URL=http://127.0.0.1:18096
export PLTEST_HOST_WORKER_TOKEN_FILE="$PWD/build/private/worker-token"
export PLTEST_HOST_API_URL=https://192.168.1.5:47990
export PLTEST_HOST_API_TOKEN_FILE="$PWD/build/private/vibepollo-token"
export PLTEST_HOST_API_CERT_SHA256="$(cat build/private/vibepollo-tls-pin)"
export PLTEST_BUILD_ROOT="$PWD/build/integration-smoke/artifacts"
```

Prepare a profile and manifest. Use the saved paired host/app; this does not pair
or change persistent settings. The example assumes the selected virtual display
is already at 240 Hz; replace its ID and stimulus FPS with independently observed
values if different. Do not change the OS display mode to make a test pass.

```sh
mkdir -p build/integration-smoke
python - <<'PY'
import json, os, pathlib
root=pathlib.Path(os.environ['PLTEST_BUILD_ROOT'])
p=json.loads(pathlib.Path('tools/test-lab/profiles/pr2-native-smoke.json').read_text())
provenance=json.loads((root/'provenance.json').read_text())
p.update(source_sha=provenance['commit'],
         build_label=(root/'PPSA99002/build-label.txt').read_text().strip())
p['client'].update(host='192.168.1.5:47989',app='Desktop')
p['host_stimulus']={'scene':'gpu-entropy','seed':137,'fps':240,'monitor':'virtual'}
p['timing'].update(warmup_seconds=10,measure_seconds=30,hard_seconds=240,stage_timeout_seconds=120)
p['analysis_environment']={'network_class':'PS5 Ethernet / Windows Ethernet','network_source':'operator-confirmed'}
pathlib.Path('build/integration-smoke/profile.json').write_text(json.dumps(p,indent=2))
PY
pltest prepare-variant --directory "$PLTEST_BUILD_ROOT/PPSA99002" \
  --provenance "$PLTEST_BUILD_ROOT/provenance.json" \
  --profile build/integration-smoke/profile.json --manifest-root "$PLTEST_BUILD_ROOT" \
  --output "$PLTEST_BUILD_ROOT/variant.json"
python - <<'PY'
import hashlib,json,os,pathlib
m=pathlib.Path(os.environ['PLTEST_BUILD_ROOT'])/'variant.json'
p=pathlib.Path('build/integration-smoke/profile.json');v=json.loads(p.read_text())
v.update(allow_deploy=True,build_variant={'manifest':'variant.json','sha256':hashlib.sha256(m.read_bytes()).hexdigest()})
p.write_text(json.dumps(v,indent=2))
print('Expected identity:',json.loads(m.read_text())['source_sha'],v['build_label'])
PY
export PLTEST_PS5_EBOOT_SHA256="$(python -c 'import json,os,pathlib; print(json.loads((pathlib.Path(os.environ["PLTEST_BUILD_ROOT"])/"variant.json").read_text())["eboot_sha256"])')"
```

Keep a read-only preflight record of the FTP banner, installed developer label/hash,
Payload Manager process inventory, saved config/pairing hashes and host sessions.
The known drakmor banner selects bounded Unix LIST and connection-local raw SELF
transfer. MLSD is used on servers supporting it. LIST follows server stat semantics:
it cannot attest hidden/unstatable entries or detect every symlink. Only proceed
for a trusted managed app directory. Inventory errors must never imply absence.

## Execute exactly once

In the configured shell start the coordinator:

```sh
pltest coordinator --mode real --port 18766 --data-dir build/integration-smoke/coordinator
```

In another terminal with the same environment:

```sh
pltest doctor
pltest run --profile build/integration-smoke/profile.json --suite decoder-smoke --wait --timeout 600
pltest status RUN_UUID
pltest report RUN_UUID --json
pltest artifacts RUN_UUID
pltest export RUN_UUID --output build/integration-smoke/result.zip
python - <<'CHECK'
import hashlib,json,zipfile
with zipfile.ZipFile('build/integration-smoke/result.zip') as z:
    m=json.loads(z.read('export-manifest.json'))
    assert set(z.namelist()) == set(m['files']) | {'export-manifest.json'}
    for name,expected in m['files'].items():
        data=z.read(name)
        assert len(data)==expected['bytes'] and hashlib.sha256(data).hexdigest()==expected['sha256'],name
print('Export file set and SHA256 PASS')
CHECK
```

Before deployment `doctor` reports installed-hash mismatch if the old developer
build is present; this is expected only after its identity is separately checked.
Do not ignore an idle, transport or worker failure. `run` validates the manifest,
acquires the exclusive remote lease, confirms TITLE_IDLE, inventories the existing
installation, verifies every final expected file by SIZE/SHA256, checks the new
label/hash and launches once. It prepares the host stimulus, measures, stops
normally, collects fresh artifacts and releases ownership only after verified exit.
Record the returned UUID; never reissue `run` to resolve an uncertain first attempt.

## Acceptance and recovery

PASS requires COMPLETED, functional PASS, measurement VALID, no cleanup errors,
matching new SDK/source/label/hash, full final manifest verification, fresh matching
schema-2 receipt/diagnostics, title exit verified, remote lease absent, no remaining
host RTSP/client sessions, and unchanged saved config/pairing hashes. Check artifact
SHA256 manifests against actual bytes. An individual smoke has no A/B delta.
Exit 3/INCONCLUSIVE is not PASS, even if a desktop appeared. Record source/hash,
SDK version, UUID and archive/report hashes separately from older SDK evidence.

On failure preserve the coordinator data, executor journal, report and partial
artifacts. Do not delete files, reboot, force-close titles or retry deployment.

- FTP inventory failure: inspect server/permissions read-only. A 550 alone proves
  neither absence nor permission. No unverified cleanup or recursive deletion.
- Incomplete deployment: keep the lease. Inspect the executor journal and compare
  every final installed file with the selected manifest using
  `pltest.ps5.ftp.installation_inventory`; only a complete readback plus TITLE_IDLE
  establishes a reconciled installation. Unknown files require operator inspection.
- Missing helper receipt or unknown launch: keep the lease and inspect mailbox,
  manager processes and console state. Close the title normally if active; do not
  overwrite an unconsumed mailbox or replay a launch.
- Failed collection: use a fresh destination and the original UUID (read-only):

```sh
pltest collect --ps5 "$PLTEST_PS5_HOST" --probe-elf "$PLTEST_PS5_PROBE_ELF" \
  --launch-elf "$PLTEST_PS5_LAUNCH_ELF" --run-id RUN_UUID \
  --output build/integration-smoke/recovered --timeout 150
```

Cooperative stop is `pltest stop RUN_UUID`. Inspect `status` until settled. After
complete installation reconciliation and confirmed normal title exit, use only
the run and owner UUID recorded in that executor journal:

```sh
pltest release-lease --ps5 "$PLTEST_PS5_HOST" --probe-elf "$PLTEST_PS5_PROBE_ELF" \
  --launch-elf "$PLTEST_PS5_LAUNCH_ELF" --run-id RECORDED_RUN_UUID --owner RECORDED_OWNER_UUID
```

This checks ownership twice and freshly probes idle; it does not repair an
installation. If either identity or hardware state is uncertain, leave the lease
in place. No automatic production restoration is performed.
