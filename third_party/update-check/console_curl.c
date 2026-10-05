/*
 * ps5-native-app-boilerplate - libcurl support for PS5 native titles.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * What PacBrew's libcurl 8.18.0 / OpenSSL 3.5.2 archives need to run in a
 * native title; see console_curl.h. Taken from ProsperoRadio (elevated) and
 * ProsperoLichess (sandboxed), where each piece was found on hardware. When a
 * PacBrew update brings new undefined symbols, the linker names them; a symbol
 * that links but resolves to libScePosixForWebKit is a null import at run time
 * and needs a definition here too.
 *
 * ProsperoLight: this copy leaves out what the app already defines, so each
 * has one definition: getaddrinfo and freeaddrinfo (platform/ps5/ps5_sockets.c,
 * the same resolver), and openlog, popen, pclose, isatty and mkstemp
 * (src/runtime/runtime_shims.c; its mkstemp is a real one, which the OpenGL
 * runtime's shader cache needs).
 */

#include "console_curl.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

extern int sceNetPoolCreate(const char *name, int size, int flags);
extern int sceNetPoolDestroy(int pool);
extern int sceNetResolverCreate(const char *name, int pool, int flags);
extern int sceNetResolverStartNtoa(int resolver, const char *hostname, struct in_addr *address,
                                   int timeout_us, int retries, int flags);
extern int sceNetResolverDestroy(int resolver);
extern const char *sceKernelGetFsSandboxRandomWord(void);

/* ---- Certificate list ---------------------------------------------------------------------- */

static pthread_mutex_t console_curl_ca_lock = PTHREAD_MUTEX_INITIALIZER;
static char console_curl_ca_path[160];

static int console_curl_readable(const char *path)
{
    struct stat facts;
    return stat(path, &facts) == 0 && S_ISREG(facts.st_mode);
}

const char *console_curl_ca_file(void)
{
    pthread_mutex_lock(&console_curl_ca_lock);
    if (console_curl_ca_path[0] == '\0' || !console_curl_readable(console_curl_ca_path))
    {
        snprintf(console_curl_ca_path, sizeof(console_curl_ca_path),
                 "/system/common/cert/CA_LIST.cer");
        if (!console_curl_readable(console_curl_ca_path))
        {
            const char *word = sceKernelGetFsSandboxRandomWord();
            snprintf(console_curl_ca_path, sizeof(console_curl_ca_path),
                     "/%s/common/cert/CA_LIST.cer", word != NULL ? word : "");
        }
    }
    pthread_mutex_unlock(&console_curl_ca_lock);
    return console_curl_ca_path;
}

/* ---- Per-handle setup ---------------------------------------------------------------------- */

enum
{
    CONSOLE_SO_NBIO = 0x1200 /* the console's own non-blocking socket option */
};

int console_curl_nonblocking(int socket)
{
    const int on = 1;
    return setsockopt(socket, SOL_SOCKET, CONSOLE_SO_NBIO, &on, sizeof(on));
}

static int console_curl_on_socket(void *user, curl_socket_t socket, curlsocktype purpose)
{
    (void)user;
    (void)purpose;
    /* Keep going if it fails: the transfer still works, only slowly. */
    (void)console_curl_nonblocking(socket);
    return CURL_SOCKOPT_OK;
}

#if defined(PROSPEROLIGHT_UPDATE_DEV_OFFER) && PROSPEROLIGHT_UPDATE_DEV_OFFER
/* ProsperoLight, development builds only: libcurl's own account of a transfer
 * (names resolved, connections tried, why one failed) goes to the log. */
static int console_curl_trace(CURL *easy, curl_infotype type, char *data, size_t size, void *user)
{
    (void)easy;
    (void)user;
    if (type == CURLINFO_TEXT && size != 0)
        printf("[curl] %.*s%s", (int)(size > 300 ? 300 : size), data,
               data[size - 1] == '\n' ? "" : "\n");
    return 0;
}
#endif

void console_curl_setup(CURL *easy)
{
#if defined(PROSPEROLIGHT_UPDATE_DEV_OFFER) && PROSPEROLIGHT_UPDATE_DEV_OFFER
    (void)curl_easy_setopt(easy, CURLOPT_DEBUGFUNCTION, console_curl_trace);
    (void)curl_easy_setopt(easy, CURLOPT_VERBOSE, 1L);
#endif
    (void)curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
    (void)curl_easy_setopt(easy, CURLOPT_CAINFO, console_curl_ca_file());
    (void)curl_easy_setopt(easy, CURLOPT_SOCKOPTFUNCTION, console_curl_on_socket);
}

/* ---- fcntl on sockets ---------------------------------------------------------------------- */

/* In a sandboxed title the console's libc refuses fcntl on sockets with EINVAL.
 * curl then fails every connect ("fcntl set CLOEXEC: Invalid argument") and its
 * sockets stay blocking. Linked with --wrap=fcntl, every fcntl call in the app
 * and the archives comes here; only a refused call is treated as a socket. */

extern int __real_fcntl(int descriptor, int command, ...);

static int console_curl_socket_flags(int socket, int command, int value)
{
    switch (command)
    {
    case F_GETFD:
    case F_SETFD:
        return 0; /* a title never execs another program */
    case F_SETFL:
    {
        const int on = (value & O_NONBLOCK) != 0;
        return setsockopt(socket, SOL_SOCKET, CONSOLE_SO_NBIO, &on, sizeof(on));
    }
    case F_GETFL:
    {
        int on = 0;
        socklen_t length = sizeof(on);
        if (getsockopt(socket, SOL_SOCKET, CONSOLE_SO_NBIO, &on, &length) < 0)
            return -1;
        return O_RDWR | (on ? O_NONBLOCK : 0);
    }
    default:
        errno = EINVAL;
        return -1;
    }
}

int __wrap_fcntl(int descriptor, int command, ...)
{
    va_list arguments;
    va_start(arguments, command);
    /* Every command curl and its libraries use takes an int or a pointer. */
    const intptr_t argument = va_arg(arguments, intptr_t);
    va_end(arguments);
    const int result = __real_fcntl(descriptor, command, argument);
    if (result >= 0 || errno != EINVAL)
        return result;
    if (command == F_GETFD || command == F_SETFD || command == F_SETFL || command == F_GETFL)
        return console_curl_socket_flags(descriptor, command, (int)argument);
    return result;
}

/* ---- Name lookup --------------------------------------------------------------------------- */

const char *gai_strerror(int code)
{
    switch (code)
    {
    case 0:
        return "no error";
    case EAI_NONAME:
        return "the name was not found";
    case EAI_FAMILY:
        return "address family not supported";
    case EAI_MEMORY:
        return "out of memory";
    default:
        return "the name lookup failed";
    }
}

struct hostent *gethostbyname(const char *name)
{
    (void)name;
    return NULL; /* libcurl uses getaddrinfo */
}

/* Numeric only: enough for libcurl's record of the connected address. */
int getnameinfo(const struct sockaddr *address, socklen_t length, char *host, size_t host_size,
                char *service, size_t service_size, int flags)
{
    (void)flags;
    if (address == NULL || address->sa_family != AF_INET || length < sizeof(struct sockaddr_in))
        return EAI_FAMILY;
    const struct sockaddr_in *in = (const struct sockaddr_in *)address;
    if (host != NULL && host_size != 0 &&
        inet_ntop(AF_INET, &in->sin_addr, host, (socklen_t)host_size) == NULL)
        return EAI_FAIL;
    if (service != NULL && service_size != 0)
        snprintf(service, service_size, "%u", (unsigned)ntohs(in->sin_port));
    return 0;
}

/* '*' and '?' only: all libcurl's wildcard matching asks for. */
int fnmatch(const char *pattern, const char *text, int flags)
{
    for (; *pattern != '\0'; ++pattern, ++text)
    {
        if (*pattern == '*')
        {
            while (*pattern == '*')
                ++pattern;
            if (*pattern == '\0')
                return 0;
            for (; *text != '\0'; ++text)
            {
                if (fnmatch(pattern, text, flags) == 0)
                    return 0;
            }
            return FNM_NOMATCH;
        }
        if (*text == '\0' || (*pattern != '?' && *pattern != *text))
            return FNM_NOMATCH;
    }
    return *text == '\0' ? 0 : FNM_NOMATCH;
}

/* ---- libc stand-ins ------------------------------------------------------------------------ */

/* No user database: "no such user", so curl looks for no home folder. */
struct passwd;
int getpwuid_r(unsigned user, struct passwd *entry, char *buffer, size_t size,
               struct passwd **result)
{
    (void)user;
    (void)entry;
    (void)buffer;
    (void)size;
    *result = NULL;
    return ENOENT;
}

/* The archives call _setjmp/_longjmp; the console exports setjmp/longjmp. A
 * plain jump keeps the caller's frame, which setjmp needs. x86-64 only. */
__asm__(".text\n"
        ".globl _setjmp\n"
        "_setjmp:\n"
        "    jmp *setjmp@GOTPCREL(%rip)\n"
        ".globl _longjmp\n"
        "_longjmp:\n"
        "    jmp *longjmp@GOTPCREL(%rip)\n");

void closelog(void)
{
}

int dladdr(const void *address, void *info)
{
    (void)address;
    (void)info;
    return 0;
}

unsigned if_nametoindex(const char *name)
{
    (void)name;
    return 0;
}

int pipe2(int descriptors[2], int flags)
{
    if (pipe(descriptors) != 0)
        return -1;
    for (int i = 0; i < 2; ++i)
    {
        if ((flags & O_NONBLOCK) != 0)
            (void)fcntl(descriptors[i], F_SETFL, fcntl(descriptors[i], F_GETFL) | O_NONBLOCK);
        if ((flags & O_CLOEXEC) != 0)
            (void)fcntl(descriptors[i], F_SETFD, FD_CLOEXEC);
    }
    return 0;
}

/* HTTP/3 only; curl falls back. */
ssize_t recvmmsg(int socket, struct mmsghdr *restrict messages, size_t count, int flags,
                 const struct timespec *restrict timeout)
{
    (void)socket;
    (void)messages;
    (void)count;
    (void)flags;
    (void)timeout;
    errno = ENOSYS;
    return -1;
}

ssize_t sendmmsg(int socket, struct mmsghdr *restrict messages, size_t count, int flags)
{
    (void)socket;
    (void)messages;
    (void)count;
    (void)flags;
    errno = ENOSYS;
    return -1;
}

/* zstd's optional tracing hooks; 0 means "not traced". */
unsigned long long ZSTD_trace_compress_begin(const void *context)
{
    (void)context;
    return 0;
}

void ZSTD_trace_compress_end(unsigned long long trace, const void *record)
{
    (void)trace;
    (void)record;
}

unsigned long long ZSTD_trace_decompress_begin(const void *context)
{
    (void)context;
    return 0;
}

void ZSTD_trace_decompress_end(unsigned long long trace, const void *record)
{
    (void)trace;
    (void)record;
}

/* OpenSSL checks certificate dates with it, so it must be right. Days since
 * 1970 to a civil date (Howard Hinnant's algorithm). */
struct tm *gmtime_r(const time_t *when, struct tm *out)
{
    long long seconds = (long long)*when;
    long long days = seconds / 86400;
    long long rest = seconds % 86400;
    if (rest < 0)
    {
        rest += 86400;
        --days;
    }
    memset(out, 0, sizeof(*out));
    out->tm_hour = (int)(rest / 3600);
    out->tm_min = (int)(rest % 3600 / 60);
    out->tm_sec = (int)(rest % 60);
    out->tm_wday = (int)(((days % 7) + 11) % 7);
    const long long z = days + 719468;
    const long long era = (z >= 0 ? z : z - 146096) / 146097;
    const long long doe = z - era * 146097;
    const long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const long long mp = (5 * doy + 2) / 153;
    const long long month = mp < 10 ? mp + 3 : mp - 9;
    const long long year = yoe + era * 400 + (month <= 2 ? 1 : 0);
    out->tm_mday = (int)(doy - (153 * mp + 2) / 5 + 1);
    out->tm_mon = (int)(month - 1);
    out->tm_year = (int)(year - 1900);
    const int leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    static const int before[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    out->tm_yday = before[out->tm_mon] + out->tm_mday - 1 + (leap && out->tm_mon > 1 ? 1 : 0);
    return out;
}
