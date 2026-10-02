#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Sourced by tools/build.sh. Uses the exact hardware-proven dependency pins.
prepare_pyrowave_build() {
    bash "$root/tools/pyrowave/fetch-deps.sh"
    local deps="$root/.deps/pyrowave"
    export PS5_VULKAN_ROOT="$deps/PS5_Vulkan"
    export PS5_PAYLOAD_SDK="$PS5_VULKAN_ROOT/.deps/native/ps5-payload-sdk"
    export PS5_MESA_FORK="$deps/PS5_Mesa" PS5_PAYLOADSDK_FORK="$deps/PS5_PayloadSDK"
    bash "$PS5_VULKAN_ROOT/tools/setup-native-dependencies.sh"
    bash "$PS5_VULKAN_ROOT/tools/build-radv.sh" release
    local staged="$PS5_VULKAN_ROOT/.deps/work/radv-src" patch
    # Reconstruct only our generated WSI source from the pinned fork before
    # applying overlapping patches, so rebuilds remain idempotent.
    git -C "$deps/PS5_Mesa" show "$(cat "$staged/.revision"):src/vulkan/wsi/wsi_common_videoout.c" > "$staged/src/vulkan/wsi/wsi_common_videoout.c"
    cp "$root/include/ps5_videoout_formats.h" "$staged/src/vulkan/wsi/ps5_videoout_formats.h"
    for patch in "$root/tools/pyrowave/patches/radv/"*.patch; do
        git -C "$staged" apply "$patch"
    done
    local hash stamp
    hash=$(sha256sum "$root/tools/pyrowave/patches/radv/"*.patch "$root/include/ps5_videoout_formats.h" | sha256sum | cut -d' ' -f1)
    stamp="$PS5_VULKAN_ROOT/.deps/native/radv-release/.pyrowave-wsi-patch-hash"
    if [[ ! -f $stamp || $(cat "$stamp") != "$hash" ]]; then
        rm -f "$PS5_VULKAN_ROOT/.deps/native/radv-release/PROVENANCE.txt"
    fi
    bash "$PS5_VULKAN_ROOT/tools/build-radv.sh" release
    printf '%s\n' "$hash" > "$stamp"
    cmake -S "$root/tools/pyrowave" -B "$root/build/pyrowave" -G Ninja \
        -DDEPS_ROOT="$deps" -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE="$root/tools/pyrowave/toolchain.cmake"
    cmake --build "$root/build/pyrowave" --target pyrowave-c -j "${BUILD_JOBS:-4}"
    mapfile -t pyrowave_archives < "$root/build/pyrowave/archives.txt"
    pyrowave_cflags=("-I$deps/pyrowave" "-I$deps/Granite/third_party/volk" \
        "-I$deps/Granite/third_party/khronos/vulkan-headers/include")
    source "$PS5_VULKAN_ROOT/tools/radv-link.sh"
    radv_link_recipe "$PS5_VULKAN_ROOT" "$PS5_PAYLOAD_SDK" \
        "$PS5_VULKAN_ROOT/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a"
    # ProsperoLight already implements these through libSceNet. The SDK
    # platform versions are EAI_FAIL stubs intended for standalone GPU titles.
    local flag
    local filtered_flags=()
    for flag in "${radv_link_flags[@]}"; do
        case "$flag" in
            --defsym=getaddrinfo=*|--defsym=freeaddrinfo=*) ;;
            *) filtered_flags+=("$flag") ;;
        esac
    done
    radv_link_flags=("${filtered_flags[@]}")
    # Mesa's generated dispatch tables also reference optional, unimplemented
    # RADV entry points weakly. Bind only those absent from the entire archive
    # to absolute zero: the common Vulkan implementation remains the fallback.
    # Otherwise LLD can retain them as dynamic imports, which the PS5 module
    # writer correctly refuses because no system module provides RADV symbols.
    local radv_archive="$PS5_VULKAN_ROOT/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a"
    local weak_symbols="$root/build/pyrowave/radv-optional-symbols.txt" symbol
    llvm-nm-18 --format=posix "$radv_archive" | awk '
        $1 ~ /^radv_/ && ($2 == "w" || $2 == "v") { weak[$1] = 1 }
        $1 ~ /^radv_/ && $2 != "U" && $2 != "w" && $2 != "v" { defined[$1] = 1 }
        END { for (name in weak) if (!(name in defined)) print name }
    ' | LC_ALL=C sort > "$weak_symbols"
    while IFS= read -r symbol; do
        radv_link_flags+=("--defsym=$symbol=0")
    done < "$weak_symbols"
    link_script=("${radv_linker_script[@]}")
    radv_link_flags+=(--wrap=sceVideoOutOpen --wrap=sceVideoOutSubmitFlip)
}
