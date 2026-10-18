# FW 3.00–4.50 startup investigation

## Baseline and evidence limits

Base main: `b1b2c5d8cde17347419091b95c30fa95f22fedd0`.
The `.110` annotated release tag resolves to source commit
`ca3f25f24734d9ae40b200c5f8354c5680fb7123` (tag object
`d617c4e564ebf7123d94db08937b62c564f92650`).
The supplied report describes two identical null faults at absolute RIP
`0x5c4b06`, executable base `0x400000`, fingerprint
`c42ff125149aa6fde1bbd8b08009eeee00000000`. It is external reported evidence,
not a locally reproduced crash. No affected-firmware console was tested here.

Inspected `.110` CI run `38676031668`: release artifact `11945654508` contains
only `PPSA99002.zip` and `SHA256SUMS`; provenance artifact `11945644315` contains
only `build-provenance.json`. Available builder outputs belong to other source
revisions. No exact corresponding unstripped ELF was located, so **no reliable
function name is assigned to the old RIP**. Rebuilding the same source is not
proof of byte identity. Obtain the original ELF and verify its build association
before symbolizing that report. New variants freeze their own ELF and hashes.

`git diff 01.000.110 <base> -- src/main.cpp src/native_modules.cpp
src/launcher/launcher_ps5.cpp tools/fetch-opengl-sdk.sh third_party/ps5-homebrew-ui`
is empty. Changes since `.110` include controller waveform handling and launcher
copy, not the inspected preload/warmup/EGL/renderer startup sequence or SDK pin.
The control is current main with shared diagnostics/guard, not a reproduction
of the exact `.110` executable. Release `.110` additionally used CI performance
flags; the matrix retains current main defaults and records all definitions.

## Actual AGC path

ps5-opengl 1.0.2 source snapshot:
`a519561626933257213d64a4a8bd5e02b55dceb2`.
`ps5_agc_runtime_backend.c` includes `ps5_agc_native_runtime.c`; its native
`load_apis` binds the `agc_api_t` table to linked imports (the payload/probe
alternative uses `dlsym`). The patched object is exactly this backend member,
not the independent link stub. Normal `agc.init(8)` call sites and normal
`get_defaults()` call sites are wrapped. No extra AGC/defaults probe is made.
The runtime tracks successful initialization with `runtime_agc_initialized`;
init call numbers and results expose repeated calls without adding new ones.

`append_target_state` originally dereferences `defaults` at offset 0 before
checking the block pointers, then reads the row count at `+0x20`. It assumes a
block-pointer table of `agc_register_t` rows (offset/padding/value) with sixteen
required color-target register offsets. This is a firmware ABI assumption, not
proof of compatibility. The derivative guards NULL before the first read and
reports missing blocks/registers. Logging reads no additional structure fields;
non-NULL does not establish that an arbitrary firmware structure is readable or
has the expected layout. No synthetic defaults or fallback GPU state are used.

NULL, missing blocks/registers, or failed init set an application failure flag.
A first render with GL error or this flag does not emit LAUNCHER_READY. A
rejected runtime operation can therefore become a diagnosed graphics failure
instead of SIGSEGV; it remains **FAIL**, not a compatibility fix.

## Changes and hypotheses

Shared: synchronous bounded startup checkpoints go to kernel debug output and
straight to the existing launcher log descriptor regardless of saved debug
preferences. Pre-storage lines are bounded in memory and replayed once storage
opens. A JSONL receipt records identity/options and later stages. Critical
writes are synchronously flushed. This affects startup timing; these builds
must not be used for performance comparisons. Crash reporting is retained.
Renderer/canvas/EGL instrumentation is generated in `build/fwcompat-ui`; the
pinned UI submodule remains unchanged. It records API return values, framebuffer
status, two canvas calls, shader compilation/linking, allocation, clears and
first render/swap. Void operations have checkpoints; GL errors are sampled at
the end of the first render rather than consumed after every call.

| Variant | Difference from diagnostic control | Hypothesis / limitation |
|---|---|---|
| 00-control | None | Last successful checkpoint; shared guard can turn a crash into explicit failure |
| 01-no-warmup | Skip decoder queries only | Lazy codec initialization interferes; retains module preload before elevation |
| 02-no-videodec2 | Skip video preload + queries | Video module side effects; launcher only, streaming refused |
| 03-no-shader-cache | Unset PS5_SHADER_CACHE_DIR | Cached OpenGL compiler input; no cache deletion, RADV settings untouched |
| 04-force-1080p | Skip 4K attempt | Initial presentation size; 1920x1080 with SDK default 60 Hz |
| 05-no-pyrowave | PYROWAVE=0 | Broad linkage/heap/runtime influence, not a single-function isolation |
| 06-combined | 02 + 03 + 04 + 05 | Interaction between hypotheses; launcher only, no single-cause inference |

All switches default off in production. A nonzero workaround without diagnostics
is rejected at compile time. Variant IDs and effective switches are compiled
into startup identity. Update check/start entry points return false in these
experiments; installed app identity/version and saved config/pairing are kept.
The module skip variants are diagnostic-only. A validated warmup change or SDK
NULL guard could later be proposed independently; none is a proven old-firmware
startup fix yet.

## Reproducible builds

In a **dedicated clean Linux checkout**, initialize submodules, install the same
LLVM 18/host/Python prerequisites as `.github/workflows/tooling.yml`, then:

```sh
BUILD_JOBS=2 CMAKE_BUILD_PARALLEL_LEVEL=2 bash tools/build-fw-compat-matrix.sh
# Or select one:
bash tools/build-fw-compat-matrix.sh 01-no-warmup
```

`tools/fwcompat/build-sdk.py` verifies the complete original release archive
SHA256 `30d8e51dd062739b629346102be5932728ba714bc50b1bdcdf08306c8843d9a4`,
all its checksums, and its source revision. It extracts source snapshots,
applies the upstream PSBC header patch, applies our checked-in AGC patch, and
rebuilds **only** `ps5_agc_runtime_backend.o` using the release's recorded common
and native-runtime flags with relocated include paths. It copies the SDK into a
new output prefix and replaces that member there. Other runtime archive members
are compared byte for byte; other SDK libraries remain copied release bytes.
A new manifest, patch/object/archive hashes and exact compiler invocation
identify this **diagnostic derivative**, not an upstream validated SDK. The
original SDK/archive is never edited or relabeled. The host sanitizer test
executes the actual patched target-state function before compilation.

An existing derivative may be supplied with `FWCOMPAT_SDK_PREFIX`; its patch,
source, archive receipt and manifest are verified before use. All seven variants
use the same SDK derivative. Each app TU is unconditionally recompiled by the
existing build script; the matrix also discards its object directory per variant.
The checkout is exclusively locked. Do not run ordinary builds there concurrently.

Each result folder contains the complete installable ZIP, a separately frozen
`llvm-pie.elf`, full build log, options, build provenance, SDK receipt,
`artifact-receipt.json` and SHA256SUMS. The receipt binds ZIP, eboot, ELF and source
SHA. Symbols never enter the installable ZIP. Original app container/helper and
asset validations run before packaging; ZIP permissions/CRC/identity are checked.

The manual **Legacy firmware startup experiments** workflow accepts one/all
variants, isolates variants into separate jobs, and uploads installable ZIPs
separately from developer symbols/evidence. It has read-only repository
permissions, no tag trigger, release publication or deployment. On a new fork,
enable Actions before dispatch. For a branch-only workflow, use the existing **Build** workflow, select this
branch and enable **firmware_compat**. It calls this same matrix and skips its
normal release-build job, so no default-branch merge is required:

```sh
gh workflow run tooling.yml --ref fix/legacy-fw-startup-diagnostics -f firmware_compat=true
```

## Interpreting returned evidence

First compare the control's final BEGIN/END and the real default pointer after
successful AGC init. NULL only after warmup, avoided by `01`, supports an order
interaction; NULL with no decoder suggests firmware API/version behavior. A
non-NULL pointer followed by a fault inside target assembly supports a layout
or mapping problem, not a proven valid structure. 1080p-only success supports a
size-dependent path. Cache-only success implicates cache/compiler input but
requires another controlled warm/cold launch. No-PyroWave success alone cannot
separate heap, linked runtime and RADV influence. Combined-only success requires
pairwise follow-up experiments. Do not infer RADV as the common cause merely
because it is linked: the reported ProsperoTV failure lacks it.

After logs return, associate each report with its receipt/ELF, symbolize only
that ELF with `tools/symbolize-crash.py`, repeat the smallest differentiating
variant, and propose the narrow verified fix separately. Do not merge this
experiment or publish it as a normal release.
