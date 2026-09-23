/*
 * ps5-native-app-boilerplate - ProsperoLight component.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define PS5_SOCKET_ADAPTER_IMPLEMENTATION 1

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdatomic.h>
#include "ps5_network_metrics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>

struct sce_net_epoll_event {
    uint32_t events;
    uint32_t pad;
    uint64_t ident;
    union {
        void *pointer;
        uint32_t value;
        uint64_t value64;
        int socket;
    } data;
};

_Static_assert(sizeof(struct sce_net_epoll_event) == 24,
               "sceNet epoll event ABI mismatch");

enum {
    SCE_NET_EPOLLIN = 0x00000001,
    SCE_NET_EPOLLOUT = 0x00000002,
    SCE_NET_EPOLLERR = 0x00000008,
    SCE_NET_EPOLLHUP = 0x00000010,
    SCE_NET_EPOLL_CTL_ADD = 1,
    SCE_NET_SO_NBIO = 0x1200,
};

int sceKernelUsleep(uint32_t microseconds);
int sceNetAccept(int socket, struct sockaddr *address, socklen_t *length);
int sceNetBind(int socket, const struct sockaddr *address, socklen_t length);
int sceNetConnect(int socket, const struct sockaddr *address, socklen_t length);
int sceNetEpollControl(int epoll, int operation, int socket,
                       struct sce_net_epoll_event *event);
int sceNetEpollCreate(const char *name, int flags);
int sceNetEpollDestroy(int epoll);
int sceNetEpollWait(int epoll, struct sce_net_epoll_event *events,
                    int maximum_events, int timeout_microseconds);
int *sceNetErrnoLoc(void);
int sceNetGetpeername(int socket, struct sockaddr *address, socklen_t *length);
int sceNetGetsockname(int socket, struct sockaddr *address, socklen_t *length);
int sceNetGetsockopt(int socket, int level, int option, void *value,
                     socklen_t *length);
int sceNetListen(int socket, int backlog);
int sceNetPoolCreate(const char *name, int size, int flags);
int sceNetPoolDestroy(int pool);
int sceNetRecv(int socket, void *buffer, size_t length, int flags);
int sceNetRecvfrom(int socket, void *buffer, size_t length, int flags,
                   struct sockaddr *address, socklen_t *address_length);
int sceNetResolverCreate(const char *name, int pool, int flags);
int sceNetResolverDestroy(int resolver);
int sceNetResolverStartNtoa(int resolver, const char *host, uint32_t *address,
                            int timeout_microseconds, int retries, int flags);
int sceNetSend(int socket, const void *buffer, size_t length, int flags);
int sceNetSendto(int socket, const void *buffer, size_t length, int flags,
                 const struct sockaddr *address, socklen_t address_length);
int sceNetSetsockopt(int socket, int level, int option, const void *value,
                     socklen_t length);
int sceNetShutdown(int socket, int how);
int sceNetSocket(const char *name, int domain, int type, int protocol);
int sceNetSocketClose(int socket);

static int network_result(int result)
{
    if (result < 0) {
        int *network_errno = sceNetErrnoLoc();
        if (network_errno)
            errno = *network_errno;
        return -1;
    }
    return result;
}

static _Atomic int metrics_enabled;
static _Atomic uint64_t metrics[9];
void ps5_network_metrics_begin(int enabled)
{
    atomic_store(&metrics_enabled, 0);
    for (unsigned i = 0; i < 9; ++i)
        atomic_store(&metrics[i], 0);
    atomic_store(&metrics_enabled, enabled);
}
ps5_network_metrics_t ps5_network_metrics_read(void)
{
    ps5_network_metrics_t result = {
        atomic_load(&metrics[0]), atomic_load(&metrics[1]), atomic_load(&metrics[2]),
        atomic_load(&metrics[3]), atomic_load(&metrics[4]), atomic_load(&metrics[5]),
        atomic_load(&metrics[6]), atomic_load(&metrics[7]), atomic_load(&metrics[8])};
    return result;
}

int socket(int domain, int type, int protocol)
{
    return network_result(sceNetSocket("moonlight", domain, type, protocol));
}

int bind(int socket_id, const struct sockaddr *address, socklen_t length)
{
    return network_result(sceNetBind(socket_id, address, length));
}

int listen(int socket_id, int backlog)
{
    return network_result(sceNetListen(socket_id, backlog));
}

int accept(int socket_id, struct sockaddr *address, socklen_t *length)
{
    return network_result(sceNetAccept(socket_id, address, length));
}

int connect(int socket_id, const struct sockaddr *address, socklen_t length)
{
    return network_result(sceNetConnect(socket_id, address, length));
}

ssize_t send(int socket_id, const void *buffer, size_t length, int flags)
{
    flags &= ~MSG_NOSIGNAL;
    return network_result(sceNetSend(socket_id, buffer, length, flags));
}

ssize_t sendto(int socket_id, const void *buffer, size_t length, int flags,
               const struct sockaddr *address, socklen_t address_length)
{
    flags &= ~MSG_NOSIGNAL;
    return network_result(sceNetSendto(socket_id, buffer, length, flags,
                                       address, address_length));
}

ssize_t recv(int socket_id, void *buffer, size_t length, int flags)
{
    flags &= ~MSG_NOSIGNAL;
    return network_result(sceNetRecv(socket_id, buffer, length, flags));
}

ssize_t recvfrom(int socket_id, void *buffer, size_t length, int flags,
                 struct sockaddr *address, socklen_t *address_length)
{
    flags &= ~MSG_NOSIGNAL;
    int result = network_result(sceNetRecvfrom(socket_id, buffer, length, flags,
                                               address, address_length));
    if (atomic_load_explicit(&metrics_enabled, memory_order_relaxed)) {
        if (result >= 0) {
            atomic_fetch_add_explicit(&metrics[0], 1, memory_order_relaxed);
            atomic_fetch_add_explicit(&metrics[1], (uint64_t)result, memory_order_relaxed);
        } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            atomic_fetch_add_explicit(&metrics[2], 1, memory_order_relaxed);
        }
    }
    return result;
}

int setsockopt(int socket_id, int level, int option, const void *value,
               socklen_t length)
{
    int result = network_result(sceNetSetsockopt(socket_id, level, option, value, length));
    if (atomic_load_explicit(&metrics_enabled, memory_order_relaxed) &&
        level == SOL_SOCKET && option == SO_RCVBUF && value && length == sizeof(int)) {
        int requested = 0, actual = 0;
        socklen_t actual_size = sizeof(actual);
        memcpy(&requested, value, sizeof(requested));
        atomic_fetch_add(&metrics[5], 1);
        if (result < 0)
            atomic_fetch_add(&metrics[6], 1);
        atomic_store(&metrics[7], requested > 0 ? (uint64_t)requested : 0);
        atomic_store(&metrics[8], 0);
        if (result == 0 && sceNetGetsockopt(socket_id, level, option, &actual, &actual_size) == 0)
            atomic_store(&metrics[8], actual > 0 ? (uint64_t)actual : 0);
    }
    return result;
}

int getsockopt(int socket_id, int level, int option, void *value,
               socklen_t *length)
{
    return network_result(sceNetGetsockopt(socket_id, level, option, value,
                                           length));
}

int getsockname(int socket_id, struct sockaddr *address, socklen_t *length)
{
    return network_result(sceNetGetsockname(socket_id, address, length));
}

int getpeername(int socket_id, struct sockaddr *address, socklen_t *length)
{
    return network_result(sceNetGetpeername(socket_id, address, length));
}

int shutdown(int socket_id, int how)
{
    return network_result(sceNetShutdown(socket_id, how));
}

int ps5_socket_close(int socket_id)
{
    return network_result(sceNetSocketClose(socket_id));
}

int ps5_socket_fcntl(int socket_id, int command, ...)
{
    if (command == F_GETFL)
    {
        int enabled = 0;
        socklen_t length = sizeof(enabled);

        if (network_result(sceNetGetsockopt(socket_id, SOL_SOCKET, SCE_NET_SO_NBIO, &enabled,
                                            &length)) < 0)
            return -1;
        return enabled ? O_NONBLOCK : 0;
    }

    if (command == F_SETFL)
    {
        va_list arguments;
        int flags;
        int enabled;

        va_start(arguments, command);
        flags = va_arg(arguments, int);
        va_end(arguments);
        enabled = (flags & O_NONBLOCK) != 0;
        return network_result(sceNetSetsockopt(socket_id, SOL_SOCKET, SCE_NET_SO_NBIO, &enabled,
                                               sizeof(enabled)));
    }

    errno = EINVAL;
    return -1;
}

int ps5_socket_ioctl(int socket_id, unsigned long request, ...)
{
    va_list arguments;
    int *value;

    va_start(arguments, request);
    value = va_arg(arguments, int *);
    va_end(arguments);

    if (request != FIONBIO || !value) {
        errno = EINVAL;
        return -1;
    }

    return network_result(sceNetSetsockopt(socket_id, SOL_SOCKET,
                                           SCE_NET_SO_NBIO, value,
                                           sizeof(*value)));
}

static uint32_t poll_events_to_sce(short events)
{
    uint32_t result = 0;
    if (events & (POLLIN | POLLRDNORM))
        result |= SCE_NET_EPOLLIN;
    if (events & (POLLOUT | POLLWRNORM))
        result |= SCE_NET_EPOLLOUT;
    return result;
}

static short poll_events_from_sce(uint32_t events)
{
    short result = 0;
    if (events & SCE_NET_EPOLLIN)
        result |= POLLIN;
    if (events & SCE_NET_EPOLLOUT)
        result |= POLLOUT;
    if (events & SCE_NET_EPOLLERR)
        result |= POLLERR;
    if (events & SCE_NET_EPOLLHUP)
        result |= POLLHUP;
    return result;
}

int ps5_socket_poll(struct pollfd *descriptors, nfds_t count,
                    int timeout_milliseconds)
{
    struct sce_net_epoll_event local_events[8] = {0};
    struct sce_net_epoll_event *events;
    int epoll;
    int ready;
    int timeout_microseconds;

    if (!descriptors && count != 0) {
        errno = EINVAL;
        return -1;
    }
    if (count == 0) {
        if (timeout_milliseconds > 0)
            sceKernelUsleep((uint32_t)timeout_milliseconds * 1000);
        return 0;
    }
    if (count > INT_MAX) {
        errno = EINVAL;
        return -1;
    }

    events = count <= 8 ? local_events : calloc((size_t)count, sizeof(*events));
    if (atomic_load_explicit(&metrics_enabled, memory_order_relaxed)) {
        atomic_fetch_add_explicit(&metrics[3], 1, memory_order_relaxed);
        if (count > 8)
            atomic_fetch_add_explicit(&metrics[4], 1, memory_order_relaxed);
    }
    if (!events) {
        errno = ENOMEM;
        return -1;
    }

    epoll = network_result(sceNetEpollCreate("moonlight-poll", 0));
    if (epoll < 0) {
        if (events != local_events) free(events);
        return -1;
    }

    for (nfds_t index = 0; index < count; ++index) {
        struct sce_net_epoll_event event = {0};
        descriptors[index].revents = 0;
        if (descriptors[index].fd < 0)
            continue;
        event.events = poll_events_to_sce(descriptors[index].events);
        event.data.value = (uint32_t)index;
        if (network_result(sceNetEpollControl(epoll, SCE_NET_EPOLL_CTL_ADD,
                                              descriptors[index].fd,
                                              &event)) < 0) {
            sceNetEpollDestroy(epoll);
            if (events != local_events) free(events);
            return -1;
        }
    }

    if (timeout_milliseconds < 0)
        timeout_microseconds = -1;
    else if (timeout_milliseconds > INT_MAX / 1000)
        timeout_microseconds = INT_MAX;
    else
        timeout_microseconds = timeout_milliseconds * 1000;

    ready = network_result(sceNetEpollWait(epoll, events, (int)count,
                                           timeout_microseconds));
    if (ready >= 0) {
        for (int index = 0; index < ready; ++index) {
            uint32_t descriptor_index = events[index].data.value;
            if (descriptor_index < count)
                descriptors[descriptor_index].revents |=
                    poll_events_from_sce(events[index].events);
        }
    }

    sceNetEpollDestroy(epoll);
    if (events != local_events) free(events);
    return ready;
}

int ps5_socket_select(int descriptor_count, fd_set *read_set,
                      fd_set *write_set, fd_set *error_set,
                      struct timeval *timeout)
{
    fd_set input_read;
    fd_set input_write;
    fd_set input_error;
    struct pollfd *descriptors;
    int timeout_milliseconds = -1;
    int used = 0;
    int result;

    if (descriptor_count < 0) {
        errno = EINVAL;
        return -1;
    }

    if (read_set)
        input_read = *read_set;
    if (write_set)
        input_write = *write_set;
    if (error_set)
        input_error = *error_set;
    if (timeout) {
        int64_t microseconds = (int64_t)timeout->tv_sec * 1000000 +
                               timeout->tv_usec;
        timeout_milliseconds = microseconds <= 0 ? 0 :
            (microseconds >= (int64_t)INT_MAX * 1000 ? INT_MAX :
             (int)((microseconds + 999) / 1000));
    }

    descriptors = calloc((size_t)descriptor_count, sizeof(*descriptors));
    if (!descriptors && descriptor_count != 0) {
        errno = ENOMEM;
        return -1;
    }

    for (int socket_id = 0; socket_id < descriptor_count; ++socket_id) {
        short events = 0;
        if (read_set && FD_ISSET(socket_id, &input_read))
            events |= POLLIN;
        if (write_set && FD_ISSET(socket_id, &input_write))
            events |= POLLOUT;
        if (error_set && FD_ISSET(socket_id, &input_error))
            events |= POLLIN | POLLOUT;
        if (events) {
            descriptors[used].fd = socket_id;
            descriptors[used].events = events;
            ++used;
        }
    }

    if (read_set)
        FD_ZERO(read_set);
    if (write_set)
        FD_ZERO(write_set);
    if (error_set)
        FD_ZERO(error_set);

    result = ps5_socket_poll(descriptors, (nfds_t)used,
                             timeout_milliseconds);
    if (result > 0) {
        int ready_descriptors = 0;
        for (int index = 0; index < used; ++index) {
            int socket_id = descriptors[index].fd;
            int any = 0;
            if (read_set && (descriptors[index].revents & (POLLIN | POLLHUP))) {
                FD_SET(socket_id, read_set);
                any = 1;
            }
            if (write_set && (descriptors[index].revents & POLLOUT)) {
                FD_SET(socket_id, write_set);
                any = 1;
            }
            if (error_set && (descriptors[index].revents & POLLERR)) {
                FD_SET(socket_id, error_set);
                any = 1;
            }
            ready_descriptors += any;
        }
        result = ready_descriptors;
    }

    free(descriptors);
    return result;
}

static int parse_ipv4(const char *text, unsigned char octets[4])
{
    for (int part = 0; part < 4; ++part) {
        unsigned int value = 0;
        int digits = 0;
        while (*text >= '0' && *text <= '9') {
            value = value * 10 + (unsigned int)(*text++ - '0');
            if (++digits > 3 || value > 255)
                return 0;
        }
        if (digits == 0 || (part != 3 && *text++ != '.'))
            return 0;
        octets[part] = (unsigned char)value;
    }
    return *text == '\0';
}

int __inet_pton(int family, const char *text, void *address)
{
    unsigned char octets[4];
    if (family != AF_INET) {
        errno = EAFNOSUPPORT;
        return -1;
    }
    if (!text || !address || !parse_ipv4(text, octets))
        return 0;
    memcpy(address, octets, sizeof(octets));
    return 1;
}

const char *__inet_ntop(int family, const void *address, char *text,
                        socklen_t length)
{
    const unsigned char *octets = address;
    int written;
    if (family != AF_INET) {
        errno = EAFNOSUPPORT;
        return NULL;
    }
    if (!address || !text) {
        errno = EINVAL;
        return NULL;
    }
    written = snprintf(text, length, "%u.%u.%u.%u", octets[0], octets[1],
                       octets[2], octets[3]);
    if (written < 0 || (socklen_t)written >= length) {
        errno = ENOSPC;
        return NULL;
    }
    return text;
}

static int parse_port(const char *service, uint16_t *port)
{
    unsigned int value = 0;
    if (!service) {
        *port = 0;
        return 1;
    }
    if (!*service)
        return 0;
    while (*service >= '0' && *service <= '9') {
        value = value * 10 + (unsigned int)(*service++ - '0');
        if (value > 65535)
            return 0;
    }
    if (*service != '\0')
        return 0;
    *port = htons((uint16_t)value);
    return 1;
}

int getaddrinfo(const char *node, const char *service,
                const struct addrinfo *hints, struct addrinfo **result)
{
    struct addrinfo *info;
    struct sockaddr_in *address;
    unsigned char octets[4];
    uint32_t network_address = 0;
    uint16_t network_port;
    int family = hints ? hints->ai_family : AF_UNSPEC;

    if (!result)
        return EAI_FAIL;
    *result = NULL;
    if (family != AF_UNSPEC && family != AF_INET)
        return EAI_FAMILY;
    if (!parse_port(service, &network_port))
        return EAI_SERVICE;

    if (!node) {
        if (!hints || !(hints->ai_flags & AI_PASSIVE)) {
            static const unsigned char loopback[4] = {127, 0, 0, 1};
            memcpy(&network_address, loopback, sizeof(loopback));
        }
    }
    else if (parse_ipv4(node, octets)) {
        memcpy(&network_address, octets, sizeof(octets));
    }
    else {
        int pool = sceNetPoolCreate("moonlight-dns", 0x4000, 0);
        int resolver;
        int resolve_result;
        if (pool < 0)
            return EAI_MEMORY;
        resolver = sceNetResolverCreate("moonlight-dns", pool, 0);
        if (resolver < 0) {
            sceNetPoolDestroy(pool);
            return EAI_FAIL;
        }
        resolve_result = sceNetResolverStartNtoa(resolver, node,
                                                 &network_address,
                                                 5000000, 2, 0);
        sceNetResolverDestroy(resolver);
        sceNetPoolDestroy(pool);
        if (resolve_result < 0)
            return EAI_NONAME;
    }

    info = calloc(1, sizeof(*info) + sizeof(*address));
    if (!info)
        return EAI_MEMORY;
    address = (struct sockaddr_in *)(info + 1);
#ifndef __linux__ // Host-only adapter tests use Linux sockaddr_in.
    address->sin_len = sizeof(*address);
#endif
    address->sin_family = AF_INET;
    address->sin_port = network_port;
    address->sin_addr.s_addr = network_address;

    info->ai_family = AF_INET;
    info->ai_socktype = hints ? hints->ai_socktype : 0;
    info->ai_protocol = hints ? hints->ai_protocol : 0;
    info->ai_addrlen = sizeof(*address);
    info->ai_addr = (struct sockaddr *)address;
    *result = info;
    return 0;
}

void freeaddrinfo(struct addrinfo *info)
{
    while (info) {
        struct addrinfo *next = info->ai_next;
        free(info);
        info = next;
    }
}

int usleep(useconds_t microseconds)
{
    return sceKernelUsleep(microseconds);
}

void perror(const char *prefix)
{
    fprintf(stderr, "%s%s%u\n", prefix ? prefix : "",
            prefix && *prefix ? ": errno " : "errno ", (unsigned int)errno);
}

const struct in6_addr in6addr_any = {{0}};
