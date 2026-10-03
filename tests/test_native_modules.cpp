/*
 * ProsperoLight - Native module lifetime regression check.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "native_modules.hpp"
#include <cassert>
#include <cstdint>

namespace
{
unsigned loads = 0;
bool storage_initialized = false;
constexpr int failed_keyboard = -1234;
} // namespace

extern "C" int sceSysmoduleLoadModule(std::uint32_t id)
{
    assert(!storage_initialized);
    ++loads;
    return id == prosperolight::native_modules::keyboard ? failed_keyboard : 0;
}

int main()
{
    using namespace prosperolight::native_modules;
    assert(Result(video_decoder) == -1);
    PrepareBeforeStorage();
    assert(loads == 3);
    storage_initialized = true;
    for (unsigned stream = 0; stream < 100; ++stream)
    {
        PrepareBeforeStorage();
        assert(Result(video_decoder) == 0);
        assert(Result(mouse) == 0);
        assert(Result(keyboard) == failed_keyboard);
        assert(Result(0xffff) == -1);
    }
    assert(loads == 3);
}
