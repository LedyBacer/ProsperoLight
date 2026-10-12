/* ps5-native-app-boilerplate - bounded PR0 launch request.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once
#include <stdint.h>
#include <string.h>
#define REQUEST_BYTES 256
/* Two NUL-terminated fields, 128 bytes each: unique request ID, saved PC.
 * App and title are fixed; no arbitrary executable or path can be supplied. */
static int valid_field(const char *p, int identifier)
{
    size_t n = strnlen(p, 128);
    if (!n || n > 127)
        return 0;
    for (size_t i = 0; i < n; ++i)
    {
        unsigned char c = (unsigned char)p[i];
        if (identifier ? !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                           (c >= '0' && c <= '9') || c == '-' || c == '_')
                       : (c < 32 || c > 126 || (c == '"' || c == '\\')))
            return 0;
    }
    for (size_t i = n; i < 128; ++i)
        if (p[i])
            return 0;
    return 1;
}
static int valid_request(const char *p)
{
    return valid_field(p, 1) && valid_field(p + 128, 0);
}
