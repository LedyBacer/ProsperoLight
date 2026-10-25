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
elif grep -Fq 'int timeoutMs = RTP_QUEUE_DELAY;' "$common/src/VideoStream.c" &&
     grep -Fq 'pollSockets(&pfd, 1, timeoutMs)' "$common/src/VideoStream.c"; then
    : # already applied; downstream changes alter reverse patch context
else
    echo "Video reorder deadline patch does not match pinned Moonlight source" >&2
    exit 1
fi

# Versioned, allocation-free counters for delayed RTP/FEC decisions.
patch="$root/tools/pyrowave/patches/moonlight/0007-video-reorder-telemetry.patch"
if git -C "$common" apply --check "$patch" 2>/dev/null; then
    git -C "$common" apply "$patch"
elif grep -Fq 'LI_VIDEO_REORDER_STATS_VERSION' "$common/src/Limelight.h" &&
     grep -Fq 'static void reorderObserveWait(' "$common/src/VideoStream.c" &&
     grep -Fq 'REORDER_INC(HELD)' "$common/src/VideoStream.c"; then
    : # already applied; downstream recovery patch modifies VideoStream.c
else
    echo "Video reorder metrics patch does not match pinned Moonlight source" >&2
    exit 1
fi

# Debounce duplicate IDR triggers after a genuinely unrecoverable frame.
patch="$root/tools/pyrowave/patches/moonlight/0008-coalesce-idr-recovery.patch"
if git -C "$common" apply --check "$patch" 2>/dev/null; then
    git -C "$common" apply "$patch"
elif grep -Fq 'IDR_REQUEST_COOLDOWN_US' "$common/src/VideoDepacketizer.c" &&
     grep -Fq 'requestIdrCoalesced();' "$common/src/VideoDepacketizer.c"; then
    : # already applied; deferred-IDR follow-up modifies helper's source
else
    echo "Video recovery coalescing patch does not match pinned Moonlight source" >&2
    exit 1
fi

# Keep a throttled recovery request pending until the next receive/poll tick,
# cancelling once a valid IDR/recovery anchor arrives. This is additive to 0008.
patch="$root/tools/pyrowave/patches/moonlight/0009-deferred-idr-recovery.patch"
if git -C "$common" apply --unidiff-zero --check "$patch" 2>/dev/null; then
    git -C "$common" apply --unidiff-zero "$patch"
elif grep -Fq 'void PlVideoRecoveryPoll(void)' "$common/src/VideoDepacketizer.c" &&
     grep -Fq 'PlVideoRecoveryPoll();' "$common/src/VideoStream.c"; then
    : # Already applied; later instrumentation may change reverse context.
else
    git -C "$common" apply --unidiff-zero --check --verbose "$patch" >&2 || true
    echo "Deferred IDR recovery patch does not match pinned Moonlight source" >&2
    exit 1
fi

# Keep sorted future-block staging O(1) in the common append case. Rare true
# out-of-order inserts still use the original bounded ordered-list traversal.
patch="$root/tools/pyrowave/patches/moonlight/0010-video-reorder-tail-fastpath.patch"
if git -C "$common" apply --check "$patch" 2>/dev/null; then
    git -C "$common" apply "$patch"
elif grep -Fq 'static VIDEO_REORDER_HELD* reorderTail;' "$common/src/VideoStream.c" &&
     grep -Fq 'reorderTail->next = entry;' "$common/src/VideoStream.c"; then
    : # Already applied.
else
    echo "Video reorder tail fast path does not match pinned Moonlight source" >&2
    exit 1
fi

# Synchronize the decoder-owner IDR-wait flag and make pending recovery
# cancellation generation-safe across concurrent receive/decode callbacks.
patch="$root/tools/pyrowave/patches/moonlight/0011-recovery-epoch-safety.patch"
if git -C "$common" apply --check "$patch" 2>/dev/null; then
    git -C "$common" apply "$patch"
elif grep -Fq 'static _Atomic uint64_t deferredIdrEpoch;' "$common/src/VideoDepacketizer.c" &&
     grep -Fq 'static _Atomic bool waitingForIdrFrame;' "$common/src/VideoDepacketizer.c"; then
    : # Already applied.
else
    echo "Video recovery epoch patch does not match pinned Moonlight source" >&2
    exit 1
fi

# Distinguish obsolete FEC parity from missing data in reorder diagnostics;
# the packets and existing admission/recovery decisions remain untouched.
patch="$root/tools/pyrowave/patches/moonlight/0012-video-reorder-classification.patch"
if git -C "$common" apply --check "$patch" 2>/dev/null; then
    git -C "$common" apply "$patch"
elif grep -Fq 'LI_VIDEO_REORDER_STATS_VERSION 2' "$common/src/Limelight.h" &&
     grep -Fq 'RV_LATE_PARITY' "$common/src/VideoStream.c"; then
    : # Already applied.
else
    echo "Video reorder telemetry classification patch does not match pinned Moonlight source" >&2
    exit 1
fi

# Reject stale RTP sequence windows and impossible FEC geometry before
# reserving limited reorder memory. The actual parser remains authoritative.
patch="$root/tools/pyrowave/patches/moonlight/0013-reorder-staging-admission.patch"
if git -C "$common" apply --check "$patch" 2>/dev/null; then
    git -C "$common" apply "$patch"
elif grep -Fq 'static bool reorderStagingEligible(' "$common/src/VideoStream.c" &&
     grep -Fq 'reorderStagingEligible(packet->sequenceNumber' "$common/src/VideoStream.c"; then
    : # Already applied.
else
    echo "Video reorder admission patch does not match pinned Moonlight source" >&2
    exit 1
fi
