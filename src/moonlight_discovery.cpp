/*
 * ps5-native-app-boilerplate - ProsperoLight component.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "moonlight_discovery.hpp"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define DNS_PTR 12U
#define DNS_SRV 33U
#define DNS_HEADER_SIZE 12U
#define NET_AF_INET 2
#define NET_SOCK_DGRAM 2
#define NET_IPPROTO_UDP 17
#define NET_SOL_SOCKET 0xffff
#define NET_SO_NBIO 0x1200
#define NET_EPOLLIN 0x00000001U
#define NET_EPOLL_CTL_ADD 1
#define MDNS_PORT 5353U
#define MDNS_WAIT_US 150000
#define MDNS_WAIT_ROUNDS 5
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

typedef struct net_epoll_event
{
    uint32_t events;
    uint32_t pad;
    uint64_t ident;
    union
    {
        void *pointer;
        uint32_t value;
        uint64_t value64;
        int socket;
    } data;
} net_epoll_event_t;

extern "C"
{
    int sceNetSocket(const char *name, int domain, int type, int protocol);
    extern int sceNetSocketClose(int socket);
    extern int sceNetSendto(int socket, const void *buffer, size_t length, int flags,
                            const void *address, uint32_t address_length);
    extern int sceNetRecvfrom(int socket, void *buffer, size_t length, int flags, void *address,
                              uint32_t *address_length);
    extern int sceNetSetsockopt(int socket, int level, int option, const void *value,
                                uint32_t length);
    extern int sceNetEpollCreate(const char *name, int flags);
    extern int sceNetEpollControl(int epoll, int operation, int socket, net_epoll_event_t *event);
    extern int sceNetEpollWait(int epoll, net_epoll_event_t *events, int maximum_events,
                               int timeout_microseconds);
    int sceNetEpollDestroy(int epoll);
}

static uint16_t read_u16(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static uint16_t big_endian_u16(uint16_t value)
{
    return (uint16_t)((value << 8) | (value >> 8));
}

static int ascii_equal(const char *left, const char *right)
{
    while (*left && *right)
    {
        unsigned char a = (unsigned char)*left++;
        unsigned char b = (unsigned char)*right++;
        if (a >= 'A' && a <= 'Z')
            a = (unsigned char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z')
            b = (unsigned char)(b + ('a' - 'A'));
        if (a != b)
            return 0;
    }
    return *left == *right;
}

static int read_name(const uint8_t *packet, size_t length, size_t *cursor, char *output,
                     size_t capacity)
{
    size_t position = *cursor;
    size_t return_position = 0;
    size_t written = 0;
    unsigned jumps = 0;

    if (!capacity)
        return 0;
    while (position < length && jumps <= length)
    {
        uint8_t label_length = packet[position++];
        if (label_length == 0)
        {
            if (!return_position)
                return_position = position;
            output[written] = 0;
            *cursor = return_position;
            return 1;
        }
        if ((label_length & 0xc0U) == 0xc0U)
        {
            uint16_t pointer;
            if (position >= length)
                return 0;
            pointer = (uint16_t)(((label_length & 0x3fU) << 8) | packet[position++]);
            if (pointer >= length)
                return 0;
            if (!return_position)
                return_position = position;
            position = pointer;
            ++jumps;
            continue;
        }
        if ((label_length & 0xc0U) != 0 || position + label_length > length)
            return 0;
        if (written && written + 1 < capacity)
            output[written++] = '.';
        while (label_length--)
        {
            if (written + 1 < capacity)
                output[written++] = (char)packet[position];
            ++position;
        }
    }
    return 0;
}

static int ascii_ends_with(const char *text, const char *suffix)
{
    const size_t text_length = strlen(text);
    const size_t suffix_length = strlen(suffix);

    return text_length >= suffix_length && ascii_equal(text + text_length - suffix_length, suffix);
}

static moonlight_discovered_service_t *find_service(moonlight_discovered_service_t *services,
                                                    uint32_t *count, uint32_t capacity,
                                                    const char *instance)
{
    moonlight_discovered_service_t *service;
    const char *dot = strchr(instance, '.');
    const size_t label = dot ? (size_t)(dot - instance) : strlen(instance);
    uint32_t index;

    for (index = 0; index < *count; ++index)
    {
        if (ascii_equal(services[index].instance, instance))
            return &services[index];
    }
    if (*count >= capacity)
        return NULL;
    service = &services[(*count)++];
    memset(service, 0, sizeof(*service));
    snprintf(service->instance, sizeof(service->instance), "%s", instance);
    if (label)
        snprintf(service->name, sizeof(service->name), "%.*s", (int)label, instance);
    else
        snprintf(service->name, sizeof(service->name), "Sunshine PC");
    return service;
}

// Every Sunshine instance in one reply: the PTR answer names it and its SRV
// record, usually in the additional section, carries the port it listens on.
uint32_t moonlight_discovery_parse_response(const uint8_t *packet, size_t length,
                                            moonlight_discovered_service_t *services,
                                            uint32_t capacity)
{
    static const char service_type[] = "_nvstream._tcp.local";
    static const char instance_suffix[] = "._nvstream._tcp.local";
    char owner[128];
    char target[128];
    size_t cursor = DNS_HEADER_SIZE;
    uint32_t count = 0;
    uint32_t records;
    uint16_t questions;
    uint32_t index;

    if (!packet || !services || !capacity || length < DNS_HEADER_SIZE ||
        !(read_u16(packet + 2) & 0x8000U))
        return 0;
    questions = read_u16(packet + 4);
    records = (uint32_t)read_u16(packet + 6) + read_u16(packet + 8) + read_u16(packet + 10);
    for (index = 0; index < questions; ++index)
    {
        if (!read_name(packet, length, &cursor, owner, sizeof(owner)) || cursor + 4 > length)
            return count;
        cursor += 4;
    }
    for (index = 0; index < records; ++index)
    {
        moonlight_discovered_service_t *service;
        uint16_t type;
        uint16_t data_length;
        size_t data_cursor;

        if (!read_name(packet, length, &cursor, owner, sizeof(owner)) || cursor + 10 > length)
            return count;
        type = read_u16(packet + cursor);
        data_length = read_u16(packet + cursor + 8);
        cursor += 10;
        if (cursor + data_length > length)
            return count;
        if (type == DNS_PTR && ascii_equal(owner, service_type))
        {
            data_cursor = cursor;
            if (read_name(packet, length, &data_cursor, target, sizeof(target)) && target[0])
                (void)find_service(services, &count, capacity, target);
        }
        else if (type == DNS_SRV && data_length >= 6 && ascii_ends_with(owner, instance_suffix))
        {
            service = find_service(services, &count, capacity, owner);
            if (service)
                service->http_port = read_u16(packet + cursor + 4);
        }
        cursor += data_length;
    }
    return count;
}

static int add_host(moonlight_discovered_host_t *hosts, uint32_t *count, uint32_t capacity,
                    const net_sockaddr_in_t *source, const moonlight_discovered_service_t *service)
{
    const uint8_t *octets = (const uint8_t *)&source->address;
    char address[64];
    uint32_t index;

    snprintf(address, sizeof(address), "%u.%u.%u.%u", octets[0], octets[1], octets[2], octets[3]);
    for (index = 0; index < *count; ++index)
    {
        if (strcmp(hosts[index].address, address) != 0)
            continue;
        // A repeated reply may be the first to carry the port.
        if (!hosts[index].http_port)
            hosts[index].http_port = service->http_port;
        if (!service->http_port || hosts[index].http_port == service->http_port)
            return 0;
    }
    if (*count >= capacity)
        return 0;
    snprintf(hosts[*count].address, sizeof(hosts[*count].address), "%s", address);
    snprintf(hosts[*count].name, sizeof(hosts[*count].name), "%s",
             service->name[0] ? service->name : "Sunshine PC");
    hosts[*count].http_port = service->http_port;
    ++*count;
    return 1;
}

uint32_t moonlight_discover_hosts(moonlight_discovered_host_t *hosts, uint32_t capacity)
{
    static const uint8_t query[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
                                    0x00, 0x00, 0x09, '_',  'n',  'v',  's',  't',  'r',  'e',
                                    'a',  'm',  0x04, '_',  't',  'c',  'p',  0x05, 'l',  'o',
                                    'c',  'a',  'l',  0x00, 0x00, 0x0c, 0x80, 0x01};
    net_sockaddr_in_t destination = {sizeof(net_sockaddr_in_t), NET_AF_INET, 0,
                                     NET_IPV4(224, 0, 0, 251),  0,           {0}};
    net_epoll_event_t event = {};
    uint32_t count = 0;
    int nonblocking = 1;
    int socket;
    int epoll;
    int round;

    if (!hosts || capacity == 0)
        return 0;
    memset(hosts, 0, sizeof(*hosts) * capacity);
    destination.port = big_endian_u16(MDNS_PORT);
    socket = sceNetSocket("prosperolight-mdns", NET_AF_INET, NET_SOCK_DGRAM, NET_IPPROTO_UDP);
    if (socket < 0)
        return 0;
    (void)sceNetSetsockopt(socket, NET_SOL_SOCKET, NET_SO_NBIO, &nonblocking, sizeof(nonblocking));
    epoll = sceNetEpollCreate("prosperolight-mdns", 0);
    if (epoll < 0)
        goto done;
    event.events = NET_EPOLLIN;
    event.data.socket = socket;
    if (sceNetEpollControl(epoll, NET_EPOLL_CTL_ADD, socket, &event) < 0)
        goto done_epoll;
    if (sceNetSendto(socket, query, sizeof(query), 0, &destination, sizeof(destination)) < 0)
        goto done_epoll;

    for (round = 0; round < MDNS_WAIT_ROUNDS; ++round)
    {
        net_epoll_event_t ready;
        if (sceNetEpollWait(epoll, &ready, 1, MDNS_WAIT_US) <= 0)
            continue;
        for (;;)
        {
            uint8_t packet[1500];
            net_sockaddr_in_t source;
            uint32_t source_length = sizeof(source);
            moonlight_discovered_service_t services[MOONLIGHT_DISCOVERY_MAX_SERVICES];
            uint32_t service;
            uint32_t found;
            int received =
                sceNetRecvfrom(socket, packet, sizeof(packet), 0, &source, &source_length);
            if (received <= 0)
                break;
            if (source.family != NET_AF_INET)
                continue;
            found = moonlight_discovery_parse_response(packet, (size_t)received, services,
                                                       MOONLIGHT_DISCOVERY_MAX_SERVICES);
            for (service = 0; service < found; ++service)
                add_host(hosts, &count, capacity, &source, &services[service]);
        }
    }

done_epoll:
    sceNetEpollDestroy(epoll);
done:
    sceNetSocketClose(socket);
    return count;
}
