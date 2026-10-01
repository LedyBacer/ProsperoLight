/*
 * ps5-native-app-boilerplate / ProsperoLight - Multiple controller checks.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#define PROSPEROLIGHT_LAN_TELEMETRY 0
#include "../src/moonlight_stream.cpp"
#include <cassert>
#include <initializer_list>
#include <vector>

// The console as the stream sees it: who is signed in, and one pad per user.
struct MockPad
{
    bool opened, refuse_open, silent, read_error;
    int connected;
    uint32_t buttons;
};
static int32_t signed_in[4];
static int32_t scan_result;
static MockPad pads[10]; // indexed by user id
static unsigned opens, closes, notifications, hud_toggles;
static int hud_enabled;
static uint64_t sample_clock;

// What the host received: arrivals and controller states, in order.
struct Sent
{
    bool arrival;
    int number, mask, buttons;
};
static std::vector<Sent> sent;
static bool fail_arrival;

extern "C"
{
    int lan_http_report_text(const char *)
    {
        return 0;
    }
    int32_t sceKernelSendNotificationRequest(uint32_t, void *, size_t, int32_t)
    {
        ++notifications;
        return 0;
    }
    int sceKernelUsleep(uint32_t)
    {
        return 0;
    }
    int32_t sceUserServiceInitialize(void *)
    {
        return 0;
    }
    int32_t sceUserServiceGetInitialUser(int32_t *user_id)
    {
        *user_id = signed_in[0];
        return 0;
    }
    int32_t sceUserServiceGetLoginUserIdList(int32_t user_ids[4])
    {
        if (scan_result < 0)
            return scan_result;
        memcpy(user_ids, signed_in, sizeof(signed_in));
        return 0;
    }
    int32_t sceUserServiceTerminate(void)
    {
        return 0;
    }
    int32_t scePadInit(void)
    {
        return 0;
    }
    int32_t scePadOpen(int32_t user_id, int32_t, int32_t, const void *)
    {
        assert(user_id > 0 && user_id < 10 && !pads[user_id].opened);
        if (pads[user_id].refuse_open)
            return -2;
        pads[user_id].opened = true;
        ++opens;
        return 100 + user_id;
    }
    int32_t scePadClose(int32_t handle)
    {
        assert(pads[handle - 100].opened);
        pads[handle - 100].opened = false;
        ++closes;
        return 0;
    }
    int32_t scePadRead(int32_t handle, void *samples, int32_t capacity)
    {
        const MockPad &pad = pads[handle - 100];
        auto *sample = static_cast<ps5_pad_sample_t *>(samples);

        assert(pad.opened && capacity > 0);
        if (pad.read_error)
            return -1;
        if (pad.silent)
            return 0;
        memset(sample, 0, sizeof(*sample));
        sample->buttons = pad.buttons;
        sample->left_x = sample->left_y = sample->right_x = sample->right_y = 128;
        sample->connected = pad.connected;
        sample->timestamp_us = ++sample_clock;
        return 1;
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
    int LiSendMouseButtonEvent(char, int)
    {
        return 0;
    }
    int LiSendMouseMoveEvent(short, short)
    {
        return 0;
    }
    int LiSendScrollEvent(signed char)
    {
        return 0;
    }
    int LiSendHScrollEvent(signed char)
    {
        return 0;
    }
    int LiSendKeyboardEvent(short, char, char)
    {
        return 0;
    }
}
void native_agc_set_hud_enabled(int enabled)
{
    hud_enabled = enabled;
    ++hud_toggles;
}
int native_agc_hud_enabled(void)
{
    return hud_enabled;
}
void native_agc_set_keyboard_state(int, uint32_t, int)
{
}

static ps5_controller_state_t state;

// A fresh stream: the first listed user started the app and every pad is on.
static int begin(std::initializer_list<int32_t> users)
{
    unsigned index = 0;

    memset(pads, 0, sizeof(pads));
    for (int32_t &user : signed_in)
        user = -1;
    for (const int32_t user : users)
    {
        signed_in[index++] = user;
        pads[user].connected = 1;
    }
    scan_result = 0;
    opens = closes = notifications = hud_toggles = 0;
    hud_enabled = 0;
    fail_arrival = false;
    sent.clear();
    assert(ps5_controller_init(&state) == 0);
    return ps5_controller_launch_mask(&state);
}

static void sign_in(std::initializer_list<int32_t> users)
{
    unsigned index = 0;

    for (int32_t &user : signed_in)
        user = -1;
    for (const int32_t user : users)
        signed_in[index++] = user;
    state.next_user_scan_us = 0; // The next poll looks at the user list again.
}

static void poll()
{
    sent.clear();
    ps5_controllers_poll(&state);
}

static bool is(const Sent &event, bool arrival, int number, int mask, int buttons = 0)
{
    return event.arrival == arrival && event.number == number && event.mask == mask &&
           event.buttons == buttons;
}

static void a_single_user_streams_as_before()
{
    assert(begin({1}) == 1 && opens == 1);
    poll();
    assert(sent.size() == 2 && is(sent[0], true, 0, 1) && is(sent[1], false, 0, 1));
    pads[1].buttons = PS5_PAD_BUTTON_CROSS;
    poll();
    assert(sent.size() == 1 && is(sent[0], false, 0, 1, A_FLAG));
    poll(); // Unchanged input is not repeated inside the keep-alive interval.
    assert(sent.empty());
    sent.clear();
    ps5_controller_stop(&state);
    assert(sent.size() == 1 && is(sent[0], false, 0, 0));
    ps5_controller_shutdown(&state);
    assert(closes == 1 && notifications == 0 && state.peak_controllers == 1);
}

static void a_second_controller_joins_and_leaves()
{
    assert(begin({1}) == 1);
    poll();
    pads[2].connected = 1;
    sign_in({1, 2});
    poll();
    assert(opens == 2 && state.active_mask == 3 && notifications == 1);
    assert(sent.size() == 2 && is(sent[0], true, 1, 3) && is(sent[1], false, 1, 3));
    // Every packet names both controllers, whichever one it is about.
    pads[1].buttons = PS5_PAD_BUTTON_CROSS;
    pads[2].buttons = PS5_PAD_BUTTON_CIRCLE;
    poll();
    assert(sent.size() == 2 && is(sent[0], false, 0, 3, A_FLAG) &&
           is(sent[1], false, 1, 3, B_FLAG));
    sign_in({1});
    poll();
    assert(sent.size() == 1 && is(sent[0], false, 1, 1) && closes == 1 && notifications == 2);
    pads[1].buttons = 0;
    poll();
    assert(sent.size() == 1 && is(sent[0], false, 0, 1));
    assert(state.extra_arrivals == 1 && state.extra_removals == 1 && state.peak_controllers == 2);
    ps5_controller_shutdown(&state);
    assert(closes == 2);
}

static void numbers_are_reused_lowest_first()
{
    assert(begin({1, 2, 3, 4}) == 0xf && opens == 4);
    poll();
    assert(sent.size() == 8 && is(sent[0], true, 0, 1) && is(sent[2], true, 1, 3) &&
           is(sent[4], true, 2, 7) && is(sent[6], true, 3, 15));
    sign_in({1, 2, -1, 4});
    poll();
    assert(sent.size() == 1 && is(sent[0], false, 2, 0xb) && state.active_mask == 0xb);
    pads[5].connected = 1;
    sign_in({1, 2, 5, 4});
    poll();
    assert(sent.size() == 2 && is(sent[0], true, 2, 0xf) && state.extra[1].user_id == 5);
    assert(state.extra[1].open && state.extra[1].handle == 105);
    ps5_controller_shutdown(&state);

    // Three extra pads exist: a fourth extra user waits without disturbing them.
    assert(begin({1}) == 1);
    for (int user = 2; user <= 5; ++user)
        pads[user].connected = 1;
    sign_in({2, 3, 4, 5});
    poll();
    assert(opens == 4 && !pads[5].opened && state.active_mask == 0xf);
    ps5_controller_shutdown(&state);
}

static void a_switched_off_controller_leaves_until_it_returns()
{
    assert(begin({1}) == 1);
    sign_in({1, 2});
    poll(); // Signed in, but the controller is off.
    assert(opens == 2 && sent.size() == 2 && state.active_mask == 1);
    pads[2].connected = 1;
    poll();
    assert(sent.size() == 2 && is(sent[0], true, 1, 3));
    pads[2].silent = true; // No new sample: the controller keeps its place.
    poll();
    assert(sent.empty() && state.active_mask == 3);
    pads[2].silent = false;
    pads[2].connected = 0;
    poll();
    assert(sent.size() == 1 && is(sent[0], false, 1, 1) && closes == 0 && pads[2].opened);
    poll();
    assert(sent.empty());
    pads[2].connected = 1;
    pads[2].buttons = PS5_PAD_BUTTON_SQUARE;
    poll();
    assert(sent.size() == 2 && is(sent[0], true, 1, 3) && is(sent[1], false, 1, 3, X_FLAG));
    assert(state.extra_arrivals == 2 && state.extra_removals == 1 && notifications == 3);
    ps5_controller_shutdown(&state);
}

static void every_controller_reaches_the_stream_shortcuts()
{
    begin({1, 2});
    poll();
    pads[2].buttons = PS5_PAD_BUTTON_TOUCH_PAD | PS5_PAD_BUTTON_R1 | PS5_PAD_BUTTON_CROSS;
    poll();
    // The statistics chord is not forwarded; the other button is.
    assert(hud_toggles == 1 && hud_enabled == 1 && is(sent.back(), false, 1, 3, A_FLAG));
    poll();
    assert(hud_toggles == 1); // Held, not pressed again.
    pads[2].buttons = 0;
    poll();
    pads[2].buttons = PS5_PAD_BUTTON_TOUCH_PAD | PS5_PAD_BUTTON_R1;
    poll();
    assert(hud_toggles == 2 && hud_enabled == 0);
    // The keyboard and mouse chords belong to the first controller only.
    pads[2].buttons = PS5_PAD_BUTTON_TOUCH_PAD | PS5_PAD_BUTTON_TRIANGLE;
    poll();
    assert(!state.keyboard_mode && is(sent.back(), false, 1, 3, TOUCHPAD_FLAG | Y_FLAG));
    pads[2].buttons = PS5_PAD_BUTTON_TOUCH_PAD | PS5_PAD_BUTTON_SQUARE;
    poll();
    assert(!state.mouse_mode && is(sent.back(), false, 1, 3, TOUCHPAD_FLAG | X_FLAG));
    // The system menu is open: nothing a player presses reaches the host.
    pads[2].buttons = PS5_PAD_BUTTON_INTERCEPTED | PS5_PAD_BUTTON_CROSS;
    poll();
    assert(is(sent.back(), false, 1, 3, 0) && !state.requested_stop);
    pads[2].buttons = PS5_PAD_BUTTON_TOUCH_PAD | PS5_PAD_BUTTON_L1;
    poll();
    assert(state.requested_stop == 1);
    ps5_controller_shutdown(&state);
}

static void stopping_removes_every_controller()
{
    assert(begin({1, 2, 3}) == 7);
    poll();
    const unsigned before = notifications;
    sent.clear();
    ps5_controller_stop(&state);
    assert(sent.size() == 3 && is(sent[0], false, 2, 3) && is(sent[1], false, 1, 1) &&
           is(sent[2], false, 0, 0));
    assert(state.active_mask == 0 && notifications == before);
    ps5_controller_shutdown(&state);
    assert(closes == 3 && !pads[1].opened && !pads[2].opened && !pads[3].opened);
}

static void failures_are_counted_and_retried()
{
    assert(begin({1}) == 1);
    pads[2].refuse_open = true;
    pads[2].connected = 1;
    sign_in({1, 2});
    poll();
    assert(state.extra_open_errors == 1 && state.extra_open_result == -2 && opens == 1);
    state.next_user_scan_us = 0;
    poll();
    assert(state.extra_open_errors == 2);
    pads[2].refuse_open = false;
    fail_arrival = true; // The host is not ready for the new controller yet.
    state.next_user_scan_us = 0;
    poll();
    assert(opens == 2 && state.active_mask == 1 && state.extra_send_errors == 1 &&
           notifications == 0);
    fail_arrival = false;
    poll();
    assert(state.active_mask == 3 && state.extra_arrivals == 1);
    // A failed user list or pad read changes nothing.
    scan_result = -5;
    state.next_user_scan_us = 0;
    pads[2].read_error = true;
    poll();
    assert(state.user_scan_errors == 1 && state.extra_read_errors == 1 && state.active_mask == 3 &&
           closes == 0);
    ps5_controller_shutdown(&state);
}

int main()
{
    a_single_user_streams_as_before();
    a_second_controller_joins_and_leaves();
    numbers_are_reused_lowest_first();
    a_switched_off_controller_leaves_until_it_returns();
    every_controller_reaches_the_stream_shortcuts();
    stopping_removes_every_controller();
    failures_are_counted_and_retried();
    printf("Multiple controller checks PASS\n");
    return 0;
}
