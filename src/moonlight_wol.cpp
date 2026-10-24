/*
 * ps5-native-app-boilerplate / ProsperoLight - pure Wake-on-LAN contract.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "moonlight_wol.hpp"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <limits.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#if defined(__FreeBSD__)
extern "C" int ps5_socket_close(int socket);
#define MOONLIGHT_WOL_CLOSE_SOCKET ps5_socket_close
#else
#define MOONLIGHT_WOL_CLOSE_SOCKET close
#endif

static_assert(MOONLIGHT_WOL_PACKET_BYTES == MOONLIGHT_WOL_MAC_BYTES * 16U + 6U,
              "Wake-on-LAN packet must be six FF bytes plus sixteen MAC repetitions");
static_assert(MOONLIGHT_WOL_MAX_PORTS == 7U, "Wake-on-LAN port derivation bound changed");

static int hex_digit(char character)
{
    if (character >= '0' && character <= '9')
        return character - '0';
    if (character >= 'a' && character <= 'f')
        return character - 'a' + 10;
    if (character >= 'A' && character <= 'F')
        return character - 'A' + 10;
    return -1;
}

static void add_unique_port(uint16_t *ports, size_t *count, uint32_t candidate)
{
    if (candidate > 65535U)
        return;
    const uint16_t port = (uint16_t)candidate;
    for (size_t index = 0; index < *count; ++index)
    {
        if (ports[index] == port)
            return;
    }
    ports[(*count)++] = port;
}

int moonlight_wol_mac_is_valid(const uint8_t mac[MOONLIGHT_WOL_MAC_BYTES])
{
    if (mac == NULL)
        return 0;

    int all_zero = 1;
    int all_broadcast = 1;
    for (size_t index = 0; index < MOONLIGHT_WOL_MAC_BYTES; ++index)
    {
        if (mac[index] != 0)
            all_zero = 0;
        if (mac[index] != 0xFFU)
            all_broadcast = 0;
    }
    if (all_zero || all_broadcast)
        return 0;
    if ((mac[0] & 0x01U) != 0)
        return 0;
    return 1;
}

int moonlight_wol_parse_mac(const char *text, uint8_t out_mac[MOONLIGHT_WOL_MAC_BYTES])
{
    if (text == NULL || out_mac == NULL)
        return 0;
    if (strlen(text) != 17U)
        return 0;

    const char separator = text[2];
    if (separator != ':' && separator != '-')
        return 0;
    for (size_t index = 0; index < MOONLIGHT_WOL_MAC_BYTES - 1U; ++index)
    {
        if (text[index * 3U + 2U] != separator)
            return 0;
    }

    uint8_t parsed[MOONLIGHT_WOL_MAC_BYTES];
    for (size_t index = 0; index < MOONLIGHT_WOL_MAC_BYTES; ++index)
    {
        const int high = hex_digit(text[index * 3U]);
        const int low = hex_digit(text[index * 3U + 1U]);
        if (high < 0 || low < 0)
            return 0;
        parsed[index] = (uint8_t)((high << 4) | low);
    }

    if (!moonlight_wol_mac_is_valid(parsed))
        return 0;
    memcpy(out_mac, parsed, MOONLIGHT_WOL_MAC_BYTES);
    return 1;
}

int moonlight_wol_build_packet(const uint8_t mac[MOONLIGHT_WOL_MAC_BYTES],
                               uint8_t out_packet[MOONLIGHT_WOL_PACKET_BYTES])
{
    if (out_packet == NULL || !moonlight_wol_mac_is_valid(mac))
        return 0;

    for (size_t index = 0; index < MOONLIGHT_WOL_MAC_BYTES; ++index)
        out_packet[index] = 0xFFU;
    for (size_t repetition = 0; repetition < 16U; ++repetition)
    {
        for (size_t index = 0; index < MOONLIGHT_WOL_MAC_BYTES; ++index)
            out_packet[MOONLIGHT_WOL_MAC_BYTES + repetition * MOONLIGHT_WOL_MAC_BYTES + index] =
                mac[index];
    }
    return 1;
}

size_t moonlight_wol_derive_ports(uint16_t http_base_port, uint16_t *out_ports, size_t capacity)
{
    uint16_t ports[MOONLIGHT_WOL_MAX_PORTS];
    size_t count = 0;

    add_unique_port(ports, &count, 9U);
    add_unique_port(ports, &count, 47009U);

    static const uint16_t offsets[] = {9U, 10U, 11U, 13U, 21U};
    for (size_t index = 0; index < sizeof(offsets) / sizeof(offsets[0]); ++index)
        add_unique_port(ports, &count, (uint32_t)http_base_port + offsets[index]);

    if (out_ports != NULL)
    {
        const size_t copied = count < capacity ? count : capacity;
        memcpy(out_ports, ports, copied * sizeof(uint16_t));
    }
    return count;
}

static void add_unique_address(uint32_t *addresses, size_t *count, uint32_t candidate)
{
    if (*count >= MOONLIGHT_WOL_MAX_ADDRESSES)
        return;
    for (size_t index = 0; index < *count; ++index)
    {
        if (addresses[index] == candidate)
            return;
    }
    addresses[(*count)++] = candidate;
}

int moonlight_wol_plan_targets(const uint32_t *required_addresses, size_t required_count,
                               const uint32_t *interface_broadcasts, size_t interface_count,
                               uint16_t http_base_port, moonlight_wol_target_plan *out_plan)
{
    if (out_plan == NULL)
        return 0;
    if ((required_addresses == NULL && required_count != 0) ||
        (interface_broadcasts == NULL && interface_count != 0))
        return 0;

    memset(out_plan, 0, sizeof(*out_plan));
    add_unique_address(out_plan->addresses, &out_plan->address_count, 0xFFFFFFFFU);
    for (size_t index = 0; index < required_count; ++index)
        add_unique_address(out_plan->addresses, &out_plan->address_count,
                           required_addresses[index]);
    for (size_t index = 0; index < interface_count; ++index)
        add_unique_address(out_plan->addresses, &out_plan->address_count,
                           interface_broadcasts[index]);

    out_plan->port_count =
        moonlight_wol_derive_ports(http_base_port, out_plan->ports, MOONLIGHT_WOL_MAX_PORTS);
    return out_plan->address_count > 0 && out_plan->port_count > 0;
}

static int default_resolve_ipv4(void *context, const char *host, uint32_t *out_addresses,
                                size_t capacity, size_t *out_count)
{
    (void)context;
    if (out_count == NULL)
        return 0;
    *out_count = 0;
    if (host == NULL || *host == '\0')
        return 1;
    if (out_addresses == NULL && capacity != 0)
        return 0;

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    struct addrinfo *results = NULL;
    if (getaddrinfo(host, NULL, &hints, &results) != 0)
        return 0;

    size_t count = 0;
    for (struct addrinfo *info = results; info != NULL; info = info->ai_next)
    {
        if (info->ai_family != AF_INET || info->ai_addr == NULL)
            continue;
        if (count < capacity)
        {
            const struct sockaddr_in *address = (const struct sockaddr_in *)info->ai_addr;
            out_addresses[count] = ntohl(address->sin_addr.s_addr);
        }
        ++count;
    }
    freeaddrinfo(results);
    *out_count = count < capacity ? count : capacity;
    return 1;
}

static int default_enumerate_interface_broadcasts(void *context, uint32_t *out_addresses,
                                                  size_t capacity, size_t *out_count)
{
    (void)context;
    if (out_count == NULL)
        return 0;
    *out_count = 0;
    if (out_addresses == NULL && capacity != 0)
        return 0;

    struct ifaddrs *interfaces = NULL;
    if (getifaddrs(&interfaces) != 0)
        return 0;

    size_t count = 0;
    for (struct ifaddrs *interface = interfaces; interface != NULL; interface = interface->ifa_next)
    {
        if (interface->ifa_addr == NULL || interface->ifa_broadaddr == NULL)
            continue;
        if (interface->ifa_addr->sa_family != AF_INET)
            continue;
        if ((interface->ifa_flags & IFF_UP) == 0 || (interface->ifa_flags & IFF_BROADCAST) == 0 ||
            (interface->ifa_flags & IFF_LOOPBACK) != 0)
            continue;
        if (interface->ifa_broadaddr->sa_family != AF_INET)
            continue;
        if (count < capacity)
        {
            const struct sockaddr_in *broadcast =
                (const struct sockaddr_in *)interface->ifa_broadaddr;
            out_addresses[count] = ntohl(broadcast->sin_addr.s_addr);
        }
        ++count;
    }
    freeifaddrs(interfaces);
    *out_count = count < capacity ? count : capacity;
    return 1;
}

static int default_open_udp_socket(void *context)
{
    (void)context;
    return socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
}

static int default_enable_broadcast(void *context, int socket_id)
{
    (void)context;
    int enabled = 1;
    return setsockopt(socket_id, SOL_SOCKET, SO_BROADCAST, &enabled, sizeof(enabled));
}

static int default_send_datagram(void *context, int socket_id, uint32_t address, uint16_t port,
                                 const uint8_t *packet, size_t packet_bytes)
{
    (void)context;
    struct sockaddr_in destination;
    memset(&destination, 0, sizeof(destination));
    destination.sin_family = AF_INET;
    destination.sin_port = htons(port);
    destination.sin_addr.s_addr = htonl(address);

    const ssize_t sent = sendto(socket_id, packet, packet_bytes, 0,
                                (const struct sockaddr *)&destination, sizeof(destination));
    if (sent < 0 || sent > INT_MAX)
        return -1;
    return (int)sent;
}

static int default_close_udp_socket(void *context, int socket_id)
{
    (void)context;
    return MOONLIGHT_WOL_CLOSE_SOCKET(socket_id);
}

static const moonlight_wol_send_operations default_send_operations = {
    default_resolve_ipv4,    default_enumerate_interface_broadcasts,
    default_open_udp_socket, default_enable_broadcast,
    default_send_datagram,   default_close_udp_socket,
};

int moonlight_wol_send_with_operations(const uint8_t mac[MOONLIGHT_WOL_MAC_BYTES], const char *host,
                                       uint16_t http_base_port,
                                       const moonlight_wol_send_operations *operations,
                                       void *context, moonlight_wol_send_result *out_result)
{
    if (out_result == NULL)
        return 0;
    memset(out_result, 0, sizeof(*out_result));
    out_result->error = MOONLIGHT_WOL_SEND_ERROR_INVALID_ARGUMENT;
    if (operations == NULL || operations->resolve_ipv4 == NULL ||
        operations->enumerate_interface_broadcasts == NULL || operations->open_udp_socket == NULL ||
        operations->enable_broadcast == NULL || operations->send_datagram == NULL ||
        operations->close_udp_socket == NULL)
        return 0;
    if (!moonlight_wol_mac_is_valid(mac))
    {
        out_result->error = MOONLIGHT_WOL_SEND_ERROR_INVALID_MAC;
        return 0;
    }

    uint8_t packet[MOONLIGHT_WOL_PACKET_BYTES];
    if (!moonlight_wol_build_packet(mac, packet))
    {
        out_result->error = MOONLIGHT_WOL_SEND_ERROR_INVALID_MAC;
        return 0;
    }

    uint32_t required_addresses[MOONLIGHT_WOL_MAX_ADDRESSES];
    size_t required_count = 0;
    if (host != NULL && *host != '\0')
    {
        struct in_addr literal;
        if (inet_pton(AF_INET, host, &literal) == 1)
        {
            required_addresses[0] = ntohl(literal.s_addr);
            required_count = 1;
        }
        else if (!operations->resolve_ipv4(context, host, required_addresses,
                                           MOONLIGHT_WOL_MAX_ADDRESSES, &required_count))
        {
            out_result->dns_failed = 1;
            required_count = 0;
        }
        else if (required_count == 0)
        {
            out_result->dns_failed = 1;
        }
        if (required_count > MOONLIGHT_WOL_MAX_ADDRESSES)
            required_count = MOONLIGHT_WOL_MAX_ADDRESSES;
    }

    uint32_t interface_broadcasts[MOONLIGHT_WOL_MAX_ADDRESSES];
    size_t interface_count = 0;
    if (!operations->enumerate_interface_broadcasts(context, interface_broadcasts,
                                                    MOONLIGHT_WOL_MAX_ADDRESSES, &interface_count))
    {
        out_result->interface_enumeration_failed = 1;
        interface_count = 0;
    }
    if (interface_count > MOONLIGHT_WOL_MAX_ADDRESSES)
        interface_count = MOONLIGHT_WOL_MAX_ADDRESSES;

    moonlight_wol_target_plan plan;
    if (!moonlight_wol_plan_targets(required_addresses, required_count, interface_broadcasts,
                                    interface_count, http_base_port, &plan))
    {
        out_result->error = MOONLIGHT_WOL_SEND_ERROR_INTERNAL;
        return 0;
    }

    const int socket_id = operations->open_udp_socket(context);
    if (socket_id < 0)
    {
        out_result->socket_failed = 1;
        out_result->error = MOONLIGHT_WOL_SEND_ERROR_SOCKET;
        return 0;
    }

    if (operations->enable_broadcast(context, socket_id) != 0)
    {
        out_result->broadcast_option_failed = 1;
        if (operations->close_udp_socket(context, socket_id) != 0)
            out_result->close_failed = 1;
        out_result->error = MOONLIGHT_WOL_SEND_ERROR_BROADCAST_OPTION;
        return 0;
    }

    for (size_t address_index = 0; address_index < plan.address_count; ++address_index)
    {
        for (size_t port_index = 0; port_index < plan.port_count; ++port_index)
        {
            ++out_result->attempted_datagrams;
            const int sent = operations->send_datagram(
                context, socket_id, plan.addresses[address_index], plan.ports[port_index], packet,
                MOONLIGHT_WOL_PACKET_BYTES);
            if (sent == (int)MOONLIGHT_WOL_PACKET_BYTES)
                ++out_result->successful_datagrams;
            else
                ++out_result->failed_datagrams;
        }
    }

    if (operations->close_udp_socket(context, socket_id) != 0)
        out_result->close_failed = 1;

    if (out_result->successful_datagrams == 0)
        out_result->error = MOONLIGHT_WOL_SEND_ERROR_ALL_DATAGRAMS_FAILED;
    else if (out_result->failed_datagrams != 0 || out_result->dns_failed ||
             out_result->interface_enumeration_failed || out_result->close_failed)
        out_result->error = MOONLIGHT_WOL_SEND_ERROR_PARTIAL;
    else
        out_result->error = MOONLIGHT_WOL_SEND_ERROR_NONE;

    return out_result->successful_datagrams > 0;
}

int moonlight_wol_send(const uint8_t mac[MOONLIGHT_WOL_MAC_BYTES], const char *host,
                       uint16_t http_base_port, moonlight_wol_send_result *out_result)
{
    return moonlight_wol_send_with_operations(mac, host, http_base_port, &default_send_operations,
                                              NULL, out_result);
}
