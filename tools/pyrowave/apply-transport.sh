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
elif grep -Fq 'LI_VIDEO_DELIVERY_STATS_VERSION' "$common/src/Limelight.h" &&
     grep -Fq 'VD_ADD(DEQUEUED_AUS' "$common/src/VideoDepacketizer.c" &&
     grep -Fq 'notifyVideoDatagramReceived' "$common/src/VideoStream.c"; then
    : # Already applied; downstream dependency patches modify reverse-apply context.
else
    git -C "$common" apply --check "$patch"
fi

# Preserve FEC recovery bursts without flushing the entire audio packet queue.
patch="$root/tools/pyrowave/patches/moonlight/0003-audio-queue-headroom.patch"
if git -C "$common" apply --check "$patch" 2>/dev/null; then
    git -C "$common" apply "$patch"
elif grep -Fq 'LbqInitializeLinkedBlockingQueue(&packetQueue, 100)' "$common/src/AudioStream.c"; then
    : # already applied
else
    git -C "$common" apply --check "$patch"
fi

# Bounded out-of-order staging in the Moonlight UDP receive thread. The
# original FEC and multi-block recovery rules remain in RtpVideoQueue.c.
patch="$root/tools/pyrowave/patches/moonlight/0004-video-reorder-window.patch"
if git -C "$common" apply --check "$patch" 2>/dev/null; then
    git -C "$common" apply "$patch"
elif grep -Fq 'VIDEO_REORDER_WAIT_US' "$common/src/VideoStream.c" &&
     grep -Fq 'static bool reorderHold' "$common/src/VideoStream.c" &&
     grep -Fq 'reorderRelease(false)' "$common/src/VideoStream.c"; then
    : # Already applied; later telemetry changes invalidate original reverse context.
else
    echo "Video reorder patch does not match pinned Moonlight source" >&2
    exit 1
fi

# Count recoveries and terminal losses using actual RTP/FEC events.
patch="$root/tools/pyrowave/patches/moonlight/0005-video-fec-telemetry.patch"
if git -C "$common" apply --check "$patch" 2>/dev/null; then
    git -C "$common" apply "$patch"
elif git -C "$common" apply --reverse --check "$patch" 2>/dev/null; then
    : # already applied
else
    echo "Video FEC stats patch does not match pinned Moonlight source" >&2
    exit 1
fi

# Keep the 40ms FEC holding window accurate without polling busy on clean LAN.
patch="$root/tools/pyrowave/patches/moonlight/0006-video-reorder-poll.patch"
if git -C "$common" apply --check "$patch" 2>/dev/null; then
    git -C "$common" apply "$patch"
elif git -C "$common" apply --reverse --check "$patch" 2>/dev/null; then
    : # already applied
else
    echo "Video reorder deadline patch does not match pinned Moonlight source" >&2
    exit 1
fi

# Versioned, allocation-free counters for delayed RTP/FEC decisions.
patch="$root/tools/pyrowave/patches/moonlight/0007-video-reorder-telemetry.patch"
if git -C "$common" apply --check "$patch" 2>/dev/null; then
    git -C "$common" apply "$patch"
elif git -C "$common" apply --reverse --check "$patch" 2>/dev/null; then
    : # already applied
else
    echo "Video reorder metrics patch does not match pinned Moonlight source" >&2
    exit 1
fi

# Debounce duplicate IDR triggers after a genuinely unrecoverable frame.
patch="$root/tools/pyrowave/patches/moonlight/0008-coalesce-idr-recovery.patch"
if git -C "$common" apply --check "$patch" 2>/dev/null; then
    git -C "$common" apply "$patch"
elif git -C "$common" apply --reverse --check "$patch" 2>/dev/null; then
    : # already applied
else
    echo "Video recovery coalescing patch does not match pinned Moonlight source" >&2
    exit 1
fi

# Keep a throttled recovery request pending until the next receive/poll tick,
# cancelling once a valid IDR/recovery anchor arrives. This is additive to 0008.
patch="$root/tools/pyrowave/patches/moonlight/0009-deferred-idr-recovery.patch"
if git -C "$common" apply --check "$patch" 2>/dev/null; then
    git -C "$common" apply "$patch"
elif grep -Fq 'void PlVideoRecoveryPoll(void)' "$common/src/VideoDepacketizer.c" &&
     grep -Fq 'PlVideoRecoveryPoll();' "$common/src/VideoStream.c"; then
    : # Already applied; later instrumentation may change reverse context.
else
    git -C "$common" apply --check --verbose "$patch" >&2 || true
    echo "Deferred IDR recovery patch does not match pinned Moonlight source" >&2
    exit 1
fi
