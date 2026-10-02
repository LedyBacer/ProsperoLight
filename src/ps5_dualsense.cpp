/*
 * ps5-native-app-boilerplate - Native DualSense and multi-controller input.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ps5_dualsense.hpp"
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
// sources, not proprietary SDK headers. Native waveform output is intentionally
// not guessed: PCM is rendered as a bounded actuator envelope via scePad.
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
    uint8_t rumble[2]{}, color[3]{};
    bool rumble_dirty = false, led_dirty = false, trigger_dirty = false;
    TriggerParam triggers{};
    bool sensor_enabled = false, sensor_dirty = false;
    uint64_t pcm_until = 0;
    uint32_t pcm_sequence = 0;
    bool pcm_sequence_valid = false;
    uint8_t pcm_amplitude[2]{};
    uint64_t pcm_packets = 0, input_events = 0, output_errors = 0;
    uint64_t rumble_requests = 0, trigger_requests = 0, led_requests = 0;
    bool failure_logged = false;
};
Slot slots[MaxControllers];
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
bool initialized = false, live = false;
uint64_t next_discovery = 0, next_report = 0;
uint16_t active_mask = 0;
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
           LB_FLAG | RB_FLAG | BACK_FLAG | PLAY_FLAG | LS_CLK_FLAG | RS_CLK_FLAG | TOUCHPAD_FLAG;
}
constexpr uint16_t Capabilities = LI_CCAP_ANALOG_TRIGGERS | LI_CCAP_RUMBLE |
                                  LI_CCAP_TRIGGER_RUMBLE | LI_CCAP_TOUCHPAD | LI_CCAP_ACCEL |
                                  LI_CCAP_GYRO | LI_CCAP_RGB_LED | LI_CCAP_HAPTICS_PCM;
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
    OutputResult(slot, "reset rumble", scePadSetVibration(slot.handle, vibration));
    OutputResult(slot, "reset triggers", scePadSetTriggerEffect(slot.handle, &off));
    OutputResult(slot, "reset lightbar", scePadResetLightBar(slot.handle));
    OutputResult(slot, "stop motion", scePadSetMotionSensorState(slot.handle, false));
    slot.rumble_dirty = slot.led_dirty = slot.trigger_dirty = slot.sensor_dirty = false;
    slot.motion_rate[0] = slot.motion_rate[1] = 0;
    slot.pcm_until = 0;
    slot.pcm_sequence_valid = false;
    slot.sensor_enabled = false;
}
void Disconnect(unsigned index)
{
    Slot &slot = slots[index];
    active_mask &= ~(1u << index);
    CancelTouches(index);
    if (live && slot.announced)
        LiSendMultiControllerEvent(index, active_mask, 0, 0, 0, 0, 0, 0, 0);
    ResetOutput(slot);
    slot.connected = slot.announced = false;
    slot.last_input = 0;
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
        active_mask |= 1u << index;
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
    const int result = LiSendControllerArrivalEvent(index, active_mask, LI_CTYPE_PS,
                                                    SupportedButtons(), Capabilities);
    if (result == 0)
    {
        slot.announced = true;
        // Public scePad data has no verified battery field. Never invent a
        // percentage, and don't advertise battery reporting as a capability.
        LiSendControllerBatteryEvent(index, LI_BATTERY_STATE_UNKNOWN,
                                     LI_BATTERY_PERCENTAGE_UNKNOWN);
    }
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
    const uint64_t now = Now();
    if (slot.pcm_until && now >= slot.pcm_until)
    {
        slot.pcm_until = 0;
        slot.rumble_dirty = true;
    }
    if (slot.rumble_dirty)
    {
        const uint8_t *motors = slot.pcm_until ? slot.pcm_amplitude : slot.rumble;
        OutputResult(slot, "rumble", scePadSetVibration(slot.handle, motors));
        slot.rumble_dirty = false;
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
        slot.intercepted = true;
        return;
    }
    if (slot.intercepted)
    {
        slot.intercepted = false;
        slot.triggers.mask = 3;
        slot.rumble_dirty = slot.led_dirty = slot.trigger_dirty = slot.sensor_dirty = true;
    }
    if (Announce(index) != 0)
        return;
    if (LiGetHostFeatureFlags() & LI_FF_CONTROLLER_TOUCH_EVENTS)
    {
        const unsigned count = sample.touch_count > 2 ? 2 : sample.touch_count;
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
    return result;
}
void Discover()
{
    int32_t users[4] = {-1, -1, -1, -1};
    if (sceUserServiceGetLoginUserIdList(users) < 0)
        return;
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
                break;
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
void Init(int32_t user, int32_t handle)
{
    Lock lock;
    for (Slot &slot : slots)
        slot = Slot{};
    active_mask = 0;
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
}
uint16_t ActiveMask()
{
    Lock lock;
    return active_mask;
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
    return LiSendMultiControllerEvent(0, active_mask, buttons, lt, rt, lx, ly, rx, ry);
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
            char line[320];
            snprintf(line, sizeof(line),
                     "DualSense slot=%u mask=%x rumble=%llu triggers=%llu led=%llu accel_hz=%u "
                     "gyro_hz=%u pcm_envelope=%llu errors=%llu",
                     i, active_mask, (unsigned long long)slot.rumble_requests,
                     (unsigned long long)slot.trigger_requests,
                     (unsigned long long)slot.led_requests, slot.motion_rate[0],
                     slot.motion_rate[1], (unsigned long long)slot.pcm_packets,
                     (unsigned long long)slot.output_errors);
            lan_http_report_text(line);
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
                const bool suppressed = (newest->buttons & 0x80000000u) != 0;
                ExtendedInput(i, *newest, suppressed);
                if (slot.connected && Announce(i) == 0)
                {
                    const int result = LiSendMultiControllerEvent(
                        i, active_mask, suppressed ? 0 : Buttons(newest->buttons),
                        suppressed ? 0 : newest->left_trigger,
                        suppressed ? 0 : newest->right_trigger,
                        suppressed ? 0 : Axis(newest->left_x, false),
                        suppressed ? 0 : Axis(newest->left_y, true),
                        suppressed ? 0 : Axis(newest->right_x, false),
                        suppressed ? 0 : Axis(newest->right_y, true));
                    if (result == 0)
                    {
                        ++slot.input_events;
                        slot.last_input = now;
                    }
                }
            }
            else if (slot.connected && now - slot.last_input >= 100000 && slot.announced)
            {
                const PadSample &sample = slot.previous;
                LiSendMultiControllerEvent(i, active_mask,
                                           slot.intercepted ? 0 : Buttons(sample.buttons),
                                           slot.intercepted ? 0 : sample.left_trigger,
                                           slot.intercepted ? 0 : sample.right_trigger,
                                           slot.intercepted ? 0 : Axis(sample.left_x, false),
                                           slot.intercepted ? 0 : Axis(sample.left_y, true),
                                           slot.intercepted ? 0 : Axis(sample.right_x, false),
                                           slot.intercepted ? 0 : Axis(sample.right_y, true));
                slot.last_input = now;
            }
        }
        ApplyOutput(slot);
    }
}
void Stop()
{
    Lock lock;
    if (!initialized)
        return;
    for (unsigned i = 0; i < MaxControllers; ++i)
    {
        Slot &slot = slots[i];
        Disconnect(i);
        char line[192];
        snprintf(line, sizeof(line), "DualSense slot=%u inputs=%llu pcm=%llu output_errors=%llu", i,
                 (unsigned long long)slot.input_events, (unsigned long long)slot.pcm_packets,
                 (unsigned long long)slot.output_errors);
        lan_http_report_text(line);
    }
    live = false;
}
void Shutdown()
{
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
    slot.rumble[0] = low >> 8;
    slot.rumble[1] = high >> 8;
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
    // Never wait on hardware from the network control receive thread.
    if (pthread_mutex_trylock(&mutex) != 0)
        return;
    Slot &slot = slots[index];
    if (initialized && slot.connected &&
        (!slot.pcm_sequence_valid || static_cast<int32_t>(sequence - slot.pcm_sequence) > 0))
    {
        uint64_t energy[2]{};
        for (unsigned frame = 0; frame < frames; ++frame)
            for (unsigned channel = 0; channel < 2; ++channel)
            {
                const int32_t value =
                    static_cast<int16_t>(MlHapticsRead16(pcm + frame * 4 + channel * 2));
                energy[channel] += static_cast<uint64_t>(static_cast<int64_t>(value) * value);
            }
        for (unsigned channel = 0; channel < 2; ++channel)
            slot.pcm_amplitude[channel] = static_cast<uint8_t>(
                std::sqrt(static_cast<double>(energy[channel]) / frames) * 255 / 32768);
        slot.pcm_sequence = sequence;
        slot.pcm_sequence_valid = true;
        slot.pcm_until = Now() + 60000;
        slot.rumble_dirty = true;
        ++slot.pcm_packets;
    }
    pthread_mutex_unlock(&mutex);
}
} // namespace prosperolight::dualsense
