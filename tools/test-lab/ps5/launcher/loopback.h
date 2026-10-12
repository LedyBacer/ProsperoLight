/* ps5-native-app-boilerplate - console-local PSFWD v1 bridge.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
static void put32(unsigned char *p, uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        p[i] = (unsigned char)(v >> (8 * i));
}
static uint32_t get32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static int transfer(int fd, unsigned char *p, size_t n, int writing)
{
    while (n)
    {
        ssize_t rc = writing ? send(fd, p, n, 0) : recv(fd, p, n, 0);
        if (rc < 0 && errno == EINTR)
            continue;
        if (rc <= 0)
            return -1;
        p += rc;
        n -= (size_t)rc;
    }
    return 0;
}
/* Existing resident service only. Never install or start a daemon here. */
static const char *loopback_launch(char *const argv[], int *rc, int32_t *user)
{
    unsigned char wire[0x1020] = {0}, reply[0x18] = {0};
    put32(wire, 0x4c574650u);
    put32(wire + 4, 1);
    memcpy(wire + 8, "PPSA99002", 10);
    size_t length = 0;
    for (size_t i = 0; i < 7; ++i)
    {
        size_t n = strlen(argv[i]) + 1;
        memcpy(wire + 0x20 + length, argv[i], n);
        length += n;
    }
    put32(wire + 0x18, 7);
    put32(wire + 0x1c, (uint32_t)length);
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        *rc = errno;
        return "PSFWD_UNAVAILABLE";
    }
    struct timeval timeout = {5, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_port = htons(10199);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const char *state = "PSFWD_UNAVAILABLE";
    if (!connect(fd, (struct sockaddr *)&address, sizeof(address)))
    {
        state = "PSFWD_REPLY_INCONCLUSIVE";
        if (!transfer(fd, wire, sizeof(wire), 1) && !transfer(fd, reply, sizeof(reply), 0) &&
            get32(reply) == 0x4c574650u && get32(reply + 4) == 1)
        {
            *rc = (int32_t)get32(reply + 0x10);
            if (!*rc)
                *rc = (int32_t)get32(reply + 0xc);
            *user = (int32_t)get32(reply + 0x14);
            uint32_t stage = get32(reply + 8);
            state = stage == 0   ? "LAUNCH_ACCEPTED"
                    : stage == 1 ? "PSFWD_BAD_REQUEST"
                    : stage == 2 ? "NO_USER"
                    : stage == 3 ? "LAUNCH_REFUSED"
                                 : "PSFWD_REPLY_INCONCLUSIVE";
        }
    }
    else
        *rc = errno;
    close(fd);
    return state;
}
