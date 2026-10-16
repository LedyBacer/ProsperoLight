/*
 * ps5-native-app-boilerplate - Native DualSense and multi-controller input.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ps5_dualsense.hpp"
#include "ps5_waveform.hpp"
#include <unistd.h>
#include "lan_http_report.hpp"
#include <Limelight.h>
#include <ControllerHaptics.h>
#include <pthread.h>
#include <cmath>
#include <cstdio>
#include <cstring>

extern "C"
{
    int32_t sceUserServiceGetLoginUserIdList(void *list);
    int32_t scePadOpen(int32_t user, int32_t type, int32_t index, const void *params);
    int32_t scePadClose(int32_t handle);
    int32_t scePadRead(int32_t handle, void *samples, int32_t capacity);
    int32_t scePadReadState(int32_t handle, void *sample);
    int32_t scePadSetVibration(int32_t handle, const void *param);
    int32_t scePadSetVibrationMode(int32_t handle, int32_t mode);
    int32_t scePadSetLightBar(int32_t handle, const void *param);
    int32_t scePadResetLightBar(int32_t handle);
    int32_t scePadSetMotionSensorState(int32_t handle, bool enabled);
    int32_t scePadSetAngularVelocityDeadbandState(int32_t handle, bool enabled);
    int32_t scePadGetControllerInformation(int32_t handle, void *info);
    int32_t scePadSetTriggerEffect(int32_t handle, const void *param);
    uint64_t PltGetMicroseconds(void);
}
namespace prosperolight::dualsense
{
namespace
{
// These compact ABI descriptions are derived from public duaLib/OpenOrbis
// sources, not proprietary SDK headers. AudioOut PCM ownership is isolated
// on per-controller workers in ps5_waveform.cpp.
struct TriggerCommand
{
    uint32_t mode;
    uint8_t reserved[4], data[48];
};
struct TriggerParam
{
    uint8_t mask, reserved[7];
    TriggerCommand commands[2];
};
static_assert(sizeof(TriggerParam) == 120);
struct PadInfo
{
    float density;
    uint16_t width, height;
    uint8_t deadzone_left, deadzone_right, connection_type, generation;
    uint8_t connected, reserved[3];
    int32_t device_class;
    uint8_t remaining[8];
};
static_assert(sizeof(PadInfo) == 28);
enum class VibrationMode
{
    Unknown,
    Native,
    Legacy
};
struct Slot
{
    int32_t user = -1, handle = -1;
    bool connected = false, announced = false, intercepted = false;
    uint8_t generation = 0;
    bool generation_valid = false;
    uint16_t width = 1920, height = 1080;
    PadSample previous{};
    uint16_t motion_rate[2]{};
    uint64_t motion_next[2]{}, last_input = 0;
    uint32_t local_buttons = 0;
    // What the host last received for this pad; unchanged input is not resent.
    bool sent_valid = false;
    int sent_buttons = 0;
    uint8_t sent_triggers[2]{};
    int16_t sent_axes[4]{};
    uint8_t rumble[2]{}, color[3]{};
    bool rumble_dirty = false, led_dirty = false, trigger_dirty = false;
    TriggerParam triggers{};
    bool sensor_enabled = false, sensor_dirty = false;
    bool native_active = false;
    VibrationMode vibration_mode = VibrationMode::Unknown;
    uint64_t native_pending_until = 0;
    uint64_t input_events = 0, output_errors = 0;
    uint64_t rumble_requests = 0, trigger_requests = 0, led_requests = 0;
    bool failure_logged = false;
};
Slot slots[MaxControllers];
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
bool initialized = false, live = false;
uint64_t next_discovery = 0, next_report = 0;
uint16_t active_mask = 0;
// Hosts add and remove controllers from the mask alone, so a packet names
// only the controllers whose arrival the host has already received.
uint16_t WireMask()
{
    uint16_t mask = 0;
    for (unsigned i = 0; i < MaxControllers; ++i)
        if (slots[i].announced)
            mask |= static_cast<uint16_t>(1u << i);
    return mask;
}
Statistics statistics{};
unsigned local_actions = 0;
struct Lock
{
    Lock()
    {
        pthread_mutex_lock(&mutex);
    }
    ~Lock()
    {
        pthread_mutex_unlock(&mutex);
    }
};
uint64_t Now()
{
    return PltGetMicroseconds();
}
void OutputResult(Slot &slot, const char *operation, int result)
{
    if (result >= 0)
        return;
    ++slot.output_errors;
    if (!slot.failure_logged)
    {
        char line[192];
        snprintf(line, sizeof(line), "DualSense user=%08x %s failed=%08x", slot.user, operation,
                 result);
        lan_http_report_text(line);
        slot.failure_logged = true;
    }
}
uint32_t SupportedButtons()
{
    return UP_FLAG | DOWN_FLAG | LEFT_FLAG | RIGHT_FLAG | A_FLAG | B_FLAG | X_FLAG | Y_FLAG |
           LB_FLAG | RB_FLAG | BACK_FLAG | PLAY_FLAG | LS_CLK_FLAG | RS_CLK_FLAG | TOUCHPAD_FLAG |
           SPECIAL_FLAG;
}
constexpr uint16_t Capabilities = LI_CCAP_ANALOG_TRIGGERS | LI_CCAP_RUMBLE |
                                  LI_CCAP_TRIGGER_RUMBLE | LI_CCAP_TOUCHPAD | LI_CCAP_ACCEL |
                                  LI_CCAP_GYRO | LI_CCAP_RGB_LED;
void CancelTouches(unsigned index)
{
    Slot &slot = slots[index];
    if (slot.announced && slot.previous.touch_count &&
        (LiGetHostFeatureFlags() & LI_FF_CONTROLLER_TOUCH_EVENTS))
        LiSendControllerTouchEvent(index, LI_TOUCH_EVENT_CANCEL_ALL, 0, 0, 0, 0);
    slot.previous.touch_count = 0;
}
void ResetOutput(Slot &slot)
{
    if (slot.handle < 0)
        return;
    uint8_t vibration[2]{};
    TriggerParam off{};
    off.mask = 3;
    if (!waveform::Active(static_cast<unsigned>(&slot - slots)))
    {
        const int mode = scePadSetVibrationMode(slot.handle, 2);
        OutputResult(slot, "restore rumble mode", mode);
        slot.vibration_mode = mode >= 0 ? VibrationMode::Legacy : VibrationMode::Unknown;
        OutputResult(slot, "reset rumble", scePadSetVibration(slot.handle, vibration));
    }
    OutputResult(slot, "reset triggers", scePadSetTriggerEffect(slot.handle, &off));
    OutputResult(slot, "reset lightbar", scePadResetLightBar(slot.handle));
    OutputResult(slot, "stop motion", scePadSetMotionSensorState(slot.handle, false));
    slot.rumble_dirty = slot.led_dirty = slot.trigger_dirty = slot.sensor_dirty = false;
    slot.motion_rate[0] = slot.motion_rate[1] = 0;
    slot.native_active = false;
    slot.sensor_enabled = false;
}
int Disconnect(unsigned index)
{
    Slot &slot = slots[index];
    active_mask &= ~(1u << index);
    CancelTouches(index);
    int result = 0;
    if (live && slot.announced)
    {
        result =
            LiSendMultiControllerEvent(index, WireMask() & ~(1u << index), 0, 0, 0, 0, 0, 0, 0);
        ++statistics.removals;
        if (result != 0)
            ++statistics.send_errors;
    }
    waveform::Disable(index);
    ResetOutput(slot);
    slot.connected = slot.announced = slot.sent_valid = false;
    slot.last_input = 0;
    slot.local_buttons = 0;
    return result;
}
void SetConnection(unsigned index, const PadSample &sample)
{
    Slot &slot = slots[index];
    if (!sample.connected)
    {
        if (slot.connected)
            Disconnect(index);
        return;
    }
    if (slot.generation_valid && slot.generation != sample.connected_count && slot.connected)
        Disconnect(index);
    slot.generation_valid = true;
    slot.generation = sample.connected_count;
    if (!slot.connected)
    {
        slot.connected = true;
        // Public duaLib constants: haptics mode 1, legacy motor emulation 2.
        const int mode = scePadSetVibrationMode(slot.handle, 1);
        OutputResult(slot, "prepare native haptics mode", mode);
        slot.vibration_mode = mode >= 0 ? VibrationMode::Native : VibrationMode::Unknown;
        if (mode >= 0)
            waveform::Configure(index, slot.user);
        else
            waveform::Disable(index);
        slot.native_pending_until = Now() + 50000;
        active_mask |= 1u << index;
        const unsigned count = __builtin_popcount(active_mask);
        if (count > statistics.peak)
            statistics.peak = count;
        alignas(8) uint8_t info_buffer[256]{};
        PadInfo info{};
        const int info_result = scePadGetControllerInformation(slot.handle, info_buffer);
        memcpy(&info, info_buffer, sizeof(info));
        if (info_result >= 0 && info.width && info.height)
        {
            slot.width = info.width;
            slot.height = info.height;
        }
        char line[192];
        snprintf(line, sizeof(line), "DualSense slot=%u user=%08x connected mask=%x touch=%ux%u",
                 index, slot.user, active_mask, slot.width, slot.height);
        lan_http_report_text(line);
    }
}
int Announce(unsigned index)
{
    Slot &slot = slots[index];
    if (!slot.connected)
        return -1;
    if (slot.announced)
        return 0;
    if (!waveform::Ready(index) && waveform::GetStatistics(index).open_result == 0 &&
        Now() < slot.native_pending_until)
        return -1;
    const int result = LiSendControllerArrivalEvent(
        index, WireMask() | (1u << index), LI_CTYPE_PS, SupportedButtons(),
        Capabilities | (waveform::Ready(index) ? LI_CCAP_HAPTICS_PCM : 0));
    if (result == 0)
    {
        slot.announced = true;
        ++statistics.arrivals;
        // Public scePad data has no verified battery field. Never invent a
        // percentage, and don't advertise battery reporting as a capability.
        LiSendControllerBatteryEvent(index, LI_BATTERY_STATE_UNKNOWN,
                                     LI_BATTERY_PERCENTAGE_UNKNOWN);
    }
    if (result != 0)
        ++statistics.send_errors;
    return result;
}
void ApplyOutput(Slot &slot)
{
    if (!slot.connected || slot.handle < 0 || slot.intercepted)
        return;
    if (slot.sensor_dirty)
    {
        const bool enabled = slot.motion_rate[0] || slot.motion_rate[1];
        const int result = scePadSetMotionSensorState(slot.handle, enabled);
        OutputResult(slot, "motion", result);
        slot.sensor_enabled = enabled && result >= 0;
        if (slot.sensor_enabled)
            scePadSetAngularVelocityDeadbandState(slot.handle, false);
        slot.sensor_dirty = false;
    }
    const unsigned index = static_cast<unsigned>(&slot - slots);
    const uint64_t token = waveform::ActiveToken(index);
    const bool native = token != 0;
    if (native != slot.native_active)
    {
        slot.native_active = native;
        slot.rumble_dirty = true;
        if (native)
        {
            const uint8_t off[2]{};
            const int rc = scePadSetVibration(slot.handle, off);
            OutputResult(slot, "yield rumble to waveform", rc);
            const int mode = rc >= 0 ? scePadSetVibrationMode(slot.handle, 1) : rc;
            OutputResult(slot, "native haptics mode", mode);
            slot.vibration_mode = mode >= 0 ? VibrationMode::Native : VibrationMode::Unknown;
            if (mode < 0)
                waveform::Disable(index);
        }
    }
    // An idle/reacquire cycle may complete between two pad polls. Refresh the
    // epoch acknowledgement without repeating native API calls or relying on
    // observing every Active() transition.
    if (native && slot.vibration_mode == VibrationMode::Native)
        waveform::Allow(index, token);
    if (slot.rumble_dirty && !native &&
        (waveform::Ready(index) || waveform::GetStatistics(index).open_result != 0))
    {
        if (slot.vibration_mode != VibrationMode::Legacy)
        {
            const int mode = scePadSetVibrationMode(slot.handle, 2);
            OutputResult(slot, "legacy rumble mode", mode);
            slot.vibration_mode = mode >= 0 ? VibrationMode::Legacy : VibrationMode::Unknown;
        }
        // A failed mode switch or motor write must remain dirty for a later pad
        // poll; do not permanently lose the latest host-requested rumble.
        if (slot.vibration_mode == VibrationMode::Legacy)
        {
            const int result = scePadSetVibration(slot.handle, slot.rumble);
            OutputResult(slot, "rumble", result);
            slot.rumble_dirty = result < 0;
        }
    }
    if (slot.led_dirty)
    {
        OutputResult(slot, "lightbar", scePadSetLightBar(slot.handle, slot.color));
        slot.led_dirty = false;
    }
    if (slot.trigger_dirty)
    {
        OutputResult(slot, "adaptive triggers",
                     scePadSetTriggerEffect(slot.handle, &slot.triggers));
        slot.trigger_dirty = false;
        slot.triggers.mask = 0;
    }
}
float Unit(uint16_t value, uint16_t resolution)
{
    const float result = static_cast<float>(value) / (resolution > 1 ? resolution - 1 : 1);
    return result > 1.f ? 1.f : result;
}
void ExtendedInput(unsigned index, const PadSample &sample, bool suppressed)
{
    Slot &slot = slots[index];
    SetConnection(index, sample);
    if (!slot.connected)
        return;
    if (suppressed)
    {
        CancelTouches(index);
        if (!slot.intercepted)
        {
            uint8_t off[2]{};
            TriggerParam triggers{};
            triggers.mask = 3;
            scePadSetVibration(slot.handle, off);
            scePadSetTriggerEffect(slot.handle, &triggers);
        }
        if (!slot.intercepted)
            waveform::Disable(index);
        slot.intercepted = true;
        return;
    }
    if (slot.intercepted)
    {
        slot.intercepted = false;
        const int mode = scePadSetVibrationMode(slot.handle, 1);
        OutputResult(slot, "resume native haptics mode", mode);
        slot.vibration_mode = mode >= 0 ? VibrationMode::Native : VibrationMode::Unknown;
        if (mode >= 0)
            waveform::Configure(index, slot.user);
        else
            waveform::Disable(index);
        slot.triggers.mask = 3;
        slot.rumble_dirty = slot.led_dirty = slot.trigger_dirty = slot.sensor_dirty = true;
    }
    if (Announce(index) != 0)
        return;
    const bool remote_chord = (sample.buttons & 0x100000u) && (sample.buttons & 0x6u);
    if (LiGetHostFeatureFlags() & LI_FF_CONTROLLER_TOUCH_EVENTS)
    {
        const unsigned count = remote_chord ? 0 : (sample.touch_count > 2 ? 2 : sample.touch_count);
        for (unsigned old = 0; old < slot.previous.touch_count; ++old)
        {
            const Touch &touch = slot.previous.touches[old];
            bool found = false;
            for (unsigned current = 0; current < count; ++current)
                found |= sample.touches[current].id == touch.id;
            if (!found)
                LiSendControllerTouchEvent(index, LI_TOUCH_EVENT_UP, touch.id,
                                           Unit(touch.x, slot.width), Unit(touch.y, slot.height),
                                           0);
        }
        for (unsigned current = 0; current < count; ++current)
        {
            const Touch &touch = sample.touches[current];
            bool found = false, changed = true;
            for (unsigned old = 0; old < slot.previous.touch_count; ++old)
            {
                const Touch &prior = slot.previous.touches[old];
                if (prior.id == touch.id)
                {
                    found = true;
                    changed = prior.x != touch.x || prior.y != touch.y;
                }
            }
            if (!found || changed)
                LiSendControllerTouchEvent(index, found ? LI_TOUCH_EVENT_MOVE : LI_TOUCH_EVENT_DOWN,
                                           touch.id, Unit(touch.x, slot.width),
                                           Unit(touch.y, slot.height), 1);
        }
    }
    slot.previous = sample;
    if (remote_chord)
        slot.previous.touch_count = 0;
    if (slot.previous.touch_count > 2)
        slot.previous.touch_count = 2;
    const uint64_t now = Now();
    for (unsigned type = 0; type < 2; ++type)
    {
        if (!slot.sensor_enabled || !slot.motion_rate[type] || now < slot.motion_next[type])
            continue;
        const float *values = type ? sample.angular_velocity : sample.acceleration;
        const float scale = type ? 57.2957795131f : 9.80665f;
        if (std::isfinite(values[0]) && std::isfinite(values[1]) && std::isfinite(values[2]))
            LiSendControllerMotionEvent(index, type ? LI_MOTION_TYPE_GYRO : LI_MOTION_TYPE_ACCEL,
                                        values[0] * scale, values[1] * scale, values[2] * scale);
        slot.motion_next[type] = now + 1000000 / slot.motion_rate[type];
    }
}
int16_t Axis(uint8_t value, bool invert)
{
    int value16 = (static_cast<int>(value) - 128) * 256;
    if (invert)
        value16 = -value16;
    return value16 > 32767 ? 32767 : static_cast<int16_t>(value16);
}
int Buttons(uint32_t raw)
{
    const uint32_t native[] = {0x10,  0x40,  0x80, 0x20, 0x4000, 0x2000, 0x8000,  0x1000,
                               0x400, 0x800, 0x1,  0x8,  0x2,    0x4,    0x100000};
    const int remote[] = {UP_FLAG,   DOWN_FLAG, LEFT_FLAG,   RIGHT_FLAG,  A_FLAG,
                          B_FLAG,    X_FLAG,    Y_FLAG,      LB_FLAG,     RB_FLAG,
                          BACK_FLAG, PLAY_FLAG, LS_CLK_FLAG, RS_CLK_FLAG, TOUCHPAD_FLAG};
    int result = 0;
    for (unsigned i = 0; i < sizeof(native) / sizeof(native[0]); ++i)
        if (raw & native[i])
            result |= remote[i];
    return RemoteShortcuts(raw, result);
}
void Discover()
{
    int32_t users[4] = {-1, -1, -1, -1};
    if (sceUserServiceGetLoginUserIdList(users) < 0)
    {
        ++statistics.scan_errors;
        return;
    }
    bool primary_present = false;
    for (int32_t user : users)
        primary_present |= slots[0].user == user;
    if (slots[0].connected && !primary_present)
        Disconnect(0);
    for (unsigned i = 1; i < MaxControllers; ++i)
    {
        Slot &slot = slots[i];
        if (slot.handle < 0)
            continue;
        bool present = false;
        for (int32_t user : users)
            present |= slot.user == user;
        if (!present)
        {
            Disconnect(i);
            scePadClose(slot.handle);
            slot = Slot{};
        }
    }
    for (int32_t user : users)
    {
        if (user == -1)
            continue;
        bool assigned = false;
        for (const Slot &slot : slots)
            assigned |= slot.user == user;
        if (assigned)
            continue;
        for (unsigned i = 1; i < MaxControllers; ++i)
        {
            Slot &slot = slots[i];
            if (slot.handle >= 0)
                continue;
            const int handle = scePadOpen(user, 0, 0, nullptr);
            if (handle < 0)
            {
                ++statistics.open_errors;
                break;
            }
            slot.user = user;
            slot.handle = handle;
            PadSample sample{};
            if (scePadReadState(handle, &sample) >= 0)
                SetConnection(i, sample);
            break;
        }
    }
}
bool DecodeTrigger(TriggerCommand &command, uint8_t type, const uint8_t *data)
{
    command = {};
    if (type == 0 || type == 5)
        return true;
    if (type == 0x21 || type == 0x26)
    {
        const uint16_t zones = data[0] | (static_cast<uint16_t>(data[1]) << 8);
        const uint32_t strengths = data[2] | (static_cast<uint32_t>(data[3]) << 8) |
                                   (static_cast<uint32_t>(data[4]) << 16) |
                                   (static_cast<uint32_t>(data[5]) << 24);
        command.mode = type == 0x21 ? 4 : 6;
        const unsigned offset = type == 0x21 ? 0 : 1;
        if (offset)
            command.data[0] = data[8];
        for (unsigned zone = 0; zone < 10; ++zone)
            command.data[offset + zone] =
                (zones & (1u << zone)) ? ((strengths >> (3 * zone)) & 7) + 1 : 0;
        return true;
    }
    if (type == 0x25)
    {
        const uint16_t zones = data[0] | (static_cast<uint16_t>(data[1]) << 8);
        unsigned start = 10, end = 0, count = 0;
        for (unsigned i = 0; i < 10; ++i)
            if (zones & (1u << i))
            {
                if (!count)
                    start = i;
                end = i;
                ++count;
            }
        if (count != 2 || start < 2 || start > 7 || end > 8)
            return false;
        command.mode = 2;
        command.data[0] = start;
        command.data[1] = end;
        command.data[2] = (data[2] & 7) + 1;
        return true;
    }
    // Legacy HID effects are converted conservatively into native commands.
    if (type == 1 || type == 0x11)
    {
        command.mode = 1;
        command.data[0] = data[0] * 10u / 256;
        command.data[1] = data[1] ? 1 + data[1] * 7u / 255 : 0;
        return true;
    }
    if (type == 6)
    {
        command.mode = 3;
        command.data[0] = data[2] * 10u / 256;
        command.data[1] = data[1] ? 1 + data[1] * 7u / 255 : 0;
        command.data[2] = data[0];
        return true;
    }
    if (type == 2 || type == 0x12)
    {
        command.mode = 2;
        command.data[0] = 2 + data[0] * 5u / 255;
        const unsigned end = data[1] * 9u / 255;
        command.data[1] = end > command.data[0] ? end : command.data[0] + 1;
        if (command.data[1] > 8)
            command.data[1] = 8;
        command.data[2] = data[2] ? 1 + data[2] * 7u / 255 : 0;
        return true;
    }
    return false;
}
} // namespace
int RemoteShortcuts(uint32_t raw, int mapped)
{
    if (!(raw & 0x100000u))
        return mapped;
    if (raw & 0x2u)
        mapped = (mapped & ~(TOUCHPAD_FLAG | LS_CLK_FLAG)) | BACK_FLAG;
    if (raw & 0x4u)
        mapped = (mapped & ~(TOUCHPAD_FLAG | RS_CLK_FLAG)) | SPECIAL_FLAG;
    return mapped;
}
void Init(int32_t user, int32_t handle)
{
    waveform::DisableAll();
    Lock lock;
    for (Slot &slot : slots)
        slot = Slot{};
    active_mask = 0;
    statistics = {};
    local_actions = 0;
    live = false;
    initialized = true;
    slots[0].user = user;
    slots[0].handle = handle;
    PadSample sample{};
    if (handle >= 0 && scePadReadState(handle, &sample) >= 0)
        SetConnection(0, sample);
    Discover();
    next_discovery = Now() + 1000000;
    next_report = 0;
    // Open is asynchronous; cap startup cost at 50ms, never delay network callbacks.
    for (unsigned wait = 0; wait < 25 && !waveform::AnyReady(); ++wait)
        usleep(2000);
}
uint16_t ActiveMask()
{
    Lock lock;
    return active_mask;
}
Statistics GetStatistics()
{
    Lock lock;
    return statistics;
}
unsigned TakeLocalActions()
{
    Lock lock;
    const unsigned actions = local_actions;
    local_actions = 0;
    return actions;
}
void PrimarySample(const PadSample &sample, bool suppressed)
{
    Lock lock;
    if (!initialized)
        return;
    live = true;
    ExtendedInput(0, sample, suppressed);
}
int SendPrimary(int buttons, uint8_t lt, uint8_t rt, int16_t lx, int16_t ly, int16_t rx, int16_t ry)
{
    Lock lock;
    if (!initialized || !slots[0].connected)
        return 0;
    live = true;
    int result = Announce(0);
    if (result != 0)
        return result;
    result = LiSendMultiControllerEvent(0, WireMask(), buttons, lt, rt, lx, ly, rx, ry);
    if (result != 0)
        ++statistics.send_errors;
    return result;
}
void Poll()
{
    Lock lock;
    if (!initialized)
        return;
    live = true;
    const uint64_t now = Now();
    if (now >= next_report)
    {
        for (unsigned i = 0; i < MaxControllers; ++i)
        {
            const Slot &slot = slots[i];
            if (!slot.connected)
                continue;
            char line[640];
            snprintf(line, sizeof(line),
                     "DualSense slot=%u mask=%x rumble=%llu triggers=%llu led=%llu accel_hz=%u "
                     "gyro_hz=%u pcm_packets=%llu errors=%llu",
                     i, active_mask, (unsigned long long)slot.rumble_requests,
                     (unsigned long long)slot.trigger_requests,
                     (unsigned long long)slot.led_requests, slot.motion_rate[0],
                     slot.motion_rate[1], (unsigned long long)waveform::GetStatistics(i).received,
                     (unsigned long long)slot.output_errors);
            lan_http_report_text(line);
            const auto pcm = waveform::GetStatistics(i);
            snprintf(line, sizeof(line),
                     "DualSense waveform slot=%u ready=%d active=%d open=%08x state=%08x "
                     "channels=%u rx=%llu drop=%llu duplicate=%llu reorder=%llu gaps=%llu "
                     "underrun=%llu contention=%llu errors=%llu overflow=%llu inbox_full=%llu "
                     "stale=%llu rejected=%llu stalls=%llu padded=%llu outputs=%llu "
                     "inputs=%llu dequeued=%llu starvation=%llu ignored_silent=%llu",
                     i, pcm.ready, pcm.active, pcm.open_result, pcm.state_result, pcm.port_channels,
                     (unsigned long long)pcm.received, (unsigned long long)pcm.dropped,
                     (unsigned long long)pcm.duplicate, (unsigned long long)pcm.out_of_order,
                     (unsigned long long)pcm.gaps, (unsigned long long)pcm.underruns,
                     (unsigned long long)pcm.contention, (unsigned long long)pcm.output_errors,
                     (unsigned long long)pcm.overflow, (unsigned long long)pcm.inbox_full,
                     (unsigned long long)pcm.stale, (unsigned long long)pcm.rejected,
                     (unsigned long long)pcm.output_stalls, (unsigned long long)pcm.padded_frames,
                     (unsigned long long)pcm.output_calls, (unsigned long long)pcm.input_frames,
                     (unsigned long long)pcm.dequeued_frames,
                     (unsigned long long)pcm.starvation_frames,
                     (unsigned long long)pcm.ignored_silent_frames);
            lan_http_report_text(line);
            snprintf(line, sizeof(line),
                     "DualSense waveform policy slot=%u profile=%d frames=%u min_us=%llu "
                     "fragment_us=%llu "
                     "capacity=%u grain=%u",
                     i, PROSPEROLIGHT_HAPTICS_PROFILE, waveform::SelectedPolicy.frames,
                     (unsigned long long)waveform::SelectedPolicy.minimum_us,
                     (unsigned long long)waveform::SelectedPolicy.fragment_us, waveform::Capacity,
                     waveform::Grain);
            lan_http_report_text(line);
            const char *names[] = {"receive_to_audioout", "audioout_blocking", "first_nonzero",
                                   "queue_depth"};
            const waveform::DistributionSummary *distributions[] = {
                &pcm.receive_to_output, &pcm.blocking, &pcm.first_nonzero_delay, &pcm.queue_depth};
            for (unsigned metric = 0; metric < 4; ++metric)
            {
                const auto &d = *distributions[metric];
                snprintf(line, sizeof(line),
                         "DualSense waveform timing slot=%u metric=%s unit=%s software=1 n=%llu "
                         "p50=%llu p95=%llu p99=%llu max=%llu",
                         i, names[metric], metric == 3 ? "frames" : "us",
                         (unsigned long long)d.samples, (unsigned long long)d.p50,
                         (unsigned long long)d.p95, (unsigned long long)d.p99,
                         (unsigned long long)d.maximum);
                lan_http_report_text(line);
            }
        }
        next_report = now + 10000000;
    }
    if (now >= next_discovery)
    {
        Discover();
        next_discovery = now + 1000000;
    }
    for (unsigned i = 0; i < MaxControllers; ++i)
    {
        Slot &slot = slots[i];
        if (slot.handle < 0)
            continue;
        if (i)
        {
            PadSample samples[64];
            const int count = scePadRead(slot.handle, samples, 64);
            if (count > 0 && count <= 64)
            {
                const PadSample *newest = &samples[0];
                for (int n = 1; n < count; ++n)
                    if (samples[n].timestamp_us >= newest->timestamp_us)
                        newest = &samples[n];
                PadSample forwarded = *newest;
                const bool suppressed = (newest->buttons & 0x80000000u) != 0;
                const uint32_t chords =
                    !suppressed && (newest->buttons & 0x100000u) ? newest->buttons & 0xc00u : 0;
                const uint32_t pressed = chords & ~slot.local_buttons;
                slot.local_buttons = chords;
                if (pressed & 0x800u)
                    local_actions ^= ToggleStatistics;
                if (pressed & 0x400u)
                    local_actions |= StopStream;
                if (chords)
                    forwarded.buttons &= ~(0x100000u | 0xc00u);
                newest = &forwarded;
                ExtendedInput(i, *newest, suppressed);
                if (slot.connected && Announce(i) == 0)
                {
                    const int buttons = suppressed ? 0 : Buttons(newest->buttons);
                    const uint8_t triggers[2] = {
                        static_cast<uint8_t>(suppressed ? 0 : newest->left_trigger),
                        static_cast<uint8_t>(suppressed ? 0 : newest->right_trigger)};
                    const int16_t axes[4] = {
                        static_cast<int16_t>(suppressed ? 0 : Axis(newest->left_x, false)),
                        static_cast<int16_t>(suppressed ? 0 : Axis(newest->left_y, true)),
                        static_cast<int16_t>(suppressed ? 0 : Axis(newest->right_x, false)),
                        static_cast<int16_t>(suppressed ? 0 : Axis(newest->right_y, true))};
                    // A pad reports hundreds of samples a second. The host
                    // hears of it when its state changes, and at the idle
                    // interval otherwise.
                    const bool changed =
                        !slot.sent_valid || buttons != slot.sent_buttons ||
                        memcmp(triggers, slot.sent_triggers, sizeof(triggers)) != 0 ||
                        memcmp(axes, slot.sent_axes, sizeof(axes)) != 0;
                    if (changed || now - slot.last_input >= 100000)
                    {
                        const int result = LiSendMultiControllerEvent(
                            i, WireMask(), buttons, triggers[0], triggers[1], axes[0], axes[1],
                            axes[2], axes[3]);
                        if (result == 0)
                        {
                            ++slot.input_events;
                            slot.last_input = now;
                            slot.sent_valid = true;
                            slot.sent_buttons = buttons;
                            memcpy(slot.sent_triggers, triggers, sizeof(triggers));
                            memcpy(slot.sent_axes, axes, sizeof(axes));
                        }
                        else
                            ++statistics.send_errors;
                    }
                }
            }
            else if (slot.connected && now - slot.last_input >= 100000 && slot.announced)
            {
                const PadSample &sample = slot.previous;
                const int result = LiSendMultiControllerEvent(
                    i, WireMask(), slot.intercepted ? 0 : Buttons(sample.buttons),
                    slot.intercepted ? 0 : sample.left_trigger,
                    slot.intercepted ? 0 : sample.right_trigger,
                    slot.intercepted ? 0 : Axis(sample.left_x, false),
                    slot.intercepted ? 0 : Axis(sample.left_y, true),
                    slot.intercepted ? 0 : Axis(sample.right_x, false),
                    slot.intercepted ? 0 : Axis(sample.right_y, true));
                if (result != 0)
                    ++statistics.send_errors;
                slot.last_input = now;
            }
        }
        ApplyOutput(slot);
    }
}
int Stop()
{
    Lock lock;
    if (!initialized)
        return 0;
    waveform::DisableAll();
    // Bounded grace for ordinary grain completion, never join a stalled AudioOut.
    for (unsigned wait = 0; wait < 5; ++wait)
    {
        bool pending = false;
        for (unsigned i = 0; i < MaxControllers; ++i)
            pending |= waveform::Active(i);
        if (!pending)
            break;
        usleep(2000);
    }
    int result = 0;
    for (unsigned i = 0; i < MaxControllers; ++i)
    {
        Slot &slot = slots[i];
        const int removal = Disconnect(i);
        if (result == 0)
            result = removal;
        char line[192];
        snprintf(line, sizeof(line), "DualSense slot=%u inputs=%llu pcm=%llu output_errors=%llu", i,
                 (unsigned long long)slot.input_events,
                 (unsigned long long)waveform::GetStatistics(i).received,
                 (unsigned long long)slot.output_errors);
        lan_http_report_text(line);
    }
    live = false;
    return result;
}
void Shutdown()
{
    waveform::DisableAll();
    Lock lock;
    if (!initialized)
        return;
    for (unsigned i = 0; i < MaxControllers; ++i)
    {
        ResetOutput(slots[i]);
        if (i && slots[i].handle >= 0)
            scePadClose(slots[i].handle);
        slots[i] = Slot{};
    }
    active_mask = 0;
    initialized = live = false;
}
void Rumble(uint16_t index, uint16_t low, uint16_t high)
{
    Lock lock;
    if (!initialized || index >= MaxControllers)
        return;
    Slot &slot = slots[index];
    if (!slot.connected)
        return;
    ++slot.rumble_requests;
    slot.rumble[0] = (low + 256u) / 257u;
    slot.rumble[1] = (high + 256u) / 257u;
    slot.rumble_dirty = true;
}
void RumbleTriggers(uint16_t index, uint16_t left, uint16_t right)
{
    Lock lock;
    if (!initialized || index >= MaxControllers || !slots[index].connected)
        return;
    Slot &slot = slots[index];
    ++slot.trigger_requests;
    slot.triggers = {};
    slot.triggers.mask = 3;
    const uint16_t strengths[2] = {left, right};
    for (unsigned i = 0; i < 2; ++i)
        if (strengths[i])
        {
            slot.triggers.commands[i].mode = 3;
            slot.triggers.commands[i].data[0] = 0;
            slot.triggers.commands[i].data[1] = 1 + strengths[i] * 7u / 65535;
            slot.triggers.commands[i].data[2] = 60;
        }
    slot.trigger_dirty = true;
}
void MotionState(uint16_t index, uint8_t type, uint16_t rate)
{
    Lock lock;
    if (!initialized || index >= MaxControllers || type < 1 || type > 2)
        return;
    Slot &slot = slots[index];
    slot.motion_rate[type - 1] = rate > 250 ? 250 : rate;
    slot.motion_next[type - 1] = 0;
    slot.sensor_dirty = true;
}
void Led(uint16_t index, uint8_t red, uint8_t green, uint8_t blue)
{
    Lock lock;
    if (!initialized || index >= MaxControllers)
        return;
    Slot &slot = slots[index];
    ++slot.led_requests;
    slot.color[0] = red;
    slot.color[1] = green;
    slot.color[2] = blue;
    slot.led_dirty = true;
}
void AdaptiveTriggers(uint16_t index, uint8_t flags, uint8_t left_type, uint8_t right_type,
                      uint8_t *left, uint8_t *right)
{
    Lock lock;
    if (!initialized || index >= MaxControllers || !slots[index].connected)
        return;
    Slot &slot = slots[index];
    ++slot.trigger_requests;
    if ((flags & DS_EFFECT_LEFT_TRIGGER) && left)
    {
        slot.triggers.mask |= 1;
        if (!DecodeTrigger(slot.triggers.commands[0], left_type, left))
            lan_http_report_text("DualSense unsupported left trigger effect: reset to off");
    }
    if ((flags & DS_EFFECT_RIGHT_TRIGGER) && right)
    {
        slot.triggers.mask |= 2;
        if (!DecodeTrigger(slot.triggers.commands[1], right_type, right))
            lan_http_report_text("DualSense unsupported right trigger effect: reset to off");
    }
    slot.trigger_dirty = slot.triggers.mask != 0;
}
void Haptics(uint16_t index, uint32_t sequence, const uint8_t *pcm, uint16_t frames)
{
    if (index >= MaxControllers || !pcm || !frames || frames > ML_HAPTICS_MAX_FRAMES)
        return;
    // Native readiness/epochs already gate disconnect, suppression and shutdown.
    // Do not contend with pad polling or diagnostics on the receive thread.
    waveform::Push(index, sequence, pcm, frames);
}
} // namespace prosperolight::dualsense
