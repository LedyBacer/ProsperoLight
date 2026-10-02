#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
source "$root/tools/pyrowave/pins.sh"
mode=${1:-all}
[[ $mode == all || $mode == host ]] || { echo 'usage: fetch-deps.sh [all|host]' >&2; exit 2; }
mkdir -p "$root/.deps/pyrowave"
fetch() {
    local name=$1 url=$2 revision=$3 directory="$root/.deps/pyrowave/$1"
    if [[ ! -d $directory/.git ]]; then
        [[ ! -e $directory ]] || { echo "refusing to overwrite $directory" >&2; exit 2; }
        git init -q "$directory"
        git -C "$directory" remote add origin "$url"
        git -C "$directory" fetch --depth 1 origin "$revision"
        git -C "$directory" checkout --detach -q FETCH_HEAD
    fi
    [[ $(git -C "$directory" rev-parse HEAD) == "$revision" ]] || {
        echo "wrong revision in $directory; move the checkout aside and rerun" >&2; exit 2;
    }
}
fetch pyrowave https://github.com/Themaister/pyrowave.git "$PYROWAVE_REV"
fetch Granite https://github.com/Themaister/Granite.git "$GRANITE_REV"
git -C "$root/.deps/pyrowave/Granite" submodule update --init third_party/volk third_party/khronos/vulkan-headers
[[ $(git -C "$root/.deps/pyrowave/Granite/third_party/volk" rev-parse HEAD) == "$VOLK_REV" ]]
[[ $(git -C "$root/.deps/pyrowave/Granite/third_party/khronos/vulkan-headers" rev-parse HEAD) == "$HEADERS_REV" ]]
for patch in "$root/tools/pyrowave/patches/"*.patch; do
    if git -C "$root/.deps/pyrowave/pyrowave" apply --check "$patch" 2>/dev/null; then
        git -C "$root/.deps/pyrowave/pyrowave" apply "$patch"
    else
        git -C "$root/.deps/pyrowave/pyrowave" apply --reverse --check "$patch" || {
            echo "patch does not match pinned tree: $patch" >&2; exit 2;
        }
    fi
done
if [[ $mode == all ]]; then
    fetch PS5_Vulkan https://github.com/mihawk-99/PS5_Vulkan.git "$PS5_VULKAN_REV"
    fetch PS5_Mesa https://github.com/mihawk-99/PS5_Mesa.git "$PS5_MESA_REV"
    fetch PS5_PayloadSDK https://github.com/mihawk-99/PS5_PayloadSDK.git "$PS5_SDK_REV"
fi
