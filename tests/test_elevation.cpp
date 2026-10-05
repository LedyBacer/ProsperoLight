/*
 * ps5-native-app-boilerplate - ProsperoLight Lapy one-host elevation regression.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstdarg>
#include <cstring>
#include <string>
#include <string_view>

#include "../src/elevation/elevation.cpp"

namespace test
{
struct State
{
    unsigned data_failures{};
    unsigned data_opens{};
    unsigned sleeps{};
    unsigned sockets{};
    unsigned receives{};
    std::size_t helper_offset{};
    std::string data;
    std::string sent;
};

State state;

void reset(unsigned failures = 0)
{
    state = {};
    state.data_failures = failures;
}
} // namespace test

extern "C"
{
    pid_t getpid() noexcept
    {
        return 4242;
    }
    uid_t geteuid() noexcept
    {
        return 1000;
    }
    int seteuid(uid_t) noexcept
    {
        return 0;
    }

    int open(const char *path, int, ...)
    {
        const std::string_view name{path};
        if (name == "/download0/lapy_owned_result")
            return 10;
        if (name.starts_with("/download0/.elevate_proc."))
            return 11;
        if (name.starts_with("/data/.lapy_probe_"))
        {
            if (++test::state.data_opens <= test::state.data_failures)
            {
                errno = EACCES;
                return -1;
            }
            return 12;
        }
        if (name == "/app0/lapy.elf")
            return 13;
        errno = ENOENT;
        return -1;
    }

    ssize_t write(int descriptor, const void *buffer, size_t size)
    {
        if (descriptor == 12)
            test::state.data.append(static_cast<const char *>(buffer), size);
        return static_cast<ssize_t>(size);
    }

    ssize_t read(int descriptor, void *buffer, size_t size)
    {
        if (descriptor == 12)
        {
            const auto count = std::min(size, test::state.data.size());
            std::memcpy(buffer, test::state.data.data(), count);
            return static_cast<ssize_t>(count);
        }
        if (descriptor == 13)
        {
            constexpr std::string_view helper{"ELF!"};
            const auto count = std::min(size, helper.size() - test::state.helper_offset);
            std::memcpy(buffer, helper.data() + test::state.helper_offset, count);
            test::state.helper_offset += count;
            return static_cast<ssize_t>(count);
        }
        errno = EBADF;
        return -1;
    }

    off_t lseek(int descriptor, off_t offset, int whence) noexcept
    {
        return descriptor == 12 && offset == 0 && whence == SEEK_SET ? 0 : -1;
    }
    int close(int)
    {
        return 0;
    }
    int fchmod(int, mode_t) noexcept
    {
        return 0;
    }
    int rename(const char *, const char *) noexcept
    {
        return 0;
    }
    int unlink(const char *) noexcept
    {
        return 0;
    }
    int usleep(useconds_t)
    {
        ++test::state.sleeps;
        return 0;
    }
    int sceNetSocket(const char *, int, int, int)
    {
        ++test::state.sockets;
        return 20;
    }
    int sceNetSetsockopt(int, int, int, const void *, std::uint32_t)
    {
        return 0;
    }
    int sceNetConnect(int, const void *, std::uint32_t)
    {
        return 0;
    }
    int sceNetSend(int, const void *data, std::size_t size, int)
    {
        test::state.sent.append(static_cast<const char *>(data), size);
        return static_cast<int>(size);
    }
    int sceNetRecv(int, void *data, std::size_t size, int)
    {
        elevation::wire::Message reply{};
        reply.pid = 4242;
        reply.kind = test::state.receives++ == 0 ? elevation::wire::Kind::prepare
                                                 : elevation::wire::Kind::response;
        const auto count = std::min(size, sizeof(reply));
        std::memcpy(data, &reply, count);
        return static_cast<int>(count);
    }
    int sceNetSocketClose(int)
    {
        return 0;
    }
}

int main()
{
    using elevation::Capability;
    using elevation::Status;
    test::reset();
    assert(elevation::request(Capability::filesystem) == Status::ok);
    assert(std::string_view{elevation::path()} == "existing");
    assert(test::state.sockets == 0);
    test::reset(1);
    assert(elevation::request(Capability::filesystem) == Status::ok);
    assert(std::string_view{elevation::path()} == "resident");
    assert(test::state.sockets == 0);
    test::reset(51);
    assert(elevation::request(Capability::filesystem) == Status::ok);
    assert(std::string_view{elevation::path()} == "helper");
    assert(test::state.sockets == 1 && test::state.receives == 2);
    assert(test::state.sent.starts_with("ELF!"));
    test::reset();
    assert(elevation::request(static_cast<Capability>(2)) == Status::unsupported_capability);
}
