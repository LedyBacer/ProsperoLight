/*
 * ps5-native-app-boilerplate / ProsperoLight - Stream thread CPU placement.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef PS5_THREAD_PLACEMENT_H
#define PS5_THREAD_PLACEMENT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /*
     * A title owns logical CPUs 0-12 (mask 0x1fff) with SMT siblings adjacent:
     * (0,1) (2,3) ... (10,11); CPU 12's sibling belongs to the system. Masks
     * use that logical numbering. Zero means "leave the inherited mask".
     */
    typedef struct ps5_thread_placement
    {
        uint64_t receive;   /* moonlight-common-c VideoRecv */
        uint64_t audio;     /* AudioRecv / AudioDec / AudioPing */
        uint64_t other;     /* every other moonlight-common-c thread */
    } ps5_thread_placement_t;

    typedef struct ps5_thread_placement_stats
    {
        uint32_t applied;
        uint32_t failed;
        uint64_t receive_verified;
    } ps5_thread_placement_stats_t;

    /* Current thread only (CPU_WHICH_TID, -1). Returns 0 or an errno value. */
    int ps5_thread_affinity_get(uint64_t *mask);
    /* Sets and reads back the current thread's mask; 0 only when verified. */
    int ps5_thread_affinity_set(uint64_t mask);

    void ps5_thread_placement_configure(const ps5_thread_placement_t *placement);
    void ps5_thread_placement_clear(void);
    /* Called on the new thread by moonlight-common-c's thread-name hook. */
    void ps5_thread_placement_apply_name(const char *name);
    ps5_thread_placement_stats_t ps5_thread_placement_stats(void);

#ifdef __cplusplus
}
#endif

#endif
