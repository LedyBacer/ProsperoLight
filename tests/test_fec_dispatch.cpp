/*
 * ps5-native-app-boilerplate / ProsperoLight - Exact nanors recovery checks.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <array>
#include <algorithm>
#include <chrono>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

#include "rs.h"
#include "ps5_fec_cpu.h"

int main()
{
    constexpr uint32_t features = (1u << 9) | (1u << 26) | (1u << 27) | (1u << 28);
    assert(ps5_fec_feature_allowed("ssse3", features, 0, 0));
    assert(ps5_fec_feature_allowed("avx2", features, 1u << 5, 6));
    assert(!ps5_fec_feature_allowed("avx2", features, 1u << 5, 2));
    assert(!ps5_fec_feature_allowed("avx2", features & ~(1u << 27), 1u << 5, 6));
    assert(!ps5_fec_feature_allowed("avx2", features, 0, 6));
    assert(!ps5_fec_feature_allowed("avx512f", UINT32_MAX, UINT32_MAX, UINT64_MAX));
    assert(!ps5_fec_feature_allowed("gfni", UINT32_MAX, UINT32_MAX, UINT64_MAX));
    uint64_t hash = 14695981039346656037ULL;
    reed_solomon_init();
    for (int bytes : {1, 63, 64, 1392, 1440, 4096})
    {
        auto *rs = reed_solomon_new(8, 4);
        assert(rs);
        const int padded = reed_solomon_padded_size(bytes);
        std::array<uint8_t *, 12> shards{};
        for (auto &shard : shards)
        {
            shard = static_cast<uint8_t *>(reed_solomon_aligned_alloc(padded));
            assert(shard);
            memset(shard, 0, padded);
        }
        for (int s = 0; s < 8; ++s)
            for (int b = 0; b < bytes; ++b)
                shards[s][b] = static_cast<uint8_t>(s * 71 + b * 13 + (b >> 3));
        assert(reed_solomon_encode(rs, shards.data(), 12, bytes) == 0);
        std::vector<std::vector<uint8_t>> original;
        for (auto *shard : shards)
        {
            original.emplace_back(shard, shard + bytes);
            for (int b = 0; b < bytes; ++b)
                hash = (hash ^ shard[b]) * 1099511628211ULL;
        }
        // Lose up to all four parity-budget shards, including mixed data/parity.
        for (unsigned losses = 1; losses <= 4; ++losses)
        {
            std::array<uint8_t, 12> marks{};
            for (unsigned s = 0; s < shards.size(); ++s)
                memcpy(shards[s], original[s].data(), bytes);
            for (unsigned s = 0; s < losses; ++s)
            {
                const unsigned missing = s * 3u;
                marks[missing] = 1;
                memset(shards[missing], 0, bytes);
            }
            assert(reed_solomon_decode(rs, shards.data(), marks.data(), 12, bytes) == 0);
            for (unsigned s = 0; s < 8; ++s)
                assert(memcmp(shards[s], original[s].data(), bytes) == 0);
            if (bytes == 1392 && losses == 4)
            {
                std::array<int64_t, 7> timings{};
                for (auto &time : timings)
                {
                    const auto start = std::chrono::steady_clock::now();
                    for (unsigned iteration = 0; iteration < 2000; ++iteration)
                    {
                        for (unsigned s = 0; s < losses; ++s)
                            memset(shards[s * 3u], 0, bytes);
                        assert(reed_solomon_decode(rs, shards.data(), marks.data(), 12, bytes) ==
                               0);
                    }
                    time = std::chrono::duration_cast<std::chrono::nanoseconds>(
                               std::chrono::steady_clock::now() - start)
                               .count() /
                           2000;
                    for (unsigned s = 0; s < 8; ++s)
                        assert(memcmp(shards[s], original[s].data(), bytes) == 0);
                }
                std::sort(timings.begin(), timings.end());
                printf("host_recovery_median_ns=%lld simd=%d path=%s\n",
                       static_cast<long long>(timings[3]), PROSPEROLIGHT_FEC_SIMD,
                       ps5_fec_cpu_supports("avx2")    ? "avx2"
                       : ps5_fec_cpu_supports("ssse3") ? "ssse3"
                                                       : "scalar");
            }
        }
        printf("bytes=%d alignment=%zu recovery=PASS\n", bytes, rs->align_size);
        for (auto *shard : shards)
            reed_solomon_free(shard);
        reed_solomon_release(rs);
    }
    printf("parity_hash=%016llx\n", static_cast<unsigned long long>(hash));
}
