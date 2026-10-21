# Round 3 verified diagnostic builds

**Twelve installable builds produced and downloaded**, with matching developer
symbols/evidence. No physical PS5 launch was performed for these binaries.

- [Main matrix](https://github.com/LedyBacer/ProsperoLight/actions/runs/39518059135): nine successful build jobs. Original 17 failed
  the 40-character build-label check; its incomplete evidence is not part of this set.
- [Replacement 17 run](https://github.com/LedyBacer/ProsperoLight/actions/runs/39518368438): **SUCCESS**.
  Its ID was shortened to `17-codecs-first`; the nine successful jobs were not rerun.
- [Matched 07/08 partition builds](https://github.com/LedyBacer/ProsperoLight/actions/runs/39518858325): **SUCCESS**.

00/09/10–16 use source `96ff0000876040a91d20a35280a5d15b4398912c`.
17 uses `086acbac912472a8d39ea88598c41b8b7d58ca8e`.
07/08 use `3f8738959fd739d92c1a9fe57b02b234caf644e8`.
All three commits have identical application source/headers, Makefile, SDK patch
and package script (Git diff checked). Later changes cover variant naming,
build-group selection, regression tests and documentation.
Each receipt retains its actual source SHA; do not substitute symbols between builds.

## Verification

Local focused suite: **10 tests PASS**, covering every query subset/order,
unchanged query parameters, error propagation, snapshot placement, launcher-only
gates, invalid configurations, matrix selection and label length. Actual patched
SDK target-state/early-init/reuse/snapshot code passes ASan/UBSan in a host harness.
Independent Linux SDK rebuild also passed; all other SDK archive members were
verified unchanged. Each successful CI job repeated focused tests and native builds.

All twelve ZIPs were downloaded and verified: CRC, 0777 modes, full PPSA99002
package (133 files each), title ID, source/variant label, query-mask/snapshot/order
options, SDK patch identity, receipts/SHA256SUMS, eboot hash, and exact matching
unstripped ELF with `.debug_info` and `.symtab`. The app debug ELF is separate
from the installation ZIP; existing runtime payload helpers remain in the package.
No full integration/performance suite, console deployment, main merge or release.

## Downloads and SHA256

Use these **successful artifact links**. Unpack the GitHub artifact wrapper first;
hashes below identify the inner installable ZIP. GitHub sign-in is required.
Install artifacts have 30-day retention; developer evidence 90 days.

| ZIP / install artifact | Inner ZIP SHA256 |
|---|---|
| [ProsperoLight-FWCompat-00-control.zip](https://github.com/LedyBacer/ProsperoLight/actions/runs/39518059135/artifacts/12311095074) | `7801f73b6f7bc95ad1d103695468b0da778b540e9fcb13035299bef7a7b012a5` |
| [ProsperoLight-FWCompat-07-compute-only.zip](https://github.com/LedyBacer/ProsperoLight/actions/runs/39518858325/artifacts/12310596939) | `fbb1705c6332d5cb77de197237aae956bb9cedd3f20edca2bd61f928525a21ec` |
| [ProsperoLight-FWCompat-08-codecs-only.zip](https://github.com/LedyBacer/ProsperoLight/actions/runs/39518858325/artifacts/12310996715) | `26690dfd8e83c27e10628f755c5cb7a61f9edd5930955d336019e168d88159c9` |
| [ProsperoLight-FWCompat-09-agc-first.zip](https://github.com/LedyBacer/ProsperoLight/actions/runs/39518059135/artifacts/12308468197) | `728ae1e55b43753f5b45914901805f7cf8218f1c774377af672ea231ce2048e6` |
| [ProsperoLight-FWCompat-10-query-snapshots.zip](https://github.com/LedyBacer/ProsperoLight/actions/runs/39518059135/artifacts/12310510661) | `bb67f30ed24c06245cab58835e60f4874d58126fa86d937b0fdf3b0a80585eef` |
| [ProsperoLight-FWCompat-11-h264-only.zip](https://github.com/LedyBacer/ProsperoLight/actions/runs/39518059135/artifacts/12310760571) | `7d86cb22738ac4f95702154027f9cc6d42e0ee21fb2f37e81b5e4a11ab6853e6` |
| [ProsperoLight-FWCompat-12-hevc-only.zip](https://github.com/LedyBacer/ProsperoLight/actions/runs/39518059135/artifacts/12310006796) | `505893ec06f680af7e898dce3435c38fac5f246bf753584012b23a2c1153d1d2` |
| [ProsperoLight-FWCompat-13-main10-only.zip](https://github.com/LedyBacer/ProsperoLight/actions/runs/39518059135/artifacts/12310785520) | `477915003493855c9cadcc827177a8df7be1d22ed24b7425193b4a4dd16c9d8f` |
| [ProsperoLight-FWCompat-14-compute-h264.zip](https://github.com/LedyBacer/ProsperoLight/actions/runs/39518059135/artifacts/12310490799) | `9949589bbfb63dd893532b0f9b431ca82b919b00a5a56e60ac0ba68d4dd3aa18` |
| [ProsperoLight-FWCompat-15-compute-hevc.zip](https://github.com/LedyBacer/ProsperoLight/actions/runs/39518059135/artifacts/12310395503) | `2918d52ecc7bd86a0e232334de85cea166ea1f20291c50744a76370de52ccadd` |
| [ProsperoLight-FWCompat-16-compute-main10.zip](https://github.com/LedyBacer/ProsperoLight/actions/runs/39518059135/artifacts/12311125381) | `9b1642e60ea656e4f0ba9b6a7d869f79c4f30c8dbffa0b304cabddd88016f498` |
| [ProsperoLight-FWCompat-17-codecs-first.zip](https://github.com/LedyBacer/ProsperoLight/actions/runs/39518368438/artifacts/12310331430) | `ac7b5dab62cb920e59ff5b50300c044cc3313ac02be25a48e55469a1067bf520` |

## Matching developer artifacts

| Variant | Exact developer artifact name / link |
|---|---|
| 00-control | [FWCompat-symbols-evidence-00-control-96ff0000876040a91d20a35280a5d15b4398912c](https://github.com/LedyBacer/ProsperoLight/actions/runs/39518059135/artifacts/12311075099) |
| 07-compute-only | [FWCompat-symbols-evidence-07-compute-only-3f8738959fd739d92c1a9fe57b02b234caf644e8](https://github.com/LedyBacer/ProsperoLight/actions/runs/39518858325/artifacts/12310272883) |
| 08-codecs-only | [FWCompat-symbols-evidence-08-codecs-only-3f8738959fd739d92c1a9fe57b02b234caf644e8](https://github.com/LedyBacer/ProsperoLight/actions/runs/39518858325/artifacts/12310586986) |
| 09-agc-first | [FWCompat-symbols-evidence-09-agc-first-96ff0000876040a91d20a35280a5d15b4398912c](https://github.com/LedyBacer/ProsperoLight/actions/runs/39518059135/artifacts/12309601065) |
| 10-query-snapshots | [FWCompat-symbols-evidence-10-query-snapshots-96ff0000876040a91d20a35280a5d15b4398912c](https://github.com/LedyBacer/ProsperoLight/actions/runs/39518059135/artifacts/12310795584) |
| 11-h264-only | [FWCompat-symbols-evidence-11-h264-only-96ff0000876040a91d20a35280a5d15b4398912c](https://github.com/LedyBacer/ProsperoLight/actions/runs/39518059135/artifacts/12310835574) |
| 12-hevc-only | [FWCompat-symbols-evidence-12-hevc-only-96ff0000876040a91d20a35280a5d15b4398912c](https://github.com/LedyBacer/ProsperoLight/actions/runs/39518059135/artifacts/12310106696) |
| 13-main10-only | [FWCompat-symbols-evidence-13-main10-only-96ff0000876040a91d20a35280a5d15b4398912c](https://github.com/LedyBacer/ProsperoLight/actions/runs/39518059135/artifacts/12310345738) |
| 14-compute-h264 | [FWCompat-symbols-evidence-14-compute-h264-96ff0000876040a91d20a35280a5d15b4398912c](https://github.com/LedyBacer/ProsperoLight/actions/runs/39518059135/artifacts/12311065580) |
| 15-compute-hevc | [FWCompat-symbols-evidence-15-compute-hevc-96ff0000876040a91d20a35280a5d15b4398912c](https://github.com/LedyBacer/ProsperoLight/actions/runs/39518059135/artifacts/12310795363) |
| 16-compute-main10 | [FWCompat-symbols-evidence-16-compute-main10-96ff0000876040a91d20a35280a5d15b4398912c](https://github.com/LedyBacer/ProsperoLight/actions/runs/39518059135/artifacts/12311130351) |
| 17-codecs-first | [FWCompat-symbols-evidence-17-codecs-first-086acbac912472a8d39ea88598c41b8b7d58ca8e](https://github.com/LedyBacer/ProsperoLight/actions/runs/39518368438/artifacts/12310756277) |

Developer evidence includes `llvm-pie.elf`, build log, options, build provenance,
SDK receipt, artifact receipt and SHA256SUMS. Full exact install/developer artifact
names, ELF/eboot hashes and per-build source/run links are retained in the
[machine-readable inventory](LEGACY_FW_AGC_ROUND3_ARTIFACTS.json).

[FW 3.00 test order and recovery](LEGACY_FW_AGC_ROUND3.md) ·
[Ready-to-send English message](LEGACY_FW_AGC_ROUND3_DISCORD.md)

**Await hardware reports:** build success, API return zero or a non-NULL pointer
does not establish a visible menu, firmware compatibility or working streaming.
