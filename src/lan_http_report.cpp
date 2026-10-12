/*
 * ps5-native-app-boilerplate - ProsperoLight component.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* LAN-only development telemetry. Disabled in normal builds. */

#include "lan_http_report.hpp"
#include "app_storage.hpp"

#include <chrono>
#include <atomic>
#include <fcntl.h>
#include <mutex>
#include <pthread.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <time.h>
#include <unistd.h>

namespace
{
std::mutex log_mutex;
bool logs_loaded = false;
bool logs_enabled = true;
#if PROSPEROLIGHT_TEST_LAB
std::atomic<bool> test_logs_override{false};
#endif
std::string log_settings()
{
    return storage::setting_file("prosperolight-logging.bin");
}
void load_logs()
{
    if (logs_loaded)
        return;
    logs_loaded = true;
    if (FILE *file = fopen(log_settings().c_str(), "rb"))
    {
        unsigned char data[5]{};
        if (fread(data, 1, sizeof(data), file) == sizeof(data) && memcmp(data, "PLL\1", 4) == 0 &&
            data[4] <= 1)
            logs_enabled = data[4] != 0;
        fclose(file);
    }
}
} // namespace

void prosperolight_test_logs_override(int enabled)
{
#if PROSPEROLIGHT_TEST_LAB
    test_logs_override.store(enabled != 0);
#else
    (void)enabled;
#endif
}

int prosperolight_logs_enabled(void)
{
#if PROSPEROLIGHT_TEST_LAB
    if (test_logs_override.load())
        return 1;
#endif
    std::lock_guard<std::mutex> guard(log_mutex);
    load_logs();
    return logs_enabled;
}

int prosperolight_logs_set_enabled(int enabled)
{
    std::lock_guard<std::mutex> guard(log_mutex);
    load_logs();
    const auto destination = log_settings();
    const auto temporary = destination + ".tmp";
    FILE *file = fopen(temporary.c_str(), "wb");
    if (!file)
        return 0;
    const unsigned char data[] = {'P', 'L', 'L', 1, static_cast<unsigned char>(enabled != 0)};
    const bool written = fwrite(data, 1, sizeof(data), file) == sizeof(data);
    const int closed = fclose(file);
    if (!written || closed || rename(temporary.c_str(), destination.c_str()) != 0)
    {
        remove(temporary.c_str());
        return 0;
    }
    logs_enabled = enabled != 0;
    return 1;
}

// ---- Debug log ---------------------------------------------------------------

namespace
{
constexpr size_t kDebugPendingLimit = 512u * 1024u;
constexpr long long kDebugFileLimit = 16ll * 1024 * 1024;
const auto debug_origin = std::chrono::steady_clock::now();
std::mutex debug_mutex;
std::string debug_pending; // lines waiting for the writer
unsigned long long debug_dropped = 0;
std::string debug_folder;
int debug_file = -1;
long long debug_written = 0;
bool debug_full = false;

double debug_seconds()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - debug_origin).count();
}

// With debug_mutex held. The first line of a trace says when it began, so the
// seconds on every other line can be placed on a clock.
bool debug_open()
{
    if (debug_file >= 0)
        return true;
    if (debug_folder.empty() || debug_full)
        return false;
    const std::string path = debug_folder + "/debug-trace.txt";
    (void)rename(path.c_str(), (debug_folder + "/debug-trace.prev.txt").c_str());
    debug_file = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, 0666);
    if (debug_file < 0)
        return false;
    char stamp[40] = "unknown";
    const time_t now = time(nullptr);
    struct tm utc
    {
    };
    if (gmtime_r(&now, &utc))
        (void)strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S UTC", &utc);
    char header[160];
    const int length =
        snprintf(header, sizeof(header), "[%9.3f] [trace] ProsperoLight debug log, opened %s\n",
                 debug_seconds(), stamp);
    if (length > 0)
        debug_written += write(debug_file, header, static_cast<size_t>(length));
    return true;
}

// With debug_mutex held.
void debug_write_pending()
{
    if (debug_pending.empty() || !debug_open())
        return;
    if (debug_dropped)
    {
        char note[96];
        snprintf(note, sizeof(note), "[%9.3f] [trace] %llu lines were not kept\n", debug_seconds(),
                 debug_dropped);
        debug_pending.insert(0, note);
        debug_dropped = 0;
    }
    const ssize_t wrote = write(debug_file, debug_pending.data(), debug_pending.size());
    if (wrote > 0)
        debug_written += wrote;
    debug_pending.clear();
    if (debug_written > kDebugFileLimit)
    {
        // A full trace keeps its beginning, where a problem's cause usually is.
        static const char full[] = "[trace] the debug log is full; nothing more is written\n";
        (void)write(debug_file, full, sizeof(full) - 1);
        close(debug_file);
        debug_file = -1;
        debug_full = true;
    }
}

void *debug_writer(void *)
{
    for (;;)
    {
        usleep(250000);
        if (prosperolight_logs_enabled() != 0)
            prosperolight_debug_flush();
    }
    return nullptr;
}
} // namespace

// Compatibility entry points share the single persistent diagnostics setting.
int prosperolight_debug_enabled(void)
{
    return prosperolight_logs_enabled();
}

int prosperolight_debug_set_enabled(int enabled)
{
    return prosperolight_logs_set_enabled(enabled);
}

void prosperolight_debug_line(const char *tag, const char *format, ...)
{
    if (!format || prosperolight_debug_enabled() != 1)
        return;
    char line[1280];
    int used = snprintf(line, sizeof(line), "[%9.3f] [%s] ", debug_seconds(), tag ? tag : "app");
    if (used < 0)
        return;
    va_list arguments;
    va_start(arguments, format);
    const int more =
        vsnprintf(line + used, sizeof(line) - static_cast<size_t>(used) - 1, format, arguments);
    va_end(arguments);
    if (more < 0)
        return;
    // Lines that already carry the console log's "[PL] " mark lose it: the tag says the same.
    if (strncmp(line + used, "[PL] ", 5) == 0)
        memmove(line + used, line + used + 5, strlen(line + used + 5) + 1);
    used = static_cast<int>(strnlen(line, sizeof(line) - 2));
    while (used > 0 && (line[used - 1] == '\n' || line[used - 1] == '\r'))
        --used;
    line[used++] = '\n';
    std::lock_guard<std::mutex> guard(debug_mutex);
    if (debug_full || debug_pending.size() + static_cast<size_t>(used) > kDebugPendingLimit)
    {
        ++debug_dropped;
        return;
    }
    debug_pending.append(line, static_cast<size_t>(used));
}

void prosperolight_debug_flush(void)
{
    std::lock_guard<std::mutex> guard(debug_mutex);
    debug_write_pending();
}

void prosperolight_debug_start(const char *logs_folder)
{
    {
        std::lock_guard<std::mutex> guard(debug_mutex);
        if (!logs_folder || !debug_folder.empty())
            return;
        debug_folder = logs_folder;
    }
    pthread_attr_t attributes;
    pthread_attr_init(&attributes);
    pthread_attr_setstacksize(&attributes, 64u * 1024u);
    pthread_t writer;
    if (pthread_create(&writer, &attributes, debug_writer, nullptr) == 0)
        pthread_detach(writer);
    pthread_attr_destroy(&attributes);
}

void prosperolight_log_append(const char *path, const char *message)
{
    if (!path || !message)
        return;
    // One text destination; CSV/JSON reports remain separate structured captures.
    const char *report = strrchr(path, '/') ? strrchr(path, '/') + 1 : path;
    if (strncmp(report, "prosperolight-", 14) == 0)
        report += 14;
    char tag[24];
    snprintf(tag, sizeof(tag), "%.*s", static_cast<int>(strcspn(report, ".")), report);
    prosperolight_debug_line(tag, "%s", message);
}

#ifndef PROSPEROLIGHT_LAN_TELEMETRY
#define PROSPEROLIGHT_LAN_TELEMETRY 0
#endif

#if PROSPEROLIGHT_LAN_TELEMETRY
#define REPORT_PORT 8767
#define NET_IPV4(a, b, c, d)                                                                       \
    ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))

typedef struct net_sockaddr_in
{
    uint8_t length;
    uint8_t family;
    uint16_t port;
    uint32_t address;
    uint16_t virtual_port;
    uint8_t zero[6];
} net_sockaddr_in_t;

extern "C"
{
    int sceNetSocket(const char *name, int domain, int type, int protocol);
    int sceNetConnect(int socket, const void *address, uint32_t address_length);
    int sceNetSend(int socket, const void *data, size_t length, int flags);
    int sceNetSocketClose(int socket);
}

static uint32_t report_address;
static char report_host[64];

static uint16_t big_endian_u16(uint16_t value)
{
    return (uint16_t)((value << 8) | (value >> 8));
}

static size_t text_length(const char *text)
{
    size_t length = 0;
    while (text[length] != '\0')
        ++length;
    return length;
}

static int send_all(int socket, const void *data, size_t length)
{
    size_t sent = 0;
    while (sent < length)
    {
        int result = sceNetSend(socket, (const uint8_t *)data + sent, length - sent, 0);
        if (result <= 0)
            return -1;
        sent += (size_t)result;
    }
    return 0;
}
#endif

void lan_http_report_set_host(const char *host)
{
#if PROSPEROLIGHT_LAN_TELEMETRY
    unsigned a, b, c, d;
    char extra;

    report_address = 0;
    report_host[0] = 0;
    if (!host || sscanf(host, " %u.%u.%u.%u %c", &a, &b, &c, &d, &extra) != 4 || a > 255 ||
        b > 255 || c > 255 || d > 255)
        return;
    report_address = NET_IPV4(a, b, c, d);
    snprintf(report_host, sizeof(report_host), "%u.%u.%u.%u", a, b, c, d);
#else
    (void)host;
#endif
}

int lan_http_report_text(const char *message)
{
    prosperolight_log_append("prosperolight-session.log", message);
    if (!prosperolight_logs_enabled())
        return 0;
#if PROSPEROLIGHT_LAN_TELEMETRY
    char request[256];
    size_t message_length;
    int request_length;
    int socket;
    net_sockaddr_in_t address;
    int result;

    if (!message)
        return -1;
    fputs(message, stdout);
    fputc('\n', stdout);
    fflush(stdout);
    if (!report_address)
        return -1;
    message_length = text_length(message);
    request_length = snprintf(request, sizeof(request),
                              "POST /moonlight/fs HTTP/1.0\r\n"
                              "Host: %s:%u\r\n"
                              "Content-Type: text/plain\r\n"
                              "Content-Length: %u\r\n"
                              "Connection: close\r\n\r\n",
                              report_host, REPORT_PORT, (unsigned)message_length);
    if (request_length <= 0 || (size_t)request_length >= sizeof(request))
        return -1;

    socket = sceNetSocket("moonlight_telemetry", 2, 1, 6);
    if (socket < 0)
        return -2;
    address = (net_sockaddr_in_t){sizeof(address), 2, big_endian_u16(REPORT_PORT),
                                  report_address,  0, {0}};
    if (sceNetConnect(socket, &address, sizeof(address)) < 0)
    {
        (void)sceNetSocketClose(socket);
        return -3;
    }
    result = send_all(socket, request, (size_t)request_length);
    if (result == 0)
        result = send_all(socket, message, message_length);
    (void)sceNetSocketClose(socket);
    return result == 0 ? 0 : -4;
#else
    (void)message;
    return 0;
#endif
}
