/*
 * ps5-native-app-boilerplate / ProsperoLight - nanors CPU dispatch.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <stdint.h>
#include <string.h>
#include <cpuid.h>

#ifndef PROSPEROLIGHT_FEC_SIMD
#define PROSPEROLIGHT_FEC_SIMD 0
#endif

static inline int ps5_fec_feature_allowed(const char *name, uint32_t leaf1_ecx,
                                         uint32_t leaf7_ebx, uint64_t xcr0)
{
    if (strcmp(name, "ssse3") == 0)
        return (leaf1_ecx & (1u << 9)) != 0;
    if (strcmp(name, "avx2") == 0)
    {
        const uint32_t avx_state = (1u << 26) | (1u << 27) | (1u << 28);
        return (leaf1_ecx & avx_state) == avx_state && (xcr0 & 6u) == 6u &&
               (leaf7_ebx & (1u << 5)) != 0;
    }
    // No AVX-512/GFNI experiment on PS5, regardless of build-host capabilities.
    return 0;
}

static inline int ps5_fec_cpu_detect(const char *name)
{
    unsigned eax = 0, ebx = 0, ecx = 0, edx = 0;
    if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx))
        return 0;
    const uint32_t leaf1_ecx = ecx;
    uint64_t xcr0 = 0;
    if ((ecx & ((1u << 26) | (1u << 27))) == ((1u << 26) | (1u << 27)))
    {
        unsigned low, high;
        __asm__ volatile("xgetbv" : "=a"(low), "=d"(high) : "c"(0));
        xcr0 = ((uint64_t)high << 32) | low;
    }
    ebx = 0;
    (void)__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx);
    return ps5_fec_feature_allowed(name, leaf1_ecx, ebx, xcr0);
}

static inline int ps5_fec_cpu_supports(const char *name)
{
    /* nanors queries several times per recovered frame; CPUID can trap. */
    static int avx2 = -1, ssse3 = -1;
    int *cached;
    int value;

    if (!PROSPEROLIGHT_FEC_SIMD ||
        (strcmp(name, "ssse3") != 0 && strcmp(name, "avx2") != 0))
        return 0;
    cached = strcmp(name, "avx2") == 0 ? &avx2 : &ssse3;
    value = __atomic_load_n(cached, __ATOMIC_RELAXED);
    if (value < 0)
    {
        value = ps5_fec_cpu_detect(name);
        __atomic_store_n(cached, value, __ATOMIC_RELAXED);
    }
    return value;
}

#define __builtin_cpu_init() ((void)0)
#define __builtin_cpu_supports(feature) ps5_fec_cpu_supports(feature)
