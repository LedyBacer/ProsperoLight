/*
 * ps5-native-app-boilerplate - Native DualSense and multi-controller input.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once
#include <cstddef>
#include <cstdint>

namespace prosperolight::dualsense
{
constexpr unsigned MaxControllers = 4;
struct Touch
{
    uint16_t x, y;
    uint8_t id, reserved[3];
};
struct PadSample
{
    uint32_t buttons;
    uint8_t left_x, left_y, right_x, right_y, left_trigger, right_trigger;
    uint16_t padding;
    float orientation[4], acceleration[3], angular_velocity[3];
    uint8_t touch_count, touch_reserved[3];
    uint32_t touch_reserved2;
    Touch touches[2];
    int32_t connected;
    uint64_t timestamp_us;
    uint8_t extension[16], connected_count, remaining[15];
};
static_assert(sizeof(PadSample) == 120);
static_assert(offsetof(PadSample, acceleration) == 0x1c);
static_assert(offsetof(PadSample, angular_velocity) == 0x28);
static_assert(offsetof(PadSample, touches) == 0x3c);
static_assert(offsetof(PadSample, connected) == 0x4c);
static_assert(offsetof(PadSample, timestamp_us) == 0x50);
static_assert(offsetof(PadSample, connected_count) == 0x68);
void Init(int32_t primary_user, int32_t primary_handle);
uint16_t ActiveMask();
struct Statistics
{
    uint32_t peak, arrivals, removals, open_errors, send_errors, scan_errors;
};
Statistics GetStatistics();
// Local shortcuts on secondary controllers, consumed by the stream owner.
constexpr unsigned ToggleStatistics = 1u, StopStream = 2u;
unsigned TakeLocalActions();
int RemoteShortcuts(uint32_t raw_buttons, int mapped_buttons);
void PrimarySample(const PadSample &sample, bool suppressed);
int SendPrimary(int buttons, uint8_t left_trigger, uint8_t right_trigger, int16_t left_x,
                int16_t left_y, int16_t right_x, int16_t right_y);
void Poll();
int Stop();
void Shutdown();
void Rumble(uint16_t controller, uint16_t low, uint16_t high);
void RumbleTriggers(uint16_t controller, uint16_t left, uint16_t right);
void MotionState(uint16_t controller, uint8_t type, uint16_t rate);
void Led(uint16_t controller, uint8_t red, uint8_t green, uint8_t blue);
void AdaptiveTriggers(uint16_t controller, uint8_t flags, uint8_t left_type, uint8_t right_type,
                      uint8_t *left, uint8_t *right);
void Haptics(uint16_t controller, uint32_t sequence, const uint8_t *pcm, uint16_t frames);
} // namespace prosperolight::dualsense
