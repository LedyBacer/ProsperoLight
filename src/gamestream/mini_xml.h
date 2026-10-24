/*
 * ps5-native-app-boilerplate - ProsperoLight component.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct app_entry
    {
        int id;
        int hdr_supported;
        int app_collector_game;
        char *name;
        struct app_entry *next;
    } app_entry_t;

    int xml_status(const char *data, size_t len);
    // Read and decode an attribute from the XML root element into a bounded buffer.
    int xml_root_attribute(const char *data, size_t len, const char *attribute, char *output,
                           size_t output_size);
    int xml_search(const char *data, size_t len, const char *node, char **result);
    int xml_applist(const char *data, size_t len, app_entry_t **list);
    void xml_applist_free(app_entry_t *list);

#ifdef __cplusplus
}
#endif
