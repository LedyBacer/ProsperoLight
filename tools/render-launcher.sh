#!/usr/bin/env bash
# ps5-native-app-boilerplate - ProsperoLight launcher on the PC.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds the launcher's model and view for the PC, runs them against a pretend
# Sunshine network through Mesa's software renderer, checks their behaviour and
# writes a PNG of every state.
#
# usage: tools/render-launcher.sh [output dir] [width height]

set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cxx=$(command -v "${HOST_CXX:-clang++}")
build="$root/build/launcher-host"
kit="$root/third_party/ps5-homebrew-ui"
mkdir -p "$build/obj"

sources=("$root"/tools/launcher-host/*.cpp "$root/src/launcher/launcher_model.cpp"
    "$root/src/launcher/launcher_view.cpp" "$root/src/moonlight_config.cpp")
while IFS= read -r -d '' source; do
    sources+=("$source")
done < <(find "$kit/gfx" "$kit/ui" "$kit/core" "$kit/audio" -type f -name '*.cpp' -print0 | sort -z)

flags="-std=c++20 -O2 -Wall -Wextra -DGL_GLEXT_PROTOTYPES=1 -I$root/src -I$root/include -I$kit -I$root/tools/launcher-host -I$root/third_party/stb"
{
    printf 'all: %s\n' "$build/launcher_host"
    objects=()
    for source in "${sources[@]}"; do
        relative=${source#"$root/"}
        object="$build/obj/${relative//\//_}.o"
        objects+=("$object")
        printf '%s: %s\n\t@%s %s -MD -MF %s.d -c %s -o %s\n' "$object" "$source" "$cxx" "$flags" \
            "$object" "$source" "$object"
    done
    printf '%s: %s\n\t@%s %s -lEGL -lGL -lm -lpthread -o %s\n' "$build/launcher_host" \
        "${objects[*]}" "$cxx" "${objects[*]}" "$build/launcher_host"
    # A header that moved or went away must not stop the build: its users are rebuilt.
    printf '%%.h %%.hpp:\n\t@:\n'
    printf -- '-include %s\n' "$build"/obj/*.d
} > "$build/Makefile"
if ! make -s -j"$(nproc)" -f "$build/Makefile" >"$build/build.log" 2>&1; then
    { grep -E 'error|Error' -A6 "$build/build.log" || tail -20 "$build/build.log"; } | head -80 >&2
    exit 1
fi
grep -E 'warning' -A4 "$build/build.log" | head -40 >&2 || true

output=${1:-"$root/build/launcher-pictures"}
rm -rf -- "$output"
mkdir -p "$output"
EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe \
    "$build/launcher_host" "$root/assets" "$output" "${@:2}"
