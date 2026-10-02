#!/usr/bin/env bash
# ps5-native-app-boilerplate - ProsperoLight launcher fonts.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Rebuilds the distance-field fonts in assets/fonts from the typefaces in
# third_party/fonts. Each line of the table is: source file, output name,
# pixel size, distance-field range.

set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cxx=$(command -v "${HOST_CXX:-clang++}")
mkdir -p "$root/build/host" "$root/assets/fonts"
"$cxx" -std=c++20 -O2 -w "$root/tools/font-baker/bake_font.cpp" -o "$root/build/host/bake_font"
while read -r source output size range; do
    [[ -n $source ]] || continue
    "$root/build/host/bake_font" "$root/third_party/fonts/$source" \
        "$root/assets/fonts/$output.huifont" "$size" "$range" 2048
done <<'FONTS'
Inter-Regular.ttf inter-regular 56 8
Inter-SemiBold.ttf inter-semibold 56 8
Montserrat-Medium.ttf montserrat-medium 56 8
DejaVuSansMono.ttf dejavu-sans-mono 52 8
FONTS
cp "$root"/third_party/fonts/*-LICENSE.txt "$root/assets/fonts/"
