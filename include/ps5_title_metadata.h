/* ps5-native-app-boilerplate - Title metadata for VideoOut capability checks.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once
#include "ps5_videoout_formats.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline int ps5_title_declares_hfr(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file)
    {
        fprintf(stderr, "wsi/videoout: metadata unreadable path=%s errno=%d\n", path, errno);
        return 0;
    }
    char text[16384];
    const size_t length = fread(text, 1, sizeof(text) - 1, file);
    const int invalid = ferror(file) || !feof(file);
    fclose(file);
    text[length] = '\0';
    const char *field = strstr(text, "\"attribute3\"");
    const char *value = field ? strchr(field, ':') : NULL;
    if (invalid || !value)
    {
        fprintf(stderr, "wsi/videoout: metadata invalid path=%s\n", path);
        return 0;
    }
    ++value;
    while (*value == ' ' || *value == '\t' || *value == '\r' || *value == '\n')
        ++value;
    char *end;
    errno = 0;
    const unsigned long attribute = strtoul(value, &end, 10);
    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')
        ++end;
    if (*value < '0' || *value > '9' || errno || attribute > UINT32_MAX ||
        (*end != ',' && *end != '}'))
    {
        fprintf(stderr, "wsi/videoout: metadata invalid attribute3 path=%s\n", path);
        return 0;
    }
    const int declared = ps5_videoout_declares_hfr(attribute);
    fprintf(stderr, "wsi/videoout: title path=%s attribute3=%#lx hfr=%d vrr_declared=%d\n", path,
            attribute, declared, (attribute & PS5_VIDEOOUT_ATTRIBUTE_VRR_MASK) != 0);
    return declared;
}
