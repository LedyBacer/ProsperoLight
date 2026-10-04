#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
patch="$root/tools/pyrowave/patches/moonlight/0001-vibepollo-independent-record-protocol.patch"
common="$root/third_party/moonlight-common-c"
if git -C "$common" apply --check "$patch" 2>/dev/null; then
    git -C "$common" apply "$patch"
else
    git -C "$common" apply --reverse --check "$patch"
fi
