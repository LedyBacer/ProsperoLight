/*
 * ps5-native-app-boilerplate / ProsperoLight - Thread placement checks, mock cpuset.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* Built with PS5_THREAD_PLACEMENT_HOST_TEST; the kernel calls are replaced here. */
#include "../platform/ps5/ps5_thread_placement.c"

#include <assert.h>
#include <stdio.h>

static uint64_t thread_mask = 0x1fff;
static int fail_set, ignore_set;

int cpuset_getaffinity(int level, int which, int64_t id, size_t setsize, cpuset_t *mask)
{
    assert(level == CPU_LEVEL_WHICH && which == CPU_WHICH_TID && id == -1);
    if (setsize != 8) /* The console rejects sizeof(cpuset_t). */
    {
        errno = ERANGE;
        return -1;
    }
    memcpy(mask, &thread_mask, sizeof(thread_mask));
    return 0;
}

int cpuset_setaffinity(int level, int which, int64_t id, size_t setsize, const cpuset_t *mask)
{
    assert(level == CPU_LEVEL_WHICH && which == CPU_WHICH_TID && id == -1);
    if (setsize != 8)
    {
        errno = ERANGE;
        return -1;
    }
    if (fail_set)
    {
        errno = EPERM;
        return -1;
    }
    if (!ignore_set)
        memcpy(&thread_mask, mask, sizeof(thread_mask));
    return 0;
}

int main(void)
{
    const ps5_thread_placement_t placement = {0x400, 0x1800, 0x1800};
    ps5_thread_placement_stats_t stats;
    uint64_t mask = 0;

    assert(ps5_thread_affinity_get(NULL) == EINVAL);
    assert(ps5_thread_affinity_get(&mask) == 0 && mask == 0x1fff);
    assert(ps5_thread_affinity_set(0) == EINVAL && thread_mask == 0x1fff);
    assert(ps5_thread_affinity_set(0x1000) == 0 && thread_mask == 0x1000);
    fail_set = 1;
    assert(ps5_thread_affinity_set(0x800) == EPERM && thread_mask == 0x1000);
    fail_set = 0;
    /* A call that succeeds without moving the thread is not a placement. */
    ignore_set = 1;
    assert(ps5_thread_affinity_set(0x800) == ERANGE);
    ignore_set = 0;

    /* Not configured: every thread keeps the mask it inherited. */
    thread_mask = 0x1fff;
    ps5_thread_placement_apply_name("VideoRecv");
    stats = ps5_thread_placement_stats();
    assert(thread_mask == 0x1fff && stats.applied == 0 && stats.failed == 0);

    ps5_thread_placement_configure(&placement);
    ps5_thread_placement_apply_name("VideoRecv");
    assert(thread_mask == 0x400);
    ps5_thread_placement_apply_name("AudioDec");
    assert(thread_mask == 0x1800);
    thread_mask = 0x1fff;
    ps5_thread_placement_apply_name("ControlRecv");
    assert(thread_mask == 0x1800);
    thread_mask = 0x1fff;
    ps5_thread_placement_apply_name(NULL);
    assert(thread_mask == 0x1800);
    stats = ps5_thread_placement_stats();
    assert(stats.applied == 4 && stats.failed == 0 && stats.receive_verified == 0x400);
    fail_set = 1;
    ps5_thread_placement_apply_name("VideoPing");
    fail_set = 0;
    stats = ps5_thread_placement_stats();
    assert(stats.applied == 4 && stats.failed == 1);

    /* A new stream starts from clean counters; clearing stops all placement. */
    ps5_thread_placement_configure(&placement);
    stats = ps5_thread_placement_stats();
    assert(stats.applied == 0 && stats.failed == 0 && stats.receive_verified == 0);
    ps5_thread_placement_clear();
    thread_mask = 0x1fff;
    ps5_thread_placement_apply_name("VideoRecv");
    assert(thread_mask == 0x1fff);
    puts("Thread placement by name / verified affinity / failure accounting PASS (mock cpuset)");
    return 0;
}
