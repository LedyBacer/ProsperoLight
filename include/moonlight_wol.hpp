/*
 * ps5-native-app-boilerplate / ProsperoLight - pure Wake-on-LAN contract.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This header is intentionally C-compatible so it can be consumed from
 * src/gamestream/client.c as well as C++ host code. It contains no socket,
 * persistence, backend, worker, or UI behavior.
 */

#ifndef MOONLIGHT_WOL_HPP
#define MOONLIGHT_WOL_HPP

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define MOONLIGHT_WOL_MAC_BYTES 6U
#define MOONLIGHT_WOL_PACKET_BYTES 102U
#define MOONLIGHT_WOL_MAX_PORTS 7U
#define MOONLIGHT_WOL_DEFAULT_HTTP_PORT 47989U
#define MOONLIGHT_WOL_MAX_ADDRESSES 32U
#define MOONLIGHT_WOL_MAX_DATAGRAMS (MOONLIGHT_WOL_MAX_ADDRESSES * MOONLIGHT_WOL_MAX_PORTS)

    /*
     * A deterministic IPv4 target/port plan. Addresses are host-order IPv4
     * values. Limited broadcast 255.255.255.255 is always the first address.
     * Required resolved/configured addresses and interface-directed broadcasts
     * follow in input order after deduplication. Ports use the Step 01 order.
     */
    typedef struct moonlight_wol_target_plan
    {
        uint32_t addresses[MOONLIGHT_WOL_MAX_ADDRESSES];
        size_t address_count;
        uint16_t ports[MOONLIGHT_WOL_MAX_PORTS];
        size_t port_count;
    } moonlight_wol_target_plan;

    /*
     * Parse a strict text MAC into six bytes.
     *
     * Separator policy: exactly six two-hex-digit octets separated by five
     * separators. The separator must be the same character throughout and may be
     * either ':' or '-'. Whitespace, mixed separators, leading/trailing junk,
     * wrong octet counts, and bad hexadecimal digits are rejected. All-zero,
     * broadcast, and multicast values are rejected by the MAC validity rules.
     *
     * Returns 1 and writes out_mac only on success; returns 0 on any rejection and
     * leaves out_mac untouched.
     */
    int moonlight_wol_parse_mac(const char *text, uint8_t out_mac[MOONLIGHT_WOL_MAC_BYTES]);

    /*
     * Validate six binary MAC bytes. All-zero, broadcast, and multicast values are
     * invalid. Locally administered unicast values remain valid.
     *
     * Returns 1 for a valid MAC and 0 otherwise.
     */
    int moonlight_wol_mac_is_valid(const uint8_t mac[MOONLIGHT_WOL_MAC_BYTES]);

    /*
     * Build the exact 102-byte magic packet: six 0xFF bytes followed by sixteen
     * repetitions of the six-byte MAC.
     *
     * Returns 1 and writes out_packet only when the MAC is valid; returns 0 for a
     * null output or invalid MAC.
     */
    int moonlight_wol_build_packet(const uint8_t mac[MOONLIGHT_WOL_MAC_BYTES],
                                   uint8_t out_packet[MOONLIGHT_WOL_PACKET_BYTES]);

    /*
     * Derive the bounded, deduplicated destination ports for a Sunshine HTTP base
     * port. Static ports 9 and 47009 are followed by base+9, base+10, base+11,
     * base+13, and base+21. Values above 65535 are skipped, never wrapped. Port 7
     * is not used.
     *
     * Returns the total number of unique ports derived. If out_ports is non-null,
     * at most capacity entries are written in derivation order.
     */
    size_t moonlight_wol_derive_ports(uint16_t http_base_port, uint16_t *out_ports,
                                      size_t capacity);

    /*
     * Build a deterministic IPv4 target/port plan. Required addresses are the
     * configured IPv4 literal or resolved IPv4 results. Interface broadcasts are
     * active, broadcast-capable, non-loopback IPv4 interface broadcast addresses.
     * Limited broadcast is always retained as the runtime fallback. The function
     * deduplicates addresses, derives and deduplicates ports, and never guesses a
     * subnet. Returns 1 on success and 0 for a null/invalid plan or input array.
     */
    int moonlight_wol_plan_targets(const uint32_t *required_addresses, size_t required_count,
                                   const uint32_t *interface_broadcasts, size_t interface_count,
                                   uint16_t http_base_port, moonlight_wol_target_plan *out_plan);

    typedef enum moonlight_wol_send_error
    {
        MOONLIGHT_WOL_SEND_ERROR_NONE = 0,
        MOONLIGHT_WOL_SEND_ERROR_INVALID_MAC,
        MOONLIGHT_WOL_SEND_ERROR_INVALID_ARGUMENT,
        MOONLIGHT_WOL_SEND_ERROR_DNS,
        MOONLIGHT_WOL_SEND_ERROR_SOCKET,
        MOONLIGHT_WOL_SEND_ERROR_BROADCAST_OPTION,
        MOONLIGHT_WOL_SEND_ERROR_ALL_DATAGRAMS_FAILED,
        MOONLIGHT_WOL_SEND_ERROR_PARTIAL,
        MOONLIGHT_WOL_SEND_ERROR_INTERNAL
    } moonlight_wol_send_error;

    /*
     * Structured sender result. A complete datagram is exactly 102 bytes.
     * Successful_datagrams > 0 means wake packets were emitted; it never proves
     * the PC woke. Error is NONE when every planned datagram completed, PARTIAL
     * when at least one completed and any diagnostic remained, and a specific
     * failure category when no datagram completed.
     */
    typedef struct moonlight_wol_send_result
    {
        size_t attempted_datagrams;
        size_t successful_datagrams;
        size_t failed_datagrams;
        int dns_failed;
        int interface_enumeration_failed;
        int socket_failed;
        int broadcast_option_failed;
        int close_failed;
        moonlight_wol_send_error error;
    } moonlight_wol_send_result;

    /*
     * Injected transport operations. Addresses are host-order IPv4 values.
     *
     * resolve_ipv4: return 1 on success, 0 on DNS/resolution failure; write at most
     * capacity addresses and set out_count.
     * enumerate_interface_broadcasts: return 1 on success, 0 on failure; a zero
     * count is valid.
     * open_udp_socket: return a non-negative socket on success, negative on failure.
     * enable_broadcast: return 0 on success, nonzero on failure.
     * send_datagram: return bytes sent on success, negative on failure. Only exactly
     * MOONLIGHT_WOL_PACKET_BYTES counts as a complete successful datagram.
     * close_udp_socket: return 0 on success, nonzero on failure.
     */
    typedef struct moonlight_wol_send_operations
    {
        int (*resolve_ipv4)(void *context, const char *host, uint32_t *out_addresses,
                            size_t capacity, size_t *out_count);
        int (*enumerate_interface_broadcasts)(void *context, uint32_t *out_addresses,
                                              size_t capacity, size_t *out_count);
        int (*open_udp_socket)(void *context);
        int (*enable_broadcast)(void *context, int socket);
        int (*send_datagram)(void *context, int socket, uint32_t address, uint16_t port,
                             const uint8_t *packet, size_t packet_bytes);
        int (*close_udp_socket)(void *context, int socket);
    } moonlight_wol_send_operations;

    /*
     * Send the exact magic packet to every deduplicated target/port pair using
     * injected operations. The configured IPv4 literal bypasses resolve_ipv4; a
     * hostname uses it. DNS or interface-enumeration failure never suppresses the
     * limited broadcast. The sender makes one pass with no sleep or retry loop.
     * Returns 1 when at least one complete datagram was emitted, otherwise 0.
     */
    int moonlight_wol_send_with_operations(const uint8_t mac[MOONLIGHT_WOL_MAC_BYTES],
                                           const char *host, uint16_t http_base_port,
                                           const moonlight_wol_send_operations *operations,
                                           void *context, moonlight_wol_send_result *out_result);

    /*
     * Production sender using the existing POSIX/BSD socket path. It emits IPv4
     * UDP only, closes every created socket, and never sleeps or retries. Returns
     * 1 when at least one complete datagram was emitted; this does not prove wake.
     */
    int moonlight_wol_send(const uint8_t mac[MOONLIGHT_WOL_MAC_BYTES], const char *host,
                           uint16_t http_base_port, moonlight_wol_send_result *out_result);

#ifdef __cplusplus
}
#endif

#endif
