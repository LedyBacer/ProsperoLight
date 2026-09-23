/*
 * ps5-native-app-boilerplate / ProsperoLight - Native socket adapter checks.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <unistd.h>
// Do not interpose Linux libc while exercising the PS5 adapter.
#define socket test_socket
#define bind test_bind
#define listen test_listen
#define accept test_accept
#define connect test_connect
#define send test_send
#define sendto test_sendto
#define recv test_recv
#define recvfrom test_recvfrom
#define setsockopt test_setsockopt
#define getsockopt test_getsockopt
#define getsockname test_getsockname
#define getpeername test_getpeername
#define shutdown test_shutdown
#define getaddrinfo test_getaddrinfo
#define freeaddrinfo test_freeaddrinfo
#define usleep test_usleep
#define perror test_perror
#include "../platform/ps5/ps5_sockets.c"
#include <assert.h>

static int fail_create, fail_control, fail_wait, native_errno = EIO;
static unsigned destroyed;
int *sceNetErrnoLoc(void)
{
    return &native_errno;
}
int sceKernelUsleep(uint32_t us)
{
    (void)us;
    return 0;
}
int sceNetEpollCreate(const char *name, int flags)
{
    (void)name;
    (void)flags;
    return fail_create ? -1 : 42;
}
int sceNetEpollControl(int epoll, int operation, int socket_id, struct sce_net_epoll_event *event)
{
    assert(epoll == 42 && operation == SCE_NET_EPOLL_CTL_ADD && socket_id >= 0);
    assert(event->events == SCE_NET_EPOLLIN);
    return fail_control ? -1 : 0;
}
int sceNetEpollWait(int epoll, struct sce_net_epoll_event *events, int count, int timeout)
{
    assert(epoll == 42 && count > 0 && timeout == 5000);
    if (fail_wait)
        return -1;
    events[0].events = SCE_NET_EPOLLIN;
    events[0].data.value = 0;
    return 1;
}
int sceNetEpollDestroy(int epoll)
{
    assert(epoll == 42);
    ++destroyed;
    return 0;
}
int sceNetRecvfrom(int socket_id, void *buffer, size_t length, int flags, struct sockaddr *address,
                   socklen_t *address_length)
{
    (void)socket_id;
    (void)buffer;
    (void)flags;
    (void)address;
    (void)address_length;
    return length ? (int)length : -1;
}
int sceNetSetsockopt(int socket_id, int level, int option, const void *value, socklen_t length)
{
    (void)socket_id;
    (void)level;
    (void)option;
    (void)length;
    return *(const int *)value ? 0 : -1;
}
int sceNetGetsockopt(int socket_id, int level, int option, void *value, socklen_t *length)
{
    (void)socket_id;
    (void)level;
    (void)option;
    *length = sizeof(int);
    *(int *)value = 65536;
    return 0;
}

int main(void)
{
    struct pollfd fds[9];
    for (unsigned i = 0; i < 9; ++i)
        fds[i] = (struct pollfd){(int)i, POLLIN, 0};
    ps5_network_metrics_begin(1);
    assert(ps5_socket_poll(NULL, 1, 5) == -1 && errno == EINVAL);
    assert(ps5_socket_poll(NULL, 0, 5) == 0);
    assert(ps5_socket_poll(fds, 1, 5) == 1 && fds[0].revents == POLLIN);
    assert(ps5_socket_poll(fds, 8, 5) == 1);
    assert(ps5_socket_poll(fds, 9, 5) == 1);
    fail_create = 1;
    assert(ps5_socket_poll(fds, 1, 5) == -1);
    fail_create = 0;
    fail_control = 1;
    assert(ps5_socket_poll(fds, 9, 5) == -1);
    fail_control = 0;
    fail_wait = 1;
    assert(ps5_socket_poll(fds, 1, 5) == -1);
    assert(destroyed == 5);
    char buffer[100];
    assert(recvfrom(1, buffer, sizeof(buffer), 0, NULL, NULL) == 100);
    native_errno = EAGAIN;
    assert(recvfrom(1, buffer, 0, 0, NULL, NULL) == -1);
    native_errno = EIO;
    assert(recvfrom(1, buffer, 0, 0, NULL, NULL) == -1);
    int size = 32768;
    assert(setsockopt(1, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size)) == 0);
    ps5_network_metrics_t m = ps5_network_metrics_read();
    assert(m.poll_calls == 6 && m.heap_polls == 2);
    assert(m.packets == 1 && m.bytes == 100 && m.receive_errors == 1);
    assert(m.last_buffer_requested == 32768 && m.last_buffer_actual == 65536);
    size = 0;
    assert(setsockopt(1, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size)) == -1);
    m = ps5_network_metrics_read();
    assert(m.buffer_requests == 2 && m.buffer_failures == 1 && m.last_buffer_actual == 0);
    ps5_network_metrics_begin(0);
    assert(recvfrom(1, buffer, sizeof(buffer), 0, NULL, NULL) == 100);
    assert(ps5_network_metrics_read().packets == 0);
    puts("Socket stack/heap paths, cleanup and telemetry PASS");
}
