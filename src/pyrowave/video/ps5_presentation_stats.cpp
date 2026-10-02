// SPDX-License-Identifier: MIT
#include "ps5_presentation_stats.hpp"
#include "../common.hpp"
#include "native_agc_present.hpp"
#include <atomic>
#ifdef __PROSPERO__
extern "C" int __real_sceVideoOutOpen(int32_t, int32_t, int32_t, const void *);
extern "C" int __real_sceVideoOutSubmitFlip(int32_t, int32_t, uint32_t, int64_t);
extern "C" int sceVideoOutGetFlipStatus(int32_t, void *);
extern "C" int sceVideoOutGetVblankStatus(int32_t, void *);
extern "C" int sceKernelUsleep(uint32_t);
static std::atomic<int> video_handle{-1};
static std::atomic<uint64_t> flips{0}, errors{0};
extern "C" int __wrap_sceVideoOutOpen(int32_t user, int32_t bus, int32_t index, const void *p)
{
    int handle = __real_sceVideoOutOpen(user, bus, index, p);
    if (handle >= 0)
    {
        video_handle.store(handle);
        flips.store(0);
        errors.store(0);
    }
    return handle;
}
extern "C" int __wrap_sceVideoOutSubmitFlip(int32_t handle, int32_t buffer, uint32_t mode,
                                            int64_t argument)
{
    int result = __real_sceVideoOutSubmitFlip(handle, buffer, mode, argument);
    if (handle == video_handle.load())
    {
        if (result == 0)
            flips.fetch_add(1);
        else
            errors.fetch_add(1);
    }
    return result;
}
#endif
PresentationStats ps5_presentation_stats()
{
    PresentationStats result;
#ifdef __PROSPERO__
    int handle = video_handle.load();
    uint64_t flip[16] = {}, vblank[16] = {};
    result.submitted = flips.load();
    result.failed = errors.load();
    if (handle >= 0 && sceVideoOutGetFlipStatus(handle, flip) == 0)
    {
        result.shown = flip[3];
        result.flip_count = flip[0];
        result.available = true;
    }
    // The public export has an opaque status ABI. Only word 0 is consumed,
    // and the caller verifies its count rate against CLOCK_MONOTONIC first.
    if (handle >= 0 && sceVideoOutGetVblankStatus(handle, vblank) == 0)
    {
        result.vblanks = vblank[0];
        result.vblank_available = true;
    }
#endif
    return result;
}
void ps5_drain_presents(uint64_t expected)
{
#ifdef __PROSPERO__
    for (unsigned i = 0; i < 10000; ++i)
    {
        auto s = ps5_presentation_stats();
        if (s.available && s.shown >= expected)
            return;
        sceKernelUsleep(1000);
    }
    fail("VideoOut did not show queued frames within 10s");
#endif
}

int ps5_hdr_output_active()
{
#ifdef __PROSPERO__
    return native_videoout_hdr_active(video_handle.load());
#else
    return -1;
#endif
}
