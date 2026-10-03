/*
 * ps5-native-app-boilerplate - ProsperoLight component.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* LAN-only development telemetry. Disabled in normal builds. */

#include "lan_http_report.hpp"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <mutex>
#include <string.h>
#include <sys/stat.h>

namespace
{
std::mutex log_mutex;
bool logs_loaded = false;
bool logs_enabled = true;
constexpr size_t log_limit = 1024 * 1024;
constexpr const char *log_settings = "/download0/prosperolight-logging.bin";
void load_logs()
{
    if (logs_loaded)
        return;
    logs_loaded = true;
    if (FILE *file = fopen(log_settings, "rb"))
    {
        unsigned char data[5]{};
        if (fread(data, 1, sizeof(data), file) == sizeof(data) && memcmp(data, "PLL\1", 4) == 0 &&
            data[4] <= 1)
            logs_enabled = data[4] != 0;
        fclose(file);
    }
}
} // namespace

int prosperolight_logs_enabled(void)
{
    std::lock_guard<std::mutex> guard(log_mutex);
    load_logs();
    return logs_enabled;
}

int prosperolight_logs_set_enabled(int enabled)
{
    std::lock_guard<std::mutex> guard(log_mutex);
    load_logs();
    const char *temporary = "/download0/prosperolight-logging.tmp";
    FILE *file = fopen(temporary, "wb");
    if (!file)
        return 0;
    const unsigned char data[] = {'P', 'L', 'L', 1, static_cast<unsigned char>(enabled != 0)};
    const bool written = fwrite(data, 1, sizeof(data), file) == sizeof(data);
    const int closed = fclose(file);
    if (!written || closed || rename(temporary, log_settings) != 0)
    {
        remove(temporary);
        return 0;
    }
    logs_enabled = enabled != 0;
    return 1;
}

void prosperolight_log_append(const char *path, const char *message)
{
    if (!path || !message)
        return;
    std::lock_guard<std::mutex> guard(log_mutex);
    load_logs();
    if (!logs_enabled)
        return;
    const size_t length = strnlen(message, log_limit - 1);
    struct stat info
    {
    };
    if (stat(path, &info) == 0 && static_cast<uint64_t>(info.st_size) + length + 1 > log_limit)
    {
        char previous[256];
        if (snprintf(previous, sizeof(previous), "%s.previous", path) >=
            static_cast<int>(sizeof(previous)))
            return;
        // Oversized logs from earlier builds must not become an unbounded backup.
        if (info.st_size > static_cast<off_t>(log_limit))
        {
            if (remove(path) != 0)
                return;
        }
        else
        {
            if (rename(path, previous) != 0)
                return;
        }
    }
    if (FILE *file = fopen(path, "a"))
    {
        fwrite(message, 1, length, file);
        fputc('\n', file);
        fclose(file);
    }
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
    if (!prosperolight_logs_enabled())
        return 0;
    prosperolight_log_append("/download0/prosperolight-session.log", message);
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
