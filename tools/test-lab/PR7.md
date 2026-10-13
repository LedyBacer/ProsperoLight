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

## Published integration acceptance

Installed developer app: `test-lab-pr67-5fd48df`, native source
`5fd48df645979939a0125d5d31d994d05e6c3761`, TEST_LAB=1,
PERFORMANCE_DETAIL=1, LAN_TELEMETRY=0. Eboot SHA256:
`59c76198bb0c0aca545eba4ffc0167140ab5e4260fdfbdc4e194be82b050a185`.
All 131 deployed files passed readback. PS5 moved from 192.168.1.137 to
192.168.1.134 during validation; production settings were not edited for that move.

Final HEVC smoke `41dcf9c9-bd37-4e2b-9e09-adcf3f347aed` and PyroWave smoke
`31b1ba2b-f12c-47f2-9805-0acf55744aa2` returned functional PASS, fresh archived
artifacts and confirmed normal title exit. Both receipts confirm paired host
cleanup before launch and on exit, despite the profile containing quit_host=false.
The existing paired GameStream identity performs this cleanup; no administrator
API token was used. Before the final native run the host retained a virtual
1080p/240 Hz display. After each final run Windows returned to the physical
1440p/180 Hz display. Thus normal title exit and host-app closure are verified
separately, rather than conflated.

HEVC confirmed 1802 measurement pictures at 59.917 FPS. PyroWave confirmed
1786 pictures at 59.393 FPS over 30.071s; its trace contains 1802 output samples,
which are not all confirmed completed pictures. PyroWave ready-to-submit p95/p99
were 4053/4342us; post-present CPU observation p95/p99 were 44/58us.
These are software observations, not physical scanout or input-to-photon latency.
Measurement remains INCONCLUSIVE and comparisons NOT_COMPARABLE because
negotiated settings and authenticated global host-session exclusion are absent.
Three-pair regression decisions are verified with synthetic fixtures, not claimed
as a real baseline performance proof.

Real doctor returned PASS in an idle-host check. CLI/API returned identical
immutable persisted reports after coordinator restart; the CLI correctly returns
3 for the real INCONCLUSIVE summary. Both final bundled exports passed content
hash checks and left raw artifacts unchanged. The dashboard was exercised in an
in-app browser against the fake API: connect, doctor, create/start, terminal
classification/events/artifacts; no console errors were observed.

The final portable target passes 68 lab, five legacy decoder and two pacing
tool tests. Linux full tests passed 164 Python tests (three Node skips), plus
native/mock/sanitizer checks. Full Clang Tidy passed for the installed native
source; subsequent Python-only fixes passed source/metadata/shell lint.
Pairing and user-setting hashes were preserved; the existing
config/self-update-sequence bookkeeping file changed across developer builds.
Optional administrator API disconnect remains host-tested only because no
Vibepollo administrator token was provisioned.
