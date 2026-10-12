# PR7 — local dashboard and integration onboarding

The coordinator serves a packaged, dependency-free dashboard at `/`. It connects
to same-origin API v1, lists recent runs, starts explicit profiles, requests normal
stop, polls at two seconds, shows events and three separate classifications,
charts PS5-local trace quantiles, and downloads artifacts or a bundled archive.
Comparison accepts explicit repeated A/B pairs. The dashboard never renders the
Windows stimulus on the coordinator machine and never deploys an app implicitly.
CLI/API remain the principal reproducible automation interfaces.

Bearer credentials are entered into a password field, cleared after connecting
and retained only in page memory. No localStorage, cookies or URL token. The
public shell cannot access protected API data. Host allowlisting, Origin and
cross-site request checks, no CORS wildcard, CSP and safe text rendering protect
controls. LAN binding requires token, explicit allowed hosts and origins; use a
trusted TLS/SSH tunnel for remote access. Asset paths are fixed and artifact paths
continue to reject traversal, temp files and symlinks.

`GET /api/v1/doctor` probes installed eboot hash over FTP, title inventory through
Payload Manager and authenticated Windows interactive desktop health without
launch, deployment, lease, pairing or OS-setting changes. Profile-specific pairing
and capability checks still happen before a real run. Fake doctor says SIMULATED.
`pltest export UUID --output result.zip` downloads a terminal bundle, redacting
private addresses/credential fields by default. Local raw artifacts remain
untouched. The export manifest hashes transformed bytes; redaction is a practical
text scrub, not a guarantee that arbitrary diagnostic prose contains no sensitive
information. Review bundles before sharing them outside the LAN.

[docs/TEST_LAB.md](../../docs/TEST_LAB.md) covers Windows worker + Mac/Linux client,
manual initial pairing, developer build/deployment, exact hash/profile matching,
smoke/regression, cancellation, comparison, exports and recovery. Native build
remains Linux/WSL-only or explicit prebuilt/external; no implicit SSH credentials,
WSL installation or privileged Windows service is added. `make test-lab-test`
runs the portable lab, legacy decoder and pacing tools regression suites.

Dashboard verification uses a real local API and in-app browser: Connect, read-only
doctor, create/start fake run, terminal classifications and indexed artifacts.
Security and archive tests exercise bearer/origin/host checks, read-only doctor,
terminal-only bundles, content hashes and raw-data preservation. Real combined
hardware checks and repeated fresh-build acceptance are recorded below.

## Final host verification and fresh-build boundary

Portable checks passed: 63 lab tests, five legacy decoder tests and two pacing
tool tests. Linux `make test` passed 159 Python tests (three Node skips), plus
native/mock/sanitizer checks. Full Clang Tidy passed before the final Python-only
review fixes; final source/metadata/shell lint passed for 270 attributed files.
The 0.7.0 wheel includes all dashboard assets and the six executable suites.

The fresh `test-lab-pr67-25157d2` developer app was built, signed, uploaded and
read back on PS5. Eboot SHA256:
`f9e13d7d77cfcb69a3c155c4b4e0134c655b25b91e415f2719f728cda3fd3848`.
Production config and pairing hashes remained unchanged. Two fresh-build
PyroWave attempts (`8eeae8cd-1c4a-442b-9d5e-4c25e57c87c9`,
`2655a7ac-94f4-45b1-8cd7-341021f07c97`) stopped before title launch at
`RAF_BELOW_TARGET`: fullscreen, visible and focused Chrome measured about
170 FPS on the unchanged 240 Hz virtual display. Cleanup completed without
errors. These runs do not validate the new PyroWave evidence counter or a
fresh-build stream. The earlier real native smoke/matrix results above remain
valid functional evidence; real paired performance validation remains pending
complete negotiated settings and a stable host stimulus.

Fresh-build acceptance after the operator closed the competing Moonlight session:
PyroWave `be167b38-667a-4c67-a978-1b6789aeab83` and HEVC
`5da15671-c158-4136-84ea-dc5edbaf2754` both returned functional PASS, fresh
artifacts and confirmed normal title exit. PyroWave confirmed 1802 unique
measurement pictures over 30.074s (59.920 FPS); host stimulus measured
239.897 rAF FPS. Ready-to-submit p95/p99 were 404/1576us, and the post-present
CPU observation timing was 26/40us, not physical scanout latency. These results
validate the repaired bounded developer-only output evidence on real hardware.
Preparing the virtual-display stimulus after the own PS5 stream became ready
removed the dependency on a separate Moonlight session. Measurement validity
remains INCONCLUSIVE: negotiated settings are missing and global host session
exclusion was not authenticated. Optional session guard behavior is host-tested;
its real authenticated disconnect has not been executed without API credentials.
