#!/usr/bin/env bash
# ps5-native-app-boilerplate - Native Linux/WSL application build.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Compiles, links, signs, validates, and assembles the root skeleton app.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
source "$root/tools/native-toolchain.sh"
format=${1:-Folder}
format=${format,,}
case "$format" in folder|ffpkg|ffpfsc|all) ;; *)
    echo "usage: tools/build.sh [Folder|Ffpkg|Ffpfsc|All]" >&2
    exit 2
esac

for command in python3 sha256sum; do
    command -v "$command" >/dev/null || {
        echo "missing required command: $command" >&2
        exit 2
    }
done
bash "$root/tools/setup-native-dependencies.sh" >/dev/null
# The launcher draws with ps5-opengl: fetch the pinned SDK and write its link group.
bash "$root/tools/prepare-opengl.sh"

param="$root/sce_sys/param.json"
title_id=$(python3 - "$param" <<'PY'
import json, re, sys

with open(sys.argv[1], encoding="utf-8") as source:
    value = json.load(source)

title_id = value.get("titleId", "")
concept_id = value.get("conceptId", "")
content_id = value.get("contentId", "")
if not re.fullmatch(r"PPSA\d{5}", title_id):
    raise SystemExit("param.json titleId must use PPSA followed by five digits")
if not re.fullmatch(r"\d{5}", concept_id):
    raise SystemExit("param.json conceptId must contain five digits")
if (not re.fullmatch(r"[A-Z]{2}\d{4}-PPSA\d{5}_00-[A-Z0-9]{16}", content_id)
        or title_id not in content_id):
    raise SystemExit("param.json contentId must be valid and contain titleId")
if not re.fullmatch(r"\d{2}\.\d{3}\.\d{3}", value.get("contentVersion", "")):
    raise SystemExit("param.json contentVersion must use NN.NNN.NNN")
if not re.fullmatch(r"\d{2}\.\d{2}", value.get("masterVersion", "")):
    raise SystemExit("param.json masterVersion must use NN.NN")
size = value.get("downloadDataSize")
if isinstance(size, bool) or not isinstance(size, int) or size < 0:
    raise SystemExit("param.json downloadDataSize must be a non-negative integer")

category = value.get("applicationCategoryType")
badge = value.get("contentBadgeType")
if (category, badge) not in {(0, 1), (65536, 2)}:
    raise SystemExit("param.json category and badge must describe a game or media app")
if category == 0:
    intents = value.get("gameIntent", {}).get("permittedIntents", [])
    if not any(item.get("intentType") == "launchActivity" for item in intents):
        raise SystemExit("game param.json must permit the launchActivity intent")
elif "gameIntent" in value:
    raise SystemExit("media param.json must not contain gameIntent")

localized = value.get("localizedParameters", {})
language = localized.get("defaultLanguage", "")
title = localized.get(language, {}).get("titleName", "")
if not isinstance(title, str) or not title.strip():
    raise SystemExit("param.json default-language titleName cannot be empty")
print(title_id)
PY
)

# Loader/container constants validated on firmware 6.02 and 12.70. These are
# deliberately separate from the public application version in param.json.
module_sdk=0x02000009
companion_sdk=0x08050001
fself_magic=0x1D3D154F

bash "$root/tools/validate-assets.sh" "$root/sce_sys"

sdk_root="$root/.deps/native/ps5-payload-sdk"
zlib_root="$root/.deps/native/zlib/root"
zlib_archive=$(find "$zlib_root" -type f -name libz.a -print -quit)
cxx=${CXX:-}
if [[ -z $cxx ]]; then
    cxx=$(command -v clang++-18 || command -v clang++)
fi
[[ -n $cxx ]] || { echo "Clang++ was not found" >&2; exit 2; }

build="$root/build"
dist="$root/dist"
native="$root/tooling/native"
tool="$build/host/ps5-native-tool"
mkdir -p "$build/host" "$build/obj" "$dist"
"$cxx" -std=c++20 -O2 -Wall -Wextra -Werror \
    -I "$zlib_root/usr/include" \
    "$native/native_app_builder.cpp" "$native/self_container.cpp" \
    "$native/elf_object.cpp" "$native/sce_module_writer.cpp" \
    "$zlib_archive" -o "$tool"

# The launcher is built from the UI kit in third_party/ps5-homebrew-ui.
mapfile -d '' -t source_paths < <(
    find "$root/src" "$root/third_party/ps5-homebrew-ui" -type f \
        \( -name '*.c' -o -name '*.cc' -o -name '*.cpp' \) \
        ! -path "$root/src/gamestream/*" \
        -print0 | sort -z
)
sources=()
for source in "${source_paths[@]}"; do
    sources+=("${source#"$root/"}")
done
(( ${#sources[@]} > 0 )) || { echo "src/ has no C or C++ sources" >&2; exit 2; }

definitions=()
includes=()
archives=()
import_stubs=()
pacbrew_packages=()
pacbrew_includes=()
pacbrew_archives=()
[[ -z ${APP_DEFINITIONS:-} ]] || read -r -a definitions <<< "$APP_DEFINITIONS"
[[ -z ${APP_INCLUDE_PATHS:-} ]] || read -r -a includes <<< "$APP_INCLUDE_PATHS"
[[ -z ${APP_STATIC_ARCHIVES:-} ]] || read -r -a archives <<< "$APP_STATIC_ARCHIVES"
[[ -z ${APP_IMPORT_STUBS:-} ]] || read -r -a import_stubs <<< "$APP_IMPORT_STUBS"
[[ -z ${PACBREW_PACKAGES:-} ]] || read -r -a pacbrew_packages <<< "$PACBREW_PACKAGES"
[[ -z ${PACBREW_INCLUDE_PATHS:-} ]] || read -r -a pacbrew_includes <<< "$PACBREW_INCLUDE_PATHS"
[[ -z ${PACBREW_STATIC_ARCHIVES:-} ]] || read -r -a pacbrew_archives <<< "$PACBREW_STATIC_ARCHIVES"

pacbrew_cflags=()
pacbrew_libs=()
if (( ${#pacbrew_packages[@]} > 0 || ${#pacbrew_includes[@]} > 0 || ${#pacbrew_archives[@]} > 0 )); then
    pacbrew_resolution=$(bash "$root/tools/setup-pacbrew-dependencies.sh" \
        --resolve "${pacbrew_packages[@]}")
    mapfile -d '' -t pacbrew_cflags < <(python3 -c \
        'import json,sys; [print(v, end="\0") for v in json.loads(sys.argv[1])["cflags"]]' \
        "$pacbrew_resolution")
    mapfile -d '' -t pacbrew_libs < <(python3 -c \
        'import json,sys; [print(v, end="\0") for v in json.loads(sys.argv[1])["libs"]]' \
        "$pacbrew_resolution")
    pacbrew_root=$(python3 -c 'import json,sys; print(json.loads(sys.argv[1])["root"])' \
        "$pacbrew_resolution")
    for include in "${pacbrew_includes[@]}"; do
        [[ $include =~ ^[A-Za-z0-9_.+-]+(/[A-Za-z0-9_.+-]+)*$ &&
            -d $pacbrew_root/user/homebrew/$include ]] || {
            echo "invalid PacBrew include path: $include" >&2; exit 2;
        }
        pacbrew_cflags+=("-I$pacbrew_root/user/homebrew/$include")
    done
    for archive in "${pacbrew_archives[@]}"; do
        [[ $archive =~ ^[A-Za-z0-9_.+-]+(/[A-Za-z0-9_.+-]+)*\.a$ &&
            -f $pacbrew_root/user/homebrew/$archive ]] || {
            echo "invalid PacBrew static archive: $archive" >&2; exit 2;
        }
        pacbrew_libs+=("$pacbrew_root/user/homebrew/$archive")
    done
    printf 'PacBrew dependencies: %s\n' "${pacbrew_packages[*]:-(manual archives)}"
fi

# Sources are compiled side by side, as many at once as BUILD_JOBS allows.
max_jobs=${BUILD_JOBS:-$(nproc 2>/dev/null || printf '2')}
[[ $max_jobs =~ ^[1-9][0-9]*$ ]] || { echo "BUILD_JOBS must be positive" >&2; exit 2; }
compile_failed=0
objects=()
for source in "${sources[@]}"; do
    [[ $source =~ ^(src|third_party/ps5-homebrew-ui)/[A-Za-z0-9_./-]+\.(c|cc|cpp)$ && -f $root/$source ]] || {
        echo "invalid source: $source" >&2; exit 2;
    }
    object="$build/obj/${source//\//_}.o"
    if [[ $source == *.c ]]; then standard=-std=c11; else standard=-std=c++20; fi
    args=("$standard" -O2 -Wall -Wextra -ffunction-sections -fdata-sections)
    # Neither the app nor the UI kit throws or asks for a type at run time.
    [[ $source == *.c ]] || args+=(-fno-exceptions -fno-rtti)
    for definition in "${definitions[@]}"; do
        [[ $definition =~ ^[A-Za-z_][A-Za-z0-9_]*(=[A-Za-z0-9_]+)?$ ]] || {
            echo "invalid compile definition: $definition" >&2; exit 2;
        }
        args+=("-D$definition")
    done
    for include in "${includes[@]}"; do
        [[ $include =~ ^[A-Za-z0-9_.-]+(/[A-Za-z0-9_.-]+)*$ && -d $root/$include ]] || {
            echo "invalid include path: $include" >&2; exit 2;
        }
        args+=("-I$root/$include")
    done
    args+=("${pacbrew_cflags[@]}")
    while (( $(jobs -rp | wc -l) >= max_jobs )); do
        wait -n || compile_failed=1
    done
    PS5_PAYLOAD_SDK="$sdk_root" sh "$root/tooling/prospero-clang18" \
        "${args[@]}" -c "$root/$source" -o "$object" &
    objects+=("$object")
done
while (( $(jobs -rp | wc -l) > 0 )); do
    wait -n || compile_failed=1
done
(( compile_failed == 0 )) || { echo "compilation failed" >&2; exit 1; }

PS5_PAYLOAD_SDK="$sdk_root" sh "$root/tooling/prospero-clang18" \
    -std=c++20 -O2 -Wall -Wextra -fno-exceptions -fno-rtti \
    -ffunction-sections -fdata-sections \
    -c "$native/app_crt.cpp" -o "$build/obj/app_crt.o"

PS5_PAYLOAD_SDK="$sdk_root" sh "$root/tooling/prospero-clang18" \
    -std=c++20 -O2 -Wall -Wextra -fno-exceptions -fno-rtti \
    -ffunction-sections -fdata-sections \
    -c "$native/app_cpp_runtime.cpp" -o "$build/obj/app_cpp_runtime.o"

# The pinned libc++ archives record a POSIX pthread dependency. The clean-room
# runtime supplies those imports, so a valid local archive is enough for lld
# to resolve the dependency without dragging in a second runtime.
"$sdk_root/bin/prospero-ar" rcs "$build/obj/libpthread.a" \
    "$build/obj/app_cpp_runtime.o"

# lld consumes conventional shared objects, while the native module writer
# reads those same objects as public-system-module import stubs. Their tiny
# bodies are never packaged or executed on the console.
build_system_link_stub() {
    local library=$1
    local source=$2
    local object="$build/obj/${library}_link_stub.o"
    local output="$build/stubs/${library}.so"

    mkdir -p "${output%/*}"
    PS5_PAYLOAD_SDK="$sdk_root" sh "$root/tooling/prospero-clang18" \
        -std=c11 -O2 -fPIC -ffunction-sections -fdata-sections \
        -c "$root/$source" -o "$object"
    "$sdk_root/bin/prospero-lld" --shared -soname "${library}.prx" \
        -o "$output" "$object"
    printf '%s\n' "$output"
}

pngdec_stub=$(build_system_link_stub libScePngDec vendor/ps5/sdk/stubs/pngdec_link_stub.c)
videodec2_stub=$(build_system_link_stub libSceVideodec2 vendor/ps5/sdk/stubs/videodec2_link_stub.c)
mouse_stub=$(build_system_link_stub libSceMouse vendor/ps5/sdk/stubs/mouse_link_stub.c)
videoout_stub=$(build_system_link_stub libSceVideoOut vendor/ps5/sdk/stubs/videoout_link_stub.c)

link_inputs=("$build/obj/app_crt.o" "$build/obj/app_cpp_runtime.o" "${objects[@]}" \
    "$pngdec_stub" "$videodec2_stub" "$mouse_stub" "$videoout_stub")
for archive in "${archives[@]}"; do
    [[ $archive =~ ^[A-Za-z0-9_.-]+(/[A-Za-z0-9_.-]+)*\.a$ && -f $root/$archive ]] || {
        echo "invalid static archive: $archive" >&2; exit 2;
    }
    link_inputs+=("$root/$archive")
done
if (( ${#pacbrew_libs[@]} > 0 )); then
    link_inputs+=(--start-group "${pacbrew_libs[@]}" --end-group)
fi
# The OpenGL SDK's import libraries (AGC) are linked like the SDK stubs and
# handed to the converter so their imports resolve to system modules.
stub_paths=()
stub_options=()
for stub in "${import_stubs[@]}"; do
    [[ $stub =~ ^[A-Za-z0-9_.-]+(/[A-Za-z0-9_.-]+)*\.so$ && -f $root/$stub ]] || {
        echo "invalid import stub: $stub" >&2; exit 2;
    }
    stub_paths+=("$root/$stub")
    stub_options+=(--stub "$root/$stub")
done
# The OpenGL runtime needs the process heap in src/runtime/app_heap.c: every
# allocation of the executable goes through it.
wrap_options=()
for symbol in malloc calloc realloc free posix_memalign malloc_usable_size; do
    wrap_options+=("--wrap=$symbol")
done
"$sdk_root/bin/prospero-lld" -T "$native/ps5-pie.ld" --eh-frame-hdr "${wrap_options[@]}" \
    --version-script "$native/app-symbols.map" \
    --exclude-libs=ALL \
    -L "$build/obj" \
    -e _start -o "$build/llvm-pie.elf" "${link_inputs[@]}" \
    --as-needed "${stub_paths[@]}" "$sdk_root"/target/lib/*.so
"$tool" link --in "$build/llvm-pie.elf" --out "$build/eboot.elf" \
    --stub-dir "$sdk_root/target/lib" \
    --stub "$pngdec_stub" \
    --stub "$videodec2_stub" \
    --stub "$mouse_stub" \
    --stub "$videoout_stub" \
    "${stub_options[@]}" \
    --module-sdk "$module_sdk" \
    --companion-sdk "$companion_sdk" --file-name eboot.elf

app="$dist/$title_id"
rm -rf -- "$app"
mkdir -p "$app/sce_sys" "$app/sce_module"
"$tool" self --sign --in "$build/eboot.elf" --out "$app/eboot.bin" \
    --magic "$fself_magic"

# Filesystem access (tooling/elevation): the console's ELF loader runs this
# helper when the app starts. It only answers the title it was built for.
grep -Fq "target_title_id[] = \"$title_id\"" "$root/tooling/elevation/helper/main.cpp" || {
    echo "tooling/elevation/helper/main.cpp is not built for $title_id" >&2; exit 2;
}
make -s -C "$root/tooling/elevation/helper" PS5_PAYLOAD_SDK="$sdk_root" \
    OUTPUT="$build/elevation/sandbox-elevator.elf"
python3 "$root/tooling/elevation/validate-helper.py" "$build/elevation/sandbox-elevator.elf"
cp "$build/elevation/sandbox-elevator.elf" "$app/sandbox-elevator.elf"

cp "$param" "$app/sce_sys/param.json"
for asset in icon0.png pic0.dds pic1.dds snd0.at9; do
    [[ -f $root/sce_sys/$asset ]] && cp "$root/sce_sys/$asset" "$app/sce_sys/$asset"
done
[[ ! -d $root/assets ]] || cp -a "$root/assets" "$app/assets"
# Directory listing returns nothing under /app0 on the console, so each sound
# folder carries an index of its files (read by the kit's save::list_files).
for dir in "$app"/assets/audio/sfx/*/; do
    [[ -d $dir ]] || continue
    (cd "$dir" && find . -maxdepth 1 -type f ! -name index.txt -printf '%f\n' | LC_ALL=C sort > index.txt)
done

[[ -f $root/runtime/libc.prx ]] || bash "$root/tools/rebuild-libc.sh"
(cd "$root/runtime" && sha256sum --check --strict libc.prx.sha256)
runtime_modules=("$root/runtime/libc.prx")
additional_runtime=()
[[ -z ${APP_RUNTIME_MODULES:-} ]] || read -r -a additional_runtime <<< "$APP_RUNTIME_MODULES"
for source in "${additional_runtime[@]}"; do
    [[ $source =~ ^\.local/runtime/[A-Za-z0-9._-]+\.prx$ && -f $root/$source ]] || {
        echo "invalid runtime module: $source" >&2; exit 2;
    }
    runtime_modules+=("$root/$source")
done
declare -A runtime_names=()
for input in "${runtime_modules[@]}"; do
    name=${input##*/}
    [[ $name =~ ^[A-Za-z0-9._-]+\.prx$ && -z ${runtime_names[$name]+present} ]] || {
        echo "invalid or duplicate runtime module name: $name" >&2; exit 2;
    }
    runtime_names[$name]=present
    magic=$(python3 - "$input" <<'PY'
import struct, sys
with open(sys.argv[1], "rb") as stream:
    print(f"{struct.unpack('<I', stream.read(4))[0]:08x}")
PY
)
    if [[ $magic == 1d3d154f || $magic == eef51454 ]]; then
        cp "$input" "$app/sce_module/$name"
    else
        "$tool" self --sign --in "$input" --out "$app/sce_module/$name"
    fi
    "$tool" self --inspect --file "$app/sce_module/$name"
done
"$tool" self --inspect --file "$app/eboot.bin"
python3 "$root/tools/write-build-provenance.py" "$app/eboot.bin" "$build/build-provenance.json"

if [[ $format == ffpkg || $format == all ]]; then
    ufs2tool=$(bash "$root/tools/setup-packaging-dependencies.sh" ffpkg)
    rm -f -- "$dist/$title_id.ffpkg"
    "$ufs2tool" makefs -S 4096 -b 20% -t ffs \
        -o version=2,bsize=32768,fsize=4096,minfree=0,softupdates=0,optimization=space \
        "$dist/$title_id.ffpkg" "$app"
    python3 - "$dist/$title_id.ffpkg" <<'PY'
import struct, sys
with open(sys.argv[1], "rb") as stream:
    stream.seek(0x1055c)
    if struct.unpack("<I", stream.read(4))[0] != 0x19540119:
        raise SystemExit("FFPKG is missing the UFS2 superblock magic")
PY
fi
if [[ $format == ffpfsc || $format == all ]]; then
    mkpfs=$(bash "$root/tools/setup-packaging-dependencies.sh" ffpfsc)
    rm -f -- "$dist/$title_id.ffpfsc"
    "$mkpfs" pack folder --no-adjust-output-file-extension \
        --version PS5 --verify "$app" "$dist/$title_id.ffpfsc"
fi

printf 'Build complete.\nApp folder: %s\n' "$app"
[[ $format != ffpkg && $format != all ]] || printf 'FFPKG:     %s\n' "$dist/$title_id.ffpkg"
[[ $format != ffpfsc && $format != all ]] || printf 'FFPFSC:    %s\n' "$dist/$title_id.ffpfsc"
