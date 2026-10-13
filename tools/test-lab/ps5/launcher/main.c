/* ps5-native-app-boilerplate - one-shot PR0 launcher; no resident service.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "protocol.h"
#include <sys/types.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/sysctl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#if PLTEST_KERNEL_FW
#include <ps5/kernel.h>
#endif
#ifndef ROOT
#define ROOT "/data/prosperolight/test-lab/pr0"
#endif
#ifndef PR0_ROUTE
#define PR0_ROUTE 0
#endif
#if PR0_ROUTE == 2
#include "loopback.h"
#endif
struct app_info
{
    uint32_t app_id;
    uint64_t unknown1;
    uint32_t app_type;
    char title_id[10];
    char unknown2[0x3c];
};
struct launch_param
{
    uint32_t size;
    int32_t user_id;
    uint8_t reserved[24];
};
int sceLncUtilGetAppIdOfRunningBigApp(void);
int sceLncUtilGetAppTitleId(uint32_t app_id, char *title_id);
int sceKernelGetAppInfo(pid_t pid, struct app_info *info);
int sceUserServiceInitialize(void *params);
int sceUserServiceGetForegroundUser(int32_t *user);
int sceSystemServiceLaunchApp(const char *title, char *const argv[], struct launch_param *p);
/* SDK samples/ps uses this enumeration and app-info ABI. Any unreadable
 * non-kernel process makes the preflight inconclusive, so fail closed. */
static int g_target_pid = -1;
static int title_running(void)
{
    /* Kernel app-info can return an empty title for mounted homebrew.
     * The shell's active-app identity is authoritative: refuse any active app. */
    int active = 0;
    int app = sceLncUtilGetAppIdOfRunningBigApp();
    if (app != -1)
    {
        if (app < 0)
            return -1;
        char title[16] = {0};
        if (sceLncUtilGetAppTitleId((uint32_t)app, title) < 0)
            return -1;
        active = !memcmp(title, "PPSA99002", 9) ? 1 : 2;
    }
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
    size_t size = 0;
    if (sysctl(mib, 4, NULL, &size, NULL, 0) || !size || size > 16 * 1024 * 1024)
        return active ? active : -1;
    char *buf = malloc(size);
    if (!buf)
        return active ? active : -1;
    int result = 0;
    if (sysctl(mib, 4, buf, &size, NULL, 0))
        result = -1;
    else
        for (size_t at = 0; at < size;)
        {
            if (size - at < sizeof(struct kinfo_proc))
            {
                result = -1;
                break;
            }
            struct kinfo_proc *ki = (struct kinfo_proc *)(buf + at);
            if (ki->ki_structsize <= 0 || (size_t)ki->ki_structsize < sizeof(*ki) ||
                (size_t)ki->ki_structsize > size - at)
            {
                result = -1;
                break;
            }
            struct app_info info = {0};
            if (ki->ki_pid > 0 && sceKernelGetAppInfo(ki->ki_pid, &info))
                result = -1;
            else if (!memcmp(info.title_id, "PPSA99002", 9) ||
                     (active == 1 && info.app_id == (uint32_t)app))
            {
                g_target_pid = ki->ki_pid;
                result = 1;
                break;
            }
            at += ki->ki_structsize;
        }
    free(buf);
    return active ? active : result;
}
/* Read-only process argv observation. Do not expose unrelated process data. */
static int observed_arguments(const char *pc)
{
    if (g_target_pid < 0)
        return 0;
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_ARGS, g_target_pid};
    char args[4096] = {0};
    size_t n = sizeof(args);
    if (sysctl(mib, 4, args, &n, NULL, 0) || n > sizeof(args))
        return 0;
    char expected[512];
    size_t at = 0;
    const char *words[] = {"--pc", pc, "--app", "Desktop", "--exit-after-stream"};
    for (size_t i = 0; i < 5; ++i)
    {
        size_t len = strlen(words[i]) + 1;
        memcpy(expected + at, words[i], len);
        at += len;
    }
    for (size_t i = 0; i + at <= n; ++i)
        if ((i == 0 || args[i - 1] == 0) && !memcmp(args + i, expected, at))
            return 1;
    return 0;
}
static int g_argv_observed = 0;
static int receipt(const char *id, const char *state, int rc, int user)
{
    char path[256], tmp[256];
    snprintf(path, sizeof(path), ROOT "/%.127s.json", id);
    snprintf(tmp, sizeof(tmp), ROOT "/%.127s.tmp", id);
    FILE *f = fopen(tmp, "wx");
    if (!f)
        return 1;
    uint32_t firmware = 0;
#if PLTEST_KERNEL_FW
    firmware = kernel_get_fw_version();
#endif
    int ok =
        fprintf(
            f,
            "{\"schema\":\"prosperolight-launch-spike/1\",\"request_id\":\"%s\","
            "\"state\":\"%s\",\"launch_rc\":%d,\"user\":%d,"
            "\"route\":%d,\"argv_verified\":false,\"target_pid\":%d,\"kernel_argv_observed\":%s,"
            "\"firmware_u32\":%u,\"firmware_verified\":%s,\"firmware_source\":\"kernel_get_fw_"
            "version\"}\n",
            id, state, rc, user, PR0_ROUTE, g_target_pid, g_argv_observed ? "true" : "false",
            firmware, firmware ? "true" : "false") > 0;
    if (fclose(f))
        ok = 0;
    if (!ok || rename(tmp, path))
        return 1;
    printf("PR0 %s: %s rc=0x%08x\n", id, state, (unsigned)rc);
    return 0;
}
int main(void)
{
    /* Inspectable transport capability: prevents using a launch ELF for preflight. */
#define PLTEST_STRING_IMPL(x) #x
#define PLTEST_STRING(x) PLTEST_STRING_IMPL(x)
    printf("PLTEST_HELPER_ROUTE=" PLTEST_STRING(PR0_ROUTE) "\n");
    char request[REQUEST_BYTES];
    FILE *f = fopen(ROOT "/request.bin", "rb");
    if (!f)
        return 1;
    size_t n = fread(request, 1, sizeof(request), f);
    int extra = fgetc(f);
    fclose(f);
    if (n != sizeof(request) || extra != EOF || !valid_request(request))
        return 1;
    /* Consume once. A replayed helper cannot launch a second session. */
    if (rename(ROOT "/request.bin", ROOT "/consumed.bin"))
        return 1;
    char previous[256];
    snprintf(previous, sizeof(previous), ROOT "/%.127s.json", request);
    if (!access(previous, F_OK))
        return 1;
    if (receipt(request, "preflight", 0, -1))
        return 1;
    int active = title_running();
    if (active > 0)
        g_argv_observed = observed_arguments(request + 128);
    if (active != 0)
        return receipt(request,
                       active == 1   ? "ALREADY_RUNNING"
                       : active == 2 ? "OTHER_TITLE_ACTIVE"
                                     : "PROCESS_PROBE_FAILED",
                       active, -1);
#if PR0_ROUTE == 3
    return receipt(request, "TITLE_IDLE", 0, -1);
#endif
    int32_t user = -1;
    int rc = sceUserServiceInitialize(NULL);
    (void)rc; /* Already-initialized is acceptable; foreground query is decisive. */
    rc = sceUserServiceGetForegroundUser(&user);
    if (rc < 0 || user == -1)
        return receipt(request, "NO_USER", rc, user);
#if PR0_ROUTE == 4
    char *argv[] = {"--test-run", request, "--exit-after-stream", NULL};
#else
    char *argv[] = {"--pc",           request + 128, "--app", "Desktop", "--exit-after-stream",
                    "--launch-proof", request,       NULL};
#endif
#if PR0_ROUTE == 2
    const char *state = loopback_launch(argv, &rc, &user);
    return receipt(request, state, rc, user);
#else
    struct launch_param param = {0};
#if PR0_ROUTE == 1
    param.size = sizeof(param);
#endif
    param.user_id = user;
    rc = sceSystemServiceLaunchApp("PPSA99002", argv, &param);
    /* Do not retry ambiguous/queued errors: avoid a duplicate launch. */
    return receipt(request,
                   rc >= 0                       ? "LAUNCH_ACCEPTED"
                   : (uint32_t)rc == 0x80940010u ? "LAUNCH_QUEUED_UNVERIFIED"
                                                 : "LAUNCH_REFUSED",
                   rc, user);
#endif
}
