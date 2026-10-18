/* ps5-native-app-boilerplate / ProsperoLight - Firmware startup experiment.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "fwcompat.h"
#if FWCOMPAT_DIAGNOSTICS
#include <cstdio>
#include <cstring>
#include <ctime>
#include <unistd.h>
#include <fcntl.h>
#include <mutex>
#include <atomic>
extern "C" int sceKernelDebugOutText(int, const char *);
#ifndef FWCOMPAT_SOURCE_SHA
#define FWCOMPAT_SOURCE_SHA unknown
#endif
#ifndef FWCOMPAT_SDK_ID
#define FWCOMPAT_SDK_ID unknown
#endif
namespace
{
std::mutex lock;
int log_fd = -1;
int receipt_fd = -1;
char early[16384];
size_t early_size = 0;
const char *variant()
{
    static char name[] = FWCOMPAT_STRING(FWCOMPAT_VARIANT_ID);
    static const bool initialized = []
    {
        for (char &c : name)
            if (c == '_')
                c = '-';
        return true;
    }();
    (void)initialized;
    return name;
}
unsigned sequence = 0;
std::atomic<bool> gpu_failed{false};
void write_all(int fd, const char *data, size_t bytes)
{
    while (fd >= 0 && bytes)
    {
        const auto count = write(fd, data, bytes);
        if (count <= 0)
            break;
        data += count;
        bytes -= static_cast<size_t>(count);
    }
}
} // namespace
extern "C" void fwcompat_checkpoint(const char *stage, long long value)
{
    const std::lock_guard<std::mutex> guard(lock);
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    char line[512];
    std::snprintf(line, sizeof(line), "[FWCOMPAT] variant=%s seq=%u ms=%lld stage=%s value=%lld\n",
                  variant(), ++sequence,
                  static_cast<long long>(now.tv_sec) * 1000 + now.tv_nsec / 1000000, stage, value);
    const size_t bytes = std::strlen(line);
    (void)sceKernelDebugOutText(0, line);
    if (log_fd >= 0)
    {
        write_all(log_fd, line, bytes);
        (void)fsync(log_fd);
    }
    else if (early_size + bytes <= sizeof(early))
    {
        std::memcpy(early + early_size, line, bytes);
        early_size += bytes;
    }
    if (receipt_fd >= 0)
    {
        char record[640];
        std::snprintf(record, sizeof(record), "{\"seq\":%u,\"stage\":\"%s\",\"value\":%lld}\n",
                      sequence, stage, value);
        write_all(receipt_fd, record, std::strlen(record));
        (void)fsync(receipt_fd);
    }
}
extern "C" void fwcompat_gpu_failure()
{
    gpu_failed.store(true);
}
extern "C" int fwcompat_gpu_failed()
{
    return gpu_failed.load();
}
extern "C" void fwcompat_operation(const char *stage, int begin, long long value)
{
    char name[192];
    std::snprintf(name, sizeof(name), "%s.%s", stage, begin ? "begin" : "end");
    fwcompat_checkpoint(name, value);
}
extern "C" void fwcompat_attach(int fd, const char *folder)
{
    const std::lock_guard<std::mutex> guard(lock);
    log_fd = fd;
    write_all(log_fd, early, early_size);
    if (log_fd >= 0)
        (void)fsync(log_fd);
    early_size = 0;
    char path[256];
    std::snprintf(path, sizeof(path), "%s/fwcompat-startup.jsonl", folder);
    receipt_fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    char identity[512];
    std::snprintf(identity, sizeof(identity),
                  "{\"experiment\":\"legacy-fw-startup\",\"variant\":\"%s\",\"source_sha\":\"%s\","
                  "\"sdk\":\"%s\",\"pyrowave\":%d,\"preload\":%d,\"warmup\":%d,\"shader_cache\":%d,"
                  "\"force_1080p\":%d}\n",
                  variant(), FWCOMPAT_STRING(FWCOMPAT_SOURCE_SHA), FWCOMPAT_STRING(FWCOMPAT_SDK_ID),
                  PROSPEROLIGHT_PYROWAVE, !FWCOMPAT_SKIP_VIDEO_MODULE_PRELOAD,
                  !FWCOMPAT_SKIP_DECODER_WARMUP && !FWCOMPAT_SKIP_VIDEO_MODULE_PRELOAD,
                  !FWCOMPAT_DISABLE_SHADER_CACHE, FWCOMPAT_FORCE_1080P);
    write_all(receipt_fd, identity, std::strlen(identity));
    if (receipt_fd >= 0)
        (void)fsync(receipt_fd);
}
extern "C" void fwcompat_start()
{
    fwcompat_checkpoint("experiment.legacy-fw-startup", 1);
    fwcompat_checkpoint("process.start_unix", static_cast<long long>(time(nullptr)));
    fwcompat_checkpoint("git_sha." FWCOMPAT_STRING(FWCOMPAT_SOURCE_SHA), 0);
    fwcompat_checkpoint("sdk." FWCOMPAT_STRING(FWCOMPAT_SDK_ID), 0);
    fwcompat_checkpoint("pyrowave", PROSPEROLIGHT_PYROWAVE);
    fwcompat_checkpoint("videodec2_preload", !FWCOMPAT_SKIP_VIDEO_MODULE_PRELOAD);
    fwcompat_checkpoint("videodec2_warmup",
                        !FWCOMPAT_SKIP_DECODER_WARMUP && !FWCOMPAT_SKIP_VIDEO_MODULE_PRELOAD);
    fwcompat_checkpoint("shader_cache", !FWCOMPAT_DISABLE_SHADER_CACHE);
    fwcompat_checkpoint("force_1080p", FWCOMPAT_FORCE_1080P);
}
#endif
