/*
 * ps5-native-app-boilerplate / ProsperoLight - Controller lifecycle checks against the native
 * DualSense backend. Copyright (C) 2026 BlackBearReloaded SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "../src/ps5_dualsense.cpp"
#include <cassert>
#include <algorithm>
#include <iterator>
#include <vector>

using namespace prosperolight::dualsense;
struct MockPad
{
    bool opened, refuse_open, silent, read_error;
    int connected;
    uint32_t buttons;
};
static MockPad pads[10];
static int32_t users[4];
static int scan_result;
static uint64_t clock_us;
static bool fail_arrival;
static unsigned closes, led_writes, rumble_writes;
struct Sent
{
    bool arrival;
    int number, mask, buttons;
};
static std::vector<Sent> sent;
extern "C"
{
    int lan_http_report_text(const char *)
    {
        return 0;
    }
    uint64_t PltGetMicroseconds(void)
    {
        return clock_us;
    }
    int32_t sceUserServiceGetLoginUserIdList(void *list)
    {
        if (scan_result < 0)
            return scan_result;
        memcpy(list, users, sizeof(users));
        return 0;
    }
    int32_t scePadOpen(int32_t user, int32_t, int32_t, const void *)
    {
        assert(user > 0 && user < 10 && !pads[user].opened);
        if (pads[user].refuse_open)
            return -2;
        pads[user].opened = true;
        return 100 + user;
    }
    int32_t scePadClose(int32_t handle)
    {
        assert(pads[handle - 100].opened);
        pads[handle - 100].opened = false;
        ++closes;
        return 0;
    }
    int32_t scePadRead(int32_t handle, void *output, int32_t capacity)
    {
        const auto &pad = pads[handle - 100];
        assert(pad.opened && capacity > 0);
        if (pad.read_error)
            return -1;
        if (pad.silent)
            return 0;
        auto *sample = static_cast<PadSample *>(output);
        *sample = {};
        sample->buttons = pad.buttons;
        sample->left_x = sample->left_y = sample->right_x = sample->right_y = 128;
        sample->connected = pad.connected;
        sample->connected_count = 1;
        sample->timestamp_us = clock_us;
        return 1;
    }
    int32_t scePadReadState(int32_t handle, void *output)
    {
        return scePadRead(handle, output, 1) > 0 ? 0 : -1;
    }
    int32_t scePadSetVibration(int32_t, const void *)
    {
        ++rumble_writes;
        return 0;
    }
    int32_t scePadSetVibrationMode(int32_t, int32_t)
    {
        return 0;
    }
    int32_t scePadSetLightBar(int32_t, const void *)
    {
        ++led_writes;
        return 0;
    }
    int32_t scePadResetLightBar(int32_t)
    {
        return 0;
    }
    int32_t scePadSetMotionSensorState(int32_t, bool)
    {
        return 0;
    }
    int32_t scePadSetAngularVelocityDeadbandState(int32_t, bool)
    {
        return 0;
    }
    int32_t scePadGetControllerInformation(int32_t, void *)
    {
        return -1;
    }
    int32_t scePadSetTriggerEffect(int32_t, const void *)
    {
        return 0;
    }
    uint32_t LiGetHostFeatureFlags(void)
    {
        return 0;
    }
    int LiSendControllerArrivalEvent(uint8_t number, uint16_t mask, uint8_t type, uint32_t,
                                     uint16_t)
    {
        if (fail_arrival)
            return -1;
        assert(type == LI_CTYPE_PS);
        sent.push_back({true, number, mask, 0});
        return 0;
    }
    int LiSendMultiControllerEvent(short number, short mask, int buttons, unsigned char,
                                   unsigned char, short, short, short, short)
    {
        sent.push_back({false, number, mask, buttons});
        return 0;
    }
    int LiSendControllerBatteryEvent(uint8_t, uint8_t, uint8_t)
    {
        return 0;
    }
    int LiSendControllerTouchEvent(uint8_t, uint8_t, uint32_t, float, float, float)
    {
        return 0;
    }
    int LiSendControllerMotionEvent(uint8_t, uint8_t, float, float, float)
    {
        return 0;
    }
}
static void sign_in(std::initializer_list<int32_t> list)
{
    std::fill(std::begin(users), std::end(users), -1);
    unsigned index = 0;
    for (int user : list)
        users[index++] = user;
    clock_us += 1000001;
}
static void begin(std::initializer_list<int32_t> list)
{
    memset(pads, 0, sizeof(pads));
    for (int user : list)
        pads[user].connected = 1;
    scan_result = 0;
    fail_arrival = false;
    closes = led_writes = rumble_writes = 0;
    sent.clear();
    sign_in(list);
    pads[1].opened = true; // The stream owns the primary pad handle.
    Init(1, 101);
}
static void poll()
{
    clock_us += 1000;
    sent.clear();
    PadSample primary{};
    if (scePadReadState(101, &primary) == 0)
    {
        PrimarySample(primary, false);
        SendPrimary(RemoteShortcuts(primary.buttons, (primary.buttons & 0x4000) ? A_FLAG : 0), 0, 0,
                    0, 0, 0, 0);
    }
    Poll();
}
static bool event(bool arrival, int number, int mask, int buttons = 0)
{
    for (const auto &e : sent)
        if (e.arrival == arrival && e.number == number && e.mask == mask && e.buttons == buttons)
            return true;
    return false;
}
static void finish()
{
    Stop();
    assert(ActiveMask() == 0);
    Shutdown();
    assert(pads[1].opened); // No double-close of the caller's primary handle.
    for (unsigned i = 2; i < 10; ++i)
        assert(!pads[i].opened);
}
int main()
{
    begin({1, 2, 3, 4});
    assert(ActiveMask() == 15);
    poll();
    for (int i = 0; i < 4; ++i)
        assert(event(true, i, 15));
    assert(GetStatistics().peak == 4 && GetStatistics().arrivals == 4);
    sign_in({1, 2, 4});
    poll();
    assert(ActiveMask() == 11 && event(false, 2, 11) && closes == 1);
    pads[5].connected = 1;
    sign_in({1, 2, 5, 4});
    poll();
    assert(ActiveMask() == 15 && event(true, 2, 15));
    pads[2].connected = 0;
    poll();
    assert(ActiveMask() == 13 && event(false, 1, 13));
    pads[2].connected = 1;
    pads[2].buttons = 0x2000; // Circle.
    poll();
    assert(event(true, 1, 15) && event(false, 1, 15, B_FLAG));
    pads[2].buttons = 0x80004000; // System menu intercepts input.
    poll();
    assert(event(false, 1, 15, 0));
    pads[2].buttons = 0x100002;
    poll();
    assert(event(false, 1, 15, BACK_FLAG));
    pads[2].buttons = 0x100004;
    poll();
    assert(event(false, 1, 15, SPECIAL_FLAG));
    pads[2].buttons = 0x100800;
    poll();
    assert(TakeLocalActions() == ToggleStatistics && event(false, 1, 15, 0));
    poll();
    assert(TakeLocalActions() == 0); // Held chords fire only on their rising edge.
    pads[2].buttons = 0x100400;
    poll();
    assert(TakeLocalActions() == StopStream && event(false, 1, 15, 0));
    pads[2].buttons = 0;
    Led(1, 12, 34, 56);
    poll();
    const unsigned led_before = led_writes;
    const unsigned rumble_before = rumble_writes;
    Rumble(1, 65535, 32768);
    poll();
    assert(led_writes == led_before && rumble_writes == rumble_before + 1);
    finish();

    begin({1});
    pads[2].connected = 1;
    pads[2].refuse_open = true;
    sign_in({1, 2});
    poll();
    assert(GetStatistics().open_errors == 1 && ActiveMask() == 1);
    pads[2].refuse_open = false;
    fail_arrival = true;
    sign_in({1, 2});
    poll();
    assert(ActiveMask() == 3 && GetStatistics().send_errors > 0);
    fail_arrival = false;
    poll();
    assert(event(true, 1, 3));
    scan_result = -5;
    pads[2].read_error = true;
    sign_in({1, 2});
    poll();
    assert(GetStatistics().scan_errors == 1 && ActiveMask() == 3 && closes == 0);
    finish();
    printf("DualSense / multiple controller lifecycle checks PASS\n");
}
