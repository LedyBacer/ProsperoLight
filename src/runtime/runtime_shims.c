/*
 * ps5-native-app-boilerplate - ProsperoLight component.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Process-level pieces the statically linked OpenGL runtime expects, adapted
 * from ps5-opengl's native-app/runtime_shims.c: where standard output goes,
 * what happens if main returns, and a few libc entry points the application
 * libc does not provide.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

extern int sceKernelUsleep(uint32_t microseconds);

/* The log is kept in the title's own storage, beside the saved settings. */
#define PROSPEROLIGHT_LOG_PATH "/download0/prosperolight-launcher.log"
#define PROSPEROLIGHT_PREVIOUS_LOG_PATH "/download0/prosperolight-launcher.prev.log"

__attribute__((constructor)) static void prosperolight_open_log(void)
{
    /* Keep the previous launch's log: it is the one that explains a crash. */
    rename(PROSPEROLIGHT_LOG_PATH, PROSPEROLIGHT_PREVIOUS_LOG_PATH);
    FILE *stream = freopen(PROSPEROLIGHT_LOG_PATH, "w", stdout);
    /* Start a fresh file, then make both streams append-only and unbuffered
     * so the log survives a close from the home screen or a GPU fail-stop. */
    if (stream != NULL)
        stream = freopen(PROSPEROLIGHT_LOG_PATH, "a", stdout);
    if (stream != NULL)
        setvbuf(stream, NULL, _IONBF, 0);
    stream = freopen(PROSPEROLIGHT_LOG_PATH, "a", stderr);
    if (stream != NULL)
        setvbuf(stream, NULL, _IONBF, 0);
}

/* Returning from main or calling exit() crashes a native title; stay alive
 * until the console closes it. */
__attribute__((noreturn)) void catchReturnFromMain(int status)
{
    printf("[PL] main returned status=%d\n", status);
    fflush(NULL);
    for (;;)
        sceKernelUsleep(100000);
}

void prosperolight_glapi_tls_context_init(void) __asm__("_ZTH23_mesa_glapi_tls_Context");

void prosperolight_glapi_tls_context_init(void)
{
}

__attribute__((noreturn)) void __assert(const char *function, const char *file, int line,
                                        const char *expression)
{
    fprintf(stderr, "[PL] assertion failed: %s (%s:%d, %s)\n", expression, file, line, function);
    abort();
}

int mkstemps(char *template_name, int suffix_length)
{
    (void)template_name;
    (void)suffix_length;
    errno = ENOSYS;
    return -1;
}

void openlog(const char *identifier, int option, int facility)
{
    (void)identifier;
    (void)option;
    (void)facility;
}

FILE *popen(const char *command, const char *mode)
{
    (void)command;
    (void)mode;
    errno = ENOSYS;
    return NULL;
}

int pclose(FILE *stream)
{
    (void)stream;
    errno = ENOSYS;
    return -1;
}
