/*
 * ps5-native-app-boilerplate / ProsperoLight - Stream thread CPU placement.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ps5_thread_placement.h"

#include <errno.h>
#include <stdatomic.h>
#include <stddef.h>
#include <string.h>

#ifdef PS5_THREAD_PLACEMENT_HOST_TEST
typedef struct
{
    uint64_t bits[4];
} cpuset_t;
#define CPU_LEVEL_WHICH 3
#define CPU_WHICH_TID 1
int cpuset_getaffinity(int level, int which, int64_t id, size_t setsize, cpuset_t *mask);
int cpuset_setaffinity(int level, int which, int64_t id, size_t setsize, const cpuset_t *mask);
#else
#include <sys/param.h>
#include <sys/cpuset.h>
#endif

/*
 * The console accepts exactly eight bytes; sizeof(cpuset_t) fails with ERANGE.
 * Always read the mask back: a successful call is not proof of placement.
 */
#define PS5_CPUSET_BYTES 8u

static _Atomic uint64_t receive_mask;
static _Atomic uint64_t audio_mask;
static _Atomic uint64_t other_mask;
static _Atomic uint32_t applied_count;
static _Atomic uint32_t failed_count;
static _Atomic uint64_t receive_verified;

int ps5_thread_affinity_get(uint64_t *mask)
{
    cpuset_t set;
    uint64_t value = 0;

    if (!mask)
        return EINVAL;
    memset(&set, 0, sizeof(set));
    errno = 0;
    if (cpuset_getaffinity(CPU_LEVEL_WHICH, CPU_WHICH_TID, -1, PS5_CPUSET_BYTES, &set) != 0)
        return errno ? errno : EIO;
    memcpy(&value, &set, sizeof(value));
    *mask = value;
    return 0;
}

int ps5_thread_affinity_set(uint64_t mask)
{
    cpuset_t set;
    uint64_t verified = 0;
    int result;

    if (!mask)
        return EINVAL;
    memset(&set, 0, sizeof(set));
    memcpy(&set, &mask, sizeof(mask));
    errno = 0;
    if (cpuset_setaffinity(CPU_LEVEL_WHICH, CPU_WHICH_TID, -1, PS5_CPUSET_BYTES, &set) != 0)
        return errno ? errno : EIO;
    result = ps5_thread_affinity_get(&verified);
    if (result)
        return result;
    return verified == mask ? 0 : ERANGE;
}

void ps5_thread_placement_configure(const ps5_thread_placement_t *placement)
{
    atomic_store(&receive_verified, 0);
    atomic_store(&applied_count, 0);
    atomic_store(&failed_count, 0);
    atomic_store(&receive_mask, placement ? placement->receive : 0);
    atomic_store(&audio_mask, placement ? placement->audio : 0);
    atomic_store(&other_mask, placement ? placement->other : 0);
}

void ps5_thread_placement_clear(void)
{
    ps5_thread_placement_configure(NULL);
}

void ps5_thread_placement_apply_name(const char *name)
{
    const int receive = name && strcmp(name, "VideoRecv") == 0;
    uint64_t mask = 0;

    if (receive)
        mask = atomic_load(&receive_mask);
    else if (name && strncmp(name, "Audio", 5) == 0)
        mask = atomic_load(&audio_mask);
    if (!mask)
        mask = atomic_load(&other_mask);
    if (!mask)
        return; /* Not configured: keep the inherited placement. */
    if (ps5_thread_affinity_set(mask) != 0)
    {
        atomic_fetch_add(&failed_count, 1);
        return;
    }
    atomic_fetch_add(&applied_count, 1);
    if (receive)
        atomic_store(&receive_verified, mask);
}

ps5_thread_placement_stats_t ps5_thread_placement_stats(void)
{
    ps5_thread_placement_stats_t stats;

    stats.applied = atomic_load(&applied_count);
    stats.failed = atomic_load(&failed_count);
    stats.receive_verified = atomic_load(&receive_verified);
    return stats;
}
