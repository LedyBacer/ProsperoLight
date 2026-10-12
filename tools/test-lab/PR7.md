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
