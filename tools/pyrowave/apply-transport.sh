#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
patch="$root/tools/pyrowave/patches/moonlight/0001-vibepollo-independent-record-protocol.patch"
common="$root/third_party/moonlight-common-c"
if git -C "$common" apply --check "$patch" 2>/dev/null; then
    git -C "$common" apply "$patch"
elif grep -Fq 'PYROWAVE_FEATURE_RECORD_FRAMING' "$common/src/Limelight.h"; then
    : # already applied; controller composition can make reverse --check fail
else
    git -C "$common" apply --reverse --check "$patch"
fi

# Allocation-free session delivery accounting; optional public getter, no ABI edits.
patch="$root/tools/pyrowave/patches/moonlight/0002-video-delivery-counters.patch"
if git -C "$common" apply --check "$patch" 2>/dev/null; then
    git -C "$common" apply "$patch"
elif grep -Fq 'LI_VIDEO_DELIVERY_STATS_VERSION' "$common/src/Limelight.h"; then
    git -C "$common" apply --reverse --check "$patch"
else
    git -C "$common" apply --check "$patch"
fi
