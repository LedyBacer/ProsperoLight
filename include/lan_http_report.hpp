/*
 * ps5-native-app-boilerplate - ProsperoLight component.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef LAN_HTTP_REPORT_HPP
#define LAN_HTTP_REPORT_HPP

#ifdef __cplusplus
extern "C"
{
#endif

    int prosperolight_logs_enabled(void);
    int prosperolight_logs_set_enabled(int enabled);
    void prosperolight_log_append(const char *path, const char *message);

    // Detailed trace controlled by the single Diagnostic logs setting. The
    // debug enabled/set entry points are compatibility aliases for that setting.
    // A line costs a memory copy; a writer thread pays for the file.
    int prosperolight_debug_enabled(void);
    int prosperolight_debug_set_enabled(int enabled);
    void prosperolight_debug_line(const char *tag, const char *format, ...)
        __attribute__((format(printf, 2, 3)));
    // Names the logs folder and starts the writer; once, when storage is ready.
    void prosperolight_debug_start(const char *logs_folder);
    // Writes what is waiting now: before a stream starts or ends, and at exit.
    void prosperolight_debug_flush(void);

    int lan_http_report_text(const char *message);
    void lan_http_report_set_host(const char *host);

#ifdef __cplusplus
}
#endif

#endif
