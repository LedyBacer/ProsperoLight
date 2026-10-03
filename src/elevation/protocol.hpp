/*
 * ps5-native-app-boilerplate - Versioned elfldr elevation messages.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>

namespace elevation
{
enum class Capability : std::uint32_t
{
    filesystem = 1,
};

enum class Status : std::uint32_t
{
    ok = 0,
    invalid_request = 1,
    unsupported_version = 2,
    unsupported_capability = 3,
    target_mismatch = 4,
    unavailable = 5,
    prepare_failed = 6,
    apply_failed = 7,
    rollback_failed = 8,
    transport_error = 9,
    protocol_error = 10,
};

namespace wire
{
// Build-time tuning for slower consoles; both endpoints use the same timeout.
inline constexpr int io_timeout_us = 5'000'000;

enum class Kind : std::uint32_t
{
    request = 1,
    prepare = 2,
    prepared = 3,
    response = 4,
};

// Fixed-width little-endian ABI. No pointers, offsets, or privilege masks cross the socket.
struct Message
{
    std::uint32_t magic = 0x31564c45; // "ELV1"
    std::uint16_t version = 1;
    std::uint16_t size = 24;
    Kind kind = Kind::request;
    Capability capability = Capability::filesystem;
    std::uint32_t pid = 0;
    Status status = Status::ok;
};
static_assert(sizeof(Message) == 24);
static_assert(std::endian::native == std::endian::little);

constexpr Status validate(const Message &message) noexcept
{
    if (message.magic != Message{}.magic || message.size != sizeof(Message))
        return Status::invalid_request;
    if (message.version != Message{}.version)
        return Status::unsupported_version;
    if (message.kind < Kind::request || message.kind > Kind::response || message.pid == 0 ||
        message.pid > INT32_MAX || message.status > Status::protocol_error)
        return Status::invalid_request;
    if (message.capability != Capability::filesystem)
        return Status::unsupported_capability;
    return Status::ok;
}

constexpr bool matches(const Message &message, const Message &request, Kind kind) noexcept
{
    return validate(message) == Status::ok && message.kind == kind && message.pid == request.pid &&
           message.capability == request.capability;
}

// Both transports use this loop: TCP can split even a 24-byte frame. A timeout,
// interruption, EOF, or failed operation ends this attempt; there is no retry loop.
template <typename Byte, typename Operation>
bool transfer(Byte *bytes, std::size_t size, Operation operation) noexcept
{
    while (size != 0)
    {
        const auto count = operation(bytes, size);
        if (count <= 0 || static_cast<std::size_t>(count) > size)
            return false;
        bytes += count;
        size -= static_cast<std::size_t>(count);
    }
    return true;
}
} // namespace wire
} // namespace elevation
