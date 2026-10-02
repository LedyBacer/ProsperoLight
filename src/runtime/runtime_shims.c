/*
 * ps5-native-app-boilerplate - ProsperoLight component.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Process-level pieces the statically linked OpenGL runtime expects, adapted
 * from ps5-opengl's native-app/runtime_shims.c: when the splash picture goes,
 * what happens if main returns,
 * and a few libc entry points the application libc does not provide. Standard
 * output becomes the log in src/app_storage.cpp.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

extern int sceKernelUsleep(uint32_t microseconds);

/* The console's splash picture stays up until the launcher has drawn its first
 * frame. The OpenGL runtime asks to hide it as soon as the display opens, which
 * would leave a black screen while fonts load and shaders compile; the build
 * routes every such call here (--wrap), and the launcher says when. */
extern int __real_sceSystemServiceHideSplashScreen(void);
static int prosperolight_splash_released;

void prosperolight_release_splash(void)
{
    prosperolight_splash_released = 1;
}

int __wrap_sceSystemServiceHideSplashScreen(void)
{
    return prosperolight_splash_released ? __real_sceSystemServiceHideSplashScreen() : 0;
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
