/*
 * ps5-native-app-boilerplate / ProsperoLight - Controller lifecycle checks against the native
 * DualSense backend. Copyright (C) 2026 BlackBearReloaded SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "../src/ps5_dualsense.cpp"
#include <cassert>
#include <algorithm>
#include <iterator>
#include <vector>

// Legacy/input tests isolate the unavailable native renderer; waveform workers have
// their own mocked AudioOut integration suite.
namespace prosperolight::waveform
{
bool mock_ready = false, mock_active = false;
unsigned permits = 0;
void Configure(unsigned, int32_t)
{
}
void Disable(unsigned)
{
}
void DisableAll()
{
}
void Contended(unsigned)
{
}
void Allow(unsigned, uint64_t observed_token)
{
    assert(observed_token == (mock_active ? 1u : 0u));
    ++permits;
}
bool Ready(unsigned)
{
    return mock_ready;
}
bool Active(unsigned)
{
    return mock_active;
}
uint64_t ActiveToken(unsigned)
{
    return mock_active ? 1 : 0;
}
bool AnyReady()
{
    return mock_ready;
}
void Push(unsigned, uint32_t, const uint8_t *, unsigned)
{
}
Statistics GetStatistics(unsigned)
{
    Statistics s{};
    s.open_result = -1;
    return s;
}
} // namespace prosperolight::waveform
namespace waveform = prosperolight::waveform;
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
static unsigned closes, led_writes, rumble_writes, trigger_writes;
static int vibration_mode = 0, fail_legacy = 0, fail_rumble = 0, fail_native = 0;
static uint8_t last_rumble[2]{};
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
    int32_t scePadSetVibration(int32_t, const void *motors)
    {
        ++rumble_writes;
        if (fail_rumble)
        {
            --fail_rumble;
            return -9;
        }
        memcpy(last_rumble, motors, sizeof(last_rumble));
        return 0;
    }
    int32_t scePadSetVibrationMode(int32_t, int32_t mode)
    {
        if (mode == 1 && fail_native)
        {
            --fail_native;
            return -7;
        }
        if (mode == 2 && fail_legacy)
        {
            --fail_legacy;
            return -8;
        }
        vibration_mode = mode;
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
        ++trigger_writes;
        return 0;
    }
    uint32_t LiGetHostFeatureFlags(void)
    {
        return 0;
    }
    int LiSendControllerArrivalEvent(uint8_t number, uint16_t mask, uint8_t type, uint32_t,
                                     uint16_t caps)
    {
        assert(bool(caps & LI_CCAP_HAPTICS_PCM) == waveform::mock_ready);
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
// Unchanged input is not resent; what matters is that nothing was pressed for the host.
static bool quiet(int number)
{
    for (const auto &e : sent)
        if (!e.arrival && e.number == number && e.buttons != 0)
            return false;
    return true;
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
    // Each arrival names the controllers the host knows so far, and itself.
    for (int i = 0; i < 4; ++i)
        assert(event(true, i, (2 << i) - 1));
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
    assert(TakeLocalActions() == StopStream && quiet(1));
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
    waveform::mock_ready = true;
    begin({1});
    poll();
    const unsigned before_pcm = rumble_writes;
    waveform::mock_active = true;
    Rumble(0, 10000, 20000);
    RumbleTriggers(0, 1000, 2000);
    Led(0, 1, 2, 3);
    poll();
    assert(rumble_writes == before_pcm + 1 && waveform::permits > 0);
    const auto prior_permits = waveform::permits;
    poll();
    assert(rumble_writes == before_pcm + 1);
    assert(waveform::permits >
           prior_permits); // re-acknowledge an idle transition missed by polling
    waveform::mock_active = false;
    poll();
    assert(rumble_writes == before_pcm + 2);
    assert(last_rumble[0] == (10000 + 256) / 257 && last_rumble[1] == (20000 + 256) / 257);
    waveform::mock_active = true;
    poll();
    assert(vibration_mode == 1);
    const unsigned yielded = rumble_writes;
    Rumble(0, 65535, 32768); // keep the latest legacy request while PCM owns output
    poll();
    assert(rumble_writes == yielded);
    waveform::mock_active = false;
    fail_legacy = 1;
    const unsigned triggers_before = trigger_writes, leds_before = led_writes;
    RumbleTriggers(0, 1234, 2345);
    Led(0, 4, 5, 6);
    poll();
    assert(vibration_mode == 1 && rumble_writes == yielded);
    assert(trigger_writes == triggers_before + 1 && led_writes == leds_before + 1);
    fail_rumble = 1;
    poll();
    assert(vibration_mode == 2 && rumble_writes == yielded + 1);
    poll(); // failed output remains pending without another host request
    assert(rumble_writes == yielded + 2 && last_rumble[0] == 255 && last_rumble[1] == 128);
    waveform::mock_active = true;
    poll();
    assert(vibration_mode == 1);
    waveform::mock_ready = waveform::mock_active = false; // native output failure
    poll();
    assert(vibration_mode == 2 && last_rumble[0] == 255 && last_rumble[1] == 128);
    finish();
    waveform::mock_ready = false;
    fail_native = 1;
    vibration_mode = 0; // mode 1 failed; the device has no confirmed legacy mode
    begin({1});
    Rumble(0, 65535, 32768);
    poll();
    assert(vibration_mode == 2 && last_rumble[0] == 255 && last_rumble[1] == 128);
    finish();
    printf("DualSense / multiple controller lifecycle checks PASS\n");
}
