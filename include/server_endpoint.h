/*
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* An optional suffix specifies the server HTTP port, not its Web UI port. */
static inline int server_endpoint_parse(const char *input, char *host, size_t capacity,
                                        uint16_t fallback_port, uint16_t *port)
{
    if (!input || !host || !port || !capacity || !input[0])
        return 0;
    const char *colon = strchr(input, ':');
    const size_t length = colon ? (size_t)(colon - input) : strlen(input);
    if (!length || length >= capacity)
        return 0;
    for (size_t i = 0; i < length; ++i)
        if ((unsigned char)input[i] <= 32 || input[i] == '/' || input[i] == '[' || input[i] == ']')
            return 0;
    uint32_t value = fallback_port ? fallback_port : 47989;
    if (colon)
    {
        value = 0;
        if (!colon[1])
            return 0;
        for (const char *digit = colon + 1; *digit; ++digit)
        {
            if (*digit < '0' || *digit > '9')
                return 0;
            value = value * 10 + (unsigned)(*digit - '0');
            if (value > 65535)
                return 0;
        }
        if (!value)
            return 0;
    }
    memcpy(host, input, length);
    host[length] = 0;
    *port = (uint16_t)value;
    return 1;
}
