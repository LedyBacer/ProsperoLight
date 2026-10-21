#!/usr/bin/env bash
# ps5-native-app-boilerplate / ProsperoLight - Firmware startup experiment.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
# No deployment, tags or releases. Run in a dedicated clean checkout.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
[[ $# -le 1 ]] || { echo 'usage: build-fw-compat-matrix.sh [variant-id]' >&2; exit 2; }
source "$root/tools/native-toolchain.sh"
unset APP_DEFINITIONS
mkdir -p .deps results
exec 9>.deps/fwcompat-matrix.lock
flock -n 9 || { echo 'Another matrix build owns this checkout' >&2; exit 2; }
[[ -z $(git status --porcelain --untracked-files=no) ]] || { echo 'Commit tracked changes before freezing builds' >&2; exit 2; }
sha=$(git rev-parse HEAD)
out=$(mktemp -d "$root/results/fwcompat-${sha:0:7}-XXXXXX")
# Every variant uses the SAME patched runtime, including control. Never patch the stock SDK.
if [[ -z ${FWCOMPAT_SDK_PREFIX:-} ]]; then
    bash tools/setup-native-dependencies.sh > "$out/setup.log" 2>&1
    python3 tools/fwcompat/build-sdk.py --output "$out/diagnostic-sdk" \
        --payload-sdk "$root/.deps/native/ps5-payload-sdk" > "$out/sdk-build.log" 2>&1
    export FWCOMPAT_SDK_PREFIX="$out/diagnostic-sdk/sdk"
fi
export PS5_OPENGL_PREFIX=$(realpath -e "$FWCOMPAT_SDK_PREFIX")
python3 - "$PS5_OPENGL_PREFIX" <<'PY'
import hashlib,json,sys
from pathlib import Path
p=Path(sys.argv[1]);r=json.loads((p/'fwcompat-sdk.json').read_text())
assert r['source_sha']=='a519561626933257213d64a4a8bd5e02b55dceb2'
assert r['patch_sha256']==hashlib.sha256(Path('tools/fwcompat/opengl-1.0.2.patch').read_bytes()).hexdigest()
assert r['runtime_archive_sha256']==hashlib.sha256((p/'lib/libps5_opengl_core33.a').read_bytes()).hexdigest()
PY
(cd "$PS5_OPENGL_PREFIX" && sha256sum --check --strict --quiet manifest.sha256)
cp "$PS5_OPENGL_PREFIX/fwcompat-sdk.json" "$out/"
sdk_id=$(python3 -c 'import json,sys; r=json.load(open(sys.argv[1])); print("ps5_opengl_1_0_2_fwcompat_"+r["patch_sha256"][:12])' "$out/fwcompat-sdk.json")
printf '%s\n' "$sha" > "$out/source-sha.txt"
python3 - "${1:-}" > "$out/variants.txt" <<'PY'
import json,sys
with open('tools/fwcompat/variants.json') as f: variants=json.load(f)
if not sys.argv[1] or sys.argv[1] in ('round2','round3','round3-partitions'):
    ids=('07-compute-only','08-codecs-only') if sys.argv[1]=='round3-partitions' else ('00-control','07-compute-only','08-codecs-only','09-agc-first',*[v['id'] for v in variants[10:] if int(v['id'][:2]) <= 17]) if sys.argv[1]=='round3' else ('00-control','07-compute-only','08-codecs-only','09-agc-first')
    variants=[v for v in variants if v['id'] in ids]
    sys.argv[1]=''
if sys.argv[1] and sys.argv[1]!='all':
    variants=[v for v in variants if v['id']==sys.argv[1]]
    if not variants:raise SystemExit('Unknown variant')
for v in variants:
    print(v['id'],1-v['warmup'],1-v['preload'],1-v['cache'],v['force_1080p'],v['pyrowave'],v.get('warmup_mask',3),v.get('agc_first',0),v.get('codec_mask',7),v.get('query_snapshots',0),v.get('compute_last',0),v.get('legacy_videoout',0))
PY
while read -r id warmup preload cache mode pyrowave mask agc_first codec_mask snapshots compute_last legacy_videoout; do
    folder="$out/$id"; mkdir "$folder"
    printf 'Building %s\n' "$id"
    # build.sh always recompiles every TU; also discard objects explicitly before flag changes.
    rm -rf -- "$root/build/obj" "$root/build/fwcompat-ui"
    options=(FWCOMPAT_DIAGNOSTICS=1 FWCOMPAT_VARIANT_ID="${id//-/_}"
        FWCOMPAT_SKIP_DECODER_WARMUP="$warmup" FWCOMPAT_SKIP_VIDEO_MODULE_PRELOAD="$preload"
        FWCOMPAT_DISABLE_SHADER_CACHE="$cache" FWCOMPAT_FORCE_1080P="$mode"
        FWCOMPAT_SDK_ID="$sdk_id" PYROWAVE="$pyrowave"
        FWCOMPAT_WARMUP_MASK="$mask" FWCOMPAT_AGC_FIRST="$agc_first"
        FWCOMPAT_CODEC_MASK="$codec_mask" FWCOMPAT_QUERY_SNAPSHOTS="$snapshots"
        FWCOMPAT_COMPUTE_LAST="$compute_last" FWCOMPAT_LEGACY_VIDEOOUT="$legacy_videoout"
        TEST_LAB=0 LAUNCH_PROOF=0 LAN_TELEMETRY=0 UPDATE_DEV_OFFER=0 UPDATE_AUTO_ACCEPT=0
        STREAM_SELF_TEST_FPS=0 VIDEO_OUTPUT_SELF_TEST_FPS=0 STOP_ACTIVE_APP_SELF_TEST=0
        BUILD_LABEL="FWCompat $id ${sha:0:7}")
    printf '%s\n' "${options[@]}" > "$folder/options.txt"
    make app "${options[@]}" > "$folder/build.log" 2>&1
    cp -a dist/PPSA99002 "$folder/"
    # Freeze symbols BEFORE another link replaces llvm-pie.elf.
    cp build/llvm-pie.elf "$folder/llvm-pie.elf"
    cp build/build-provenance.json "$folder/"
    cp "$out/fwcompat-sdk.json" "$folder/"
    python3 tools/fwcompat/package.py "$folder" "$id" "$sha"
done < "$out/variants.txt"
printf 'Offline builds complete. No console contacted.\n%s\n' "$out"
