/*
 * ps5-native-app-boilerplate - One-request elevation helper.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "../../../src/elevation/protocol.hpp"

#include <array>
#include <cstring>

extern "C"
{
#include <ps5/kernel.h>
#include <ps5/klog.h>
#include <ps5/payload.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
}

namespace
{
using elevation::Capability;
using elevation::Status;
using elevation::wire::Kind;
using elevation::wire::Message;

constexpr char target_title_id[] = "PPSA99002";

struct AppInfo
{
    std::uint32_t app_id;
    std::uint64_t unknown1;
    char title_id[14];
    char unknown2[0x3c];
};
extern "C" int sceKernelGetAppInfo(pid_t pid, AppInfo *info);

struct Target
{
    std::intptr_t process = 0;
    std::intptr_t ucred = 0;
    std::intptr_t filedesc = 0;
};

struct State
{
    // uid, ruid, svuid, ngroups, rgid, svgid; the SDK's supported ucred layout.
    std::array<std::uint32_t, 6> identity{};
    std::uint64_t authority = 0;
    std::array<std::uint8_t, 16> caps{};
    std::array<std::uint8_t, 32> attrs{};
    std::intptr_t root = 0;
    std::intptr_t jail = 0;
    bool operator==(const State &) const = default;
};

bool kernel_pointer(std::intptr_t pointer) noexcept
{
    return (static_cast<std::uint64_t>(pointer) >> 48) == 0xffff;
}

bool find_target(std::uint32_t pid, Target &target) noexcept
{
    AppInfo info{};
    if (sceKernelGetAppInfo(static_cast<pid_t>(pid), &info) != 0 ||
        std::memcmp(info.title_id, target_title_id, sizeof(target_title_id)) != 0)
        return false;

    // Walk afresh instead of reusing the SDK's PID cache after the handshake.
    std::intptr_t process = 0;
    if (kernel_copyout(KERNEL_ADDRESS_ALLPROC, &process, sizeof(process)) != 0)
        return false;
    for (unsigned guard = 0; process != 0 && guard < 4096; ++guard)
    {
        std::uint32_t candidate = 0;
        if (!kernel_pointer(process) ||
            kernel_copyout(process + KERNEL_OFFSET_PROC_P_PID, &candidate, sizeof(candidate)) != 0)
            return false;
        if (candidate == pid)
        {
            target.process = process;
            return kernel_copyout(process + KERNEL_OFFSET_PROC_P_UCRED, &target.ucred,
                                  sizeof(target.ucred)) == 0 &&
                   kernel_copyout(process + KERNEL_OFFSET_PROC_P_FD, &target.filedesc,
                                  sizeof(target.filedesc)) == 0 &&
                   kernel_pointer(target.ucred) && kernel_pointer(target.filedesc);
        }
        if (kernel_copyout(process, &process, sizeof(process)) != 0)
            return false;
    }
    return false;
}

bool read_state(const Target &target, State &state) noexcept
{
    return kernel_copyout(target.ucred + KERNEL_OFFSET_UCRED_CR_UID, state.identity.data(),
                          sizeof(state.identity)) == 0 &&
           kernel_copyout(target.ucred + KERNEL_OFFSET_UCRED_CR_SCEAUTHID, &state.authority,
                          sizeof(state.authority)) == 0 &&
           kernel_copyout(target.ucred + KERNEL_OFFSET_UCRED_CR_SCECAPS, state.caps.data(),
                          state.caps.size()) == 0 &&
           kernel_copyout(target.ucred + KERNEL_OFFSET_UCRED_CR_SCEATTRS, state.attrs.data(),
                          state.attrs.size()) == 0 &&
           kernel_copyout(target.filedesc + KERNEL_OFFSET_FILEDESC_FD_RDIR, &state.root,
                          sizeof(state.root)) == 0 &&
           kernel_copyout(target.filedesc + KERNEL_OFFSET_FILEDESC_FD_JDIR, &state.jail,
                          sizeof(state.jail)) == 0;
}

bool write_state(const Target &target, const State &state) noexcept
{
    // Attempt every field, including during rollback after a partial write.
    int failures = 0;
    failures += kernel_copyin(state.identity.data(), target.ucred + KERNEL_OFFSET_UCRED_CR_UID,
                              sizeof(state.identity)) != 0;
    failures += kernel_copyin(&state.authority, target.ucred + KERNEL_OFFSET_UCRED_CR_SCEAUTHID,
                              sizeof(state.authority)) != 0;
    failures += kernel_copyin(state.caps.data(), target.ucred + KERNEL_OFFSET_UCRED_CR_SCECAPS,
                              state.caps.size()) != 0;
    failures += kernel_copyin(state.attrs.data(), target.ucred + KERNEL_OFFSET_UCRED_CR_SCEATTRS,
                              state.attrs.size()) != 0;
    failures += kernel_copyin(&state.root, target.filedesc + KERNEL_OFFSET_FILEDESC_FD_RDIR,
                              sizeof(state.root)) != 0;
    failures += kernel_copyin(&state.jail, target.filedesc + KERNEL_OFFSET_FILEDESC_FD_JDIR,
                              sizeof(state.jail)) != 0;
    return failures == 0;
}

Status grant_filesystem(const Target &target, const State &original) noexcept
{
    State desired = original;
    // /data is already mounted inside the title jail. Grant filesystem UID/GID
    // access without changing root/jail or SCE module-loading credentials.
    // Lazy loads (VdecCore/Savc/Shevc, HID) need the original library namespace.
    desired.identity.fill(0);

    State verified{};
    if (write_state(target, desired) && read_state(target, verified) && verified == desired)
        return Status::ok;
    if (write_state(target, original) && read_state(target, verified) && verified == original)
        return Status::apply_failed;
    return Status::rollback_failed;
}

bool send_message(const Message &message) noexcept
{
    return elevation::wire::transfer(reinterpret_cast<const std::uint8_t *>(&message),
                                     sizeof(message), [](const auto *bytes, std::size_t size)
                                     { return write(STDOUT_FILENO, bytes, size); });
}

bool receive_message(Message &message) noexcept
{
    return elevation::wire::transfer(reinterpret_cast<std::uint8_t *>(&message), sizeof(message),
                                     [](auto *bytes, std::size_t size)
                                     { return read(STDIN_FILENO, bytes, size); });
}

Status handle_request(const Message &request) noexcept
{
    if (const auto error = elevation::wire::validate(request); error != Status::ok)
        return error;
    if (request.kind != Kind::request || request.status != Status::ok)
        return Status::invalid_request;
    const auto *args = payload_get_args();
    if (args == nullptr || args->kdata_base_addr == 0)
        return Status::unavailable;

    Target before{};
    State original{};
    if (!find_target(request.pid, before))
        return Status::target_mismatch;
    if (!read_state(before, original))
        return Status::unavailable;
    Message prepare = request;
    prepare.kind = Kind::prepare;
    Message prepared{};
    if (!send_message(prepare) || !receive_message(prepared))
        return Status::transport_error;
    if (!elevation::wire::matches(prepared, request, Kind::prepared))
        return Status::invalid_request;
    if (prepared.status != Status::ok)
        return Status::prepare_failed;

    Target target{};
    State cloned{};
    if (!find_target(request.pid, target) || target.process != before.process ||
        target.filedesc != before.filedesc)
        return Status::target_mismatch;
    // Never write into the preexisting, potentially shared credential. Do not
    // copy cr_prison or swap credential pointers ourselves.
    if (target.ucred == before.ucred || !read_state(target, cloned) || cloned != original)
        return Status::prepare_failed;

    switch (request.capability)
    {
    case Capability::filesystem:
        return grant_filesystem(target, original);
    default:
        return Status::unsupported_capability;
    }
}
} // namespace

int main()
{
    const timeval timeout{elevation::wire::io_timeout_us / 1'000'000,
                          elevation::wire::io_timeout_us % 1'000'000};
    if (setsockopt(STDIN_FILENO, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0 ||
        setsockopt(STDOUT_FILENO, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) != 0)
        return 1;
    Message request{};
    if (!receive_message(request))
        return 1;
    const auto result = handle_request(request);
    Message response{};
    response.kind = Kind::response;
    response.capability = request.capability;
    response.pid = request.pid;
    response.status = result;
    klog_printf("[sandbox-elevator] fw=%08x pid=%u capability=%u result=%u\n",
                kernel_get_fw_version(), request.pid, static_cast<unsigned>(request.capability),
                static_cast<unsigned>(result));
    return send_message(response) && result == Status::ok ? 0 : 1;
}
