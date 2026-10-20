# Round 2 verified CI artifacts

[GitHub Actions run 39515915112](https://github.com/LedyBacer/ProsperoLight/actions/runs/39515915112): **SUCCESS**, exactly four matrix jobs.
Build source: `d2fddd25ca9990dc894a01968c791c992ae1fa6c`. Later documentation-only commits do not change these binaries.

All four installable ZIPs were downloaded from this run, along with their matching
developer artifacts. Verified: ZIP CRC, 0777 file modes, complete PPSA99002 package
(133 files each), title ID, variant/source label, effective warm-up mask and early-init
options, eboot SHA256, receipts, SDK patch identity, and exact unstripped ELF SHA256
with debug information. Runtime payload ELFs remain in the app package; the developer
`llvm-pie.elf` stays in the separate evidence artifact.

Focused CI host regressions: 9 tests PASS per job. Actual patched SDK code passed
ASan/UBSan host harnesses for target defaults, early-init failure and later reuse.
Native SDK derivative and all four applications compiled successfully. No full
integration/performance suite was run. No round-2 hardware launch was performed.

An independent Linux SDK rebuild also passed the sanitizer harnesses and verified
all unchanged archive members. The first two CI attempts stopped at obsolete
dependency URLs; the final run uses current owners with the same pinned Lapy
commit and byte-identical OpenGL release archive/SHA256.

## Installable ZIPs

Hashes below refer to the **inner installable ZIP**, not the GitHub artifact wrapper.
Open the artifact link, download, and unpack the wrapper first. Sign in to GitHub.
Install artifacts are retained for 30 days; developer evidence for 90 days.

| ZIP / download artifact | SHA256 |
|---|---|
| [ProsperoLight-FWCompat-00-control.zip](https://github.com/LedyBacer/ProsperoLight/actions/runs/39515915112/artifacts/12309335477) | `c1baaf8e491dbe9ee45df700b708a069e4c49887e3603e027f0876e7497b11bf` |
| [ProsperoLight-FWCompat-07-compute-only.zip](https://github.com/LedyBacer/ProsperoLight/actions/runs/39515915112/artifacts/12306839440) | `308b87e05c9d1894c48dd22544ef35effcc54c28c8eff37d0c01cbc42de70f48` |
| [ProsperoLight-FWCompat-08-codecs-only.zip](https://github.com/LedyBacer/ProsperoLight/actions/runs/39515915112/artifacts/12308545678) | `c0dab5fe9bf91381d25b085f100e54b2a0e078b6658aea62a9a1bc7ed692cc3a` |
| [ProsperoLight-FWCompat-09-agc-first.zip](https://github.com/LedyBacer/ProsperoLight/actions/runs/39515915112/artifacts/12308143254) | `b3ddcf606795beb80f39b0c1fc0cb27d8a73a4aa13bcd47bac20dcc544b3c495` |

## Exact artifact names

| Install artifact | Matching developer symbols/evidence artifact |
|---|---|
| `ProsperoLight-FWCompat-00-control-d2fddd25ca9990dc894a01968c791c992ae1fa6c` | `FWCompat-symbols-evidence-00-control-d2fddd25ca9990dc894a01968c791c992ae1fa6c` |
| `ProsperoLight-FWCompat-07-compute-only-d2fddd25ca9990dc894a01968c791c992ae1fa6c` | `FWCompat-symbols-evidence-07-compute-only-d2fddd25ca9990dc894a01968c791c992ae1fa6c` |
| `ProsperoLight-FWCompat-08-codecs-only-d2fddd25ca9990dc894a01968c791c992ae1fa6c` | `FWCompat-symbols-evidence-08-codecs-only-d2fddd25ca9990dc894a01968c791c992ae1fa6c` |
| `ProsperoLight-FWCompat-09-agc-first-d2fddd25ca9990dc894a01968c791c992ae1fa6c` | `FWCompat-symbols-evidence-09-agc-first-d2fddd25ca9990dc894a01968c791c992ae1fa6c` |

Each developer artifact contains the exact `llvm-pie.elf`, compiler options,
build log, build provenance, SDK receipt, artifact receipt and SHA256SUMS. Do not
mix ELFs/ZIPs from other CI runs or independent local builds.
[Machine-readable inventory](LEGACY_FW_AGC_ARTIFACTS.json) also records eboot
and ELF hashes and direct links to the matching developer artifacts.

[Tester instructions](LEGACY_FW_AGC_TESTING.md) · [Evidence and interpretation](LEGACY_FW_AGC_ANALYSIS.md)

**Compatibility is unverified for these new binaries.** A successful API return,
build or software checkpoint alone does not prove a visible menu, usable decoder
or working stream. Await physical PS5 reports before selecting a workaround.

[Ready-to-send Discord message](LEGACY_FW_AGC_DISCORD.md).
