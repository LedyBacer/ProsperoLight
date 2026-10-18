# Legacy firmware experiment validation — 2026-10-18

Application build source: `9aad3d3a095a075c7cb29c1d300df461752500f0`.
Subsequent branch commits update evidence parsing/tests and this report, not the
application or SDK patch used in these seven builds.

- Linux LLVM 18: **seven complete installable builds PASS**. The first five ZIPs
  contain 133 files; no-PyroWave and combined contain 127. Container integrity,
  original package checks, ZIP CRC/0777 modes, source/variant labels, build
  receipts and exact artifact/ELF hash associations verified.
- Production defaults and configuration switches checked by actual host C++
  compilation. Keyboard/mouse preload and process lifetime preserved when the
  video preload is skipped. Diagnostic logging survives `_exit` without stdio
  flushing. Actual patched AGC target-state code passes ASan/UBSan for NULL,
  absent blocks/registers and valid defaults.
- `LINT_SKIP_TIDY=1 make lint`: PASS, 301 attributed files. Separate clang-tidy
  was not run. `make test-unit test-performance-guards test-integration`: PASS
  (64 GoogleTests; 220 Python tests, 4 skipped). Latest focused firmware suite:
  8 tests PASS, including nested Git source-patch isolation and GNU build-ID
  parsing. Source/shell syntax checks and `git diff --check`: PASS.
- [GitHub Actions run 39156566873](https://github.com/LedyBacer/ProsperoLight/actions/runs/39156566873):
  **SUCCESS** for all seven matrix build jobs. Fourteen artifacts are present:
  seven install ZIP artifacts and seven separately named symbols/evidence
  artifacts. Normal release build and GitHub release jobs were skipped.
- No deployment, console launch, main merge or production release performed.
  **FW 3.00/4.50 startup acceptance remains untested**. The NULL fault mechanism
  is identified against matching public binary build IDs; the reason the API
  returns NULL, and a successful firmware workaround, remain unproven.

The first CI attempt correctly stopped at the SDK sanitizer test because a
source snapshot nested in the parent repository could cause `git apply` to skip
its paths. Fixed by creating isolated Git roots for the generated source trees,
checking the patch result, and adding a regression test. The successful run
above builds the patched SDK from scratch, rather than trusting that attempt.

## Frozen local builder artifacts

All seven have separate `llvm-pie.elf`, build log, compiler options,
`build-provenance.json`, `fwcompat-sdk.json`, `artifact-receipt.json` and
`SHA256SUMS`. They are retained under `results/fwcompat-9aad3d3-lDw2yr/` in the
dedicated builder checkout; install ZIPs are also copied locally for delivery.

**These hashes describe the local builder outputs.** GitHub CI performs its
own builds: use its own per-variant receipt/SHA256SUMS and corresponding ELF.
Do not mix a local ZIP with an ELF from an independently packaged CI build,
even if the source SHA and variant label are the same. Symbol files stay out
of the installable ZIPs. The complete CI download set is available on the run
page above; symbols/evidence artifacts begin with `FWCompat-symbols-evidence-`.

| Variant | ZIP SHA256 | Matching local ELF SHA256 |
|---|---|---|
| 00-control | `ca1f7d966e6b6007eb84051f1c286a55f0b2af879f057e99a6ca68cb4acd6ef7` | `bf575862c6d5c3126b8bdbce02f779c3cadfc00af377942fb7e57fe24c8c2b90` |
| 01-no-warmup | `8ad7d525961c2136bdfd2b73eb3ccd2e7eeb299e56907af564bcfebb3b8d9d75` | `357953f621acca770a7f27acab6ff0192154547ead2e0be5daab50091d86b474` |
| 02-no-videodec2 | `5188dba2276438a78ef69415d2b5feaee37956a6a27f33245e538e5738fa086f` | `63a621ab48ecf00499b2bce24f90e2edd9a93f84841759dd2ad7a4f509d3b46c` |
| 03-no-shader-cache | `930bfe0fbe09e140e9ca83ce956ffa73b8416f7b218f6ed970b4b7b23802f204` | `e619bfcbac64f1b4380cb53c196c082c471f47340fd14dc180a92f0faeb4f230` |
| 04-force-1080p | `2001fc3aa81b24f577d5896b59704a1a988851530437c6432c62c7688fce1a42` | `206df145cadce9941d8d29faf6f47b8ebf0f865171095576d0a16b074fd1860f` |
| 05-no-pyrowave | `aa89719a0d175436ac329b38937a325af00e6c638d79ee58cf35e5392f2f8452` | `87ed0b292ae3b06863e2365d566a44616e443c5ef6f5b6cc84d8dca7f7a0fe84` |
| 06-combined | `01ec06acad9f9d33961f63eb54ca56ee78f3f7a6e892223a8be2c91ba51b3a60` | `33fba9419544c42adeb2927a9cba2ff715716e37735910b666c9eab0709088b8` |

## Recommended next step

Distribute the CI ZIPs together with the matching CI evidence receipts (or the
local ZIPs while retaining their local developer symbols). Test **00, 01, 02,
03, 04, 05, 06**, one whole app folder at a time. First obtain the control's
AGC init status, resolved defaults function address and actual returned pointer.
Do not stream with these startup experiments; 02 and 06 explicitly refuse it.
A guarded NULL failure is FAIL. A visible first menu frame and a matching fresh
LAUNCHER_READY record are both needed for startup acceptance.
