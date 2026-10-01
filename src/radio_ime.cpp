/*
 * ps5-native-app-boilerplate - ProsperoLight component.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "radio_ime.hpp"

#include "radio_input.hpp"

#include <SDL2/SDL.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#define SCE_SYSMODULE_IME_DIALOG UINT16_C(0x0096)
#define SCE_COMMON_DIALOG_ALREADY_INITIALIZED UINT32_C(0x80B80002)
#define IME_TEXT_CHARACTERS 127U
#define IME_TYPE_DEFAULT 0
#define IME_TYPE_NUMBER 4
#define IME_OPTION_NO_AUTO_CAPITALIZATION UINT32_C(0x00000002)
#define IME_OPTION_PASSWORD UINT32_C(0x00000004)
#define IME_OPTION_NO_LEARNING UINT32_C(0x00000020)
#define IME_OPTION_DISABLE_COPY_PASTE UINT32_C(0x00000080)

typedef struct
{
    int32_t user_id;
    int32_t type;
    uint64_t supported_languages;
    int32_t enter_label;
    int32_t input_method;
    void *filter;
    uint32_t option;
    uint32_t max_text_length;
    uint16_t *input_text_buffer;
    float pos_x;
    float pos_y;
    int32_t horizontal_alignment;
    int32_t vertical_alignment;
    const uint16_t *placeholder;
    const uint16_t *title;
    int8_t reserved[16];
} sce_ime_dialog_param_t;

typedef struct
{
    int32_t outcome;
    int8_t reserved[12];
} sce_ime_dialog_result_t;

static_assert(sizeof(sce_ime_dialog_param_t) == 96U, "unexpected PS5 IME parameter layout");
static_assert(sizeof(sce_ime_dialog_result_t) == 16U, "unexpected PS5 IME result layout");

extern "C"
{
    int sceCommonDialogInitialize(void);
    extern int sceImeDialogAbort(void);
    extern int sceImeDialogGetResult(sce_ime_dialog_result_t *result);
    extern int sceImeDialogGetStatus(void);
    extern int sceImeDialogInit(const sce_ime_dialog_param_t *param, const void *extended);
    extern int sceImeDialogTerm(void);
    extern int sceSysmoduleLoadModule(uint16_t module_id);
    extern int sceSysmoduleUnloadModule(uint16_t module_id);
    int sceUserServiceGetForegroundUser(int32_t *user_id);
}

static bool module_loaded;
static bool requested;
static bool active;
static uint64_t started_at;
static char initial_text[IME_TEXT_CHARACTERS * 4U + 1U];
static char requested_placeholder[96];
static char requested_title[64];
static uint32_t requested_option;
static int32_t requested_enter_label;
static int32_t requested_type;
static uint32_t requested_max_length;
static uint16_t text_buffer[IME_TEXT_CHARACTERS + 1U];
static uint16_t placeholder[96];
static uint16_t title[64];
static radio_ime_result_fn result_callback;
static void *result_user_data;

static uint64_t monotonic_milliseconds(void)
{
    struct timespec now = {};
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * UINT64_C(1000) + (uint64_t)now.tv_nsec / UINT64_C(1000000);
}

static void clear_sensitive_text(void)
{
    volatile unsigned char *initial = (volatile unsigned char *)initial_text;
    volatile unsigned char *utf16 = (volatile unsigned char *)text_buffer;

    for (size_t index = 0; index < sizeof(initial_text); ++index)
        initial[index] = 0;
    for (size_t index = 0; index < sizeof(text_buffer); ++index)
        utf16[index] = 0;
}

static void utf8_to_utf16(const char *source, uint16_t *output, size_t capacity)
{
    size_t written = 0;
    const unsigned char *text = (const unsigned char *)(source != NULL ? source : "");
    while (*text != 0 && written + 1U < capacity)
    {
        uint32_t codepoint = *text;
        unsigned bytes = 1;
        if ((text[0] & 0xe0U) == 0xc0U)
        {
            codepoint = text[0] & 0x1fU;
            bytes = 2;
        }
        else if ((text[0] & 0xf0U) == 0xe0U)
        {
            codepoint = text[0] & 0x0fU;
            bytes = 3;
        }
        else if ((text[0] & 0xf8U) == 0xf0U)
        {
            codepoint = text[0] & 0x07U;
            bytes = 4;
        }
        bool valid = bytes == 1;
        if (bytes > 1)
        {
            valid = true;
            for (unsigned i = 1; i < bytes; ++i)
            {
                if (text[i] == 0 || (text[i] & 0xc0U) != 0x80U)
                {
                    valid = false;
                    break;
                }
                codepoint = (codepoint << 6) | (text[i] & 0x3fU);
            }
            const uint32_t minimum = bytes == 2 ? 0x80U : bytes == 3 ? 0x800U : 0x10000U;
            if (codepoint < minimum || codepoint > 0x10ffffU ||
                (codepoint >= 0xd800U && codepoint <= 0xdfffU))
                valid = false;
        }
        if (!valid)
        {
            codepoint = 0xfffdU;
            bytes = 1;
        }
        text += bytes;
        if (codepoint >= 0x10000U)
        {
            if (written + 2U >= capacity)
                break;
            codepoint -= 0x10000U;
            output[written++] = (uint16_t)(0xd800U | (codepoint >> 10));
            output[written++] = (uint16_t)(0xdc00U | (codepoint & 0x3ffU));
        }
        else
            output[written++] = (uint16_t)codepoint;
    }
    output[written] = 0;
}

static void utf16_to_utf8(const uint16_t *source, char *output, size_t capacity)
{
    size_t written = 0;
    for (size_t i = 0; source[i] != 0; ++i)
    {
        uint32_t codepoint = source[i];
        if (codepoint >= 0xd800U && codepoint <= 0xdbffU && source[i + 1U] >= 0xdc00U &&
            source[i + 1U] <= 0xdfffU)
        {
            codepoint = 0x10000U + ((codepoint - 0xd800U) << 10) + (source[++i] - 0xdc00U);
        }
        else if (codepoint >= 0xd800U && codepoint <= 0xdfffU)
            codepoint = 0xfffdU;
        const unsigned bytes = codepoint < 0x80U      ? 1U
                               : codepoint < 0x800U   ? 2U
                               : codepoint < 0x10000U ? 3U
                                                      : 4U;
        if (written + bytes >= capacity)
            break;
        if (bytes == 1U)
            output[written++] = (char)codepoint;
        else
        {
            for (unsigned byte = bytes - 1U; byte != 0; --byte)
            {
                output[written + byte] =
                    (char)(0x80U | ((codepoint >> (6U * (bytes - 1U - byte))) & 0x3fU));
            }
            output[written] = (char)((0xf0U << (4U - bytes)) | (codepoint >> (6U * (bytes - 1U))));
            written += bytes;
        }
    }
    output[written] = '\0';
}

bool radio_ime_init(void)
{
    const int common_dialog = sceCommonDialogInitialize();
    if (common_dialog < 0 && (uint32_t)common_dialog != SCE_COMMON_DIALOG_ALREADY_INITIALIZED)
        return false;
    if (sceSysmoduleLoadModule(SCE_SYSMODULE_IME_DIALOG) < 0)
        return false;
    module_loaded = true;
    return true;
}

static bool request_with_option(const char *value, const char *dialog_title,
                                const char *dialog_placeholder, uint32_t option,
                                int32_t enter_label, radio_ime_result_fn callback, void *user_data,
                                int32_t type = IME_TYPE_DEFAULT,
                                uint32_t max_length = IME_TEXT_CHARACTERS)
{
    if (active || requested || !module_loaded)
        return false;
    requested_type = type;
    requested_max_length =
        max_length && max_length < IME_TEXT_CHARACTERS ? max_length : IME_TEXT_CHARACTERS;
    clear_sensitive_text();
    SDL_strlcpy(initial_text, value != NULL ? value : "", sizeof(initial_text));
    SDL_strlcpy(requested_title, dialog_title != NULL ? dialog_title : "Text entry",
                sizeof(requested_title));
    SDL_strlcpy(requested_placeholder, dialog_placeholder != NULL ? dialog_placeholder : "",
                sizeof(requested_placeholder));
    requested_option = option;
    requested_enter_label = enter_label;
    result_callback = callback;
    result_user_data = user_data;
    requested = true;
    return true;
}

bool radio_ime_request(const char *value, const char *dialog_title, const char *dialog_placeholder,
                       radio_ime_result_fn callback, void *user_data)
{
    return request_with_option(value, dialog_title, dialog_placeholder, 0, 2, callback, user_data);
}

bool radio_ime_request_password(const char *dialog_title, const char *dialog_placeholder,
                                radio_ime_result_fn callback, void *user_data)
{
    const uint32_t option = IME_OPTION_NO_AUTO_CAPITALIZATION | IME_OPTION_PASSWORD |
                            IME_OPTION_NO_LEARNING | IME_OPTION_DISABLE_COPY_PASTE;
    return request_with_option("", dialog_title, dialog_placeholder, option, 3, callback,
                               user_data);
}

bool radio_ime_request_number(const char *value, unsigned max_digits, const char *dialog_title,
                              const char *dialog_placeholder, radio_ime_result_fn callback,
                              void *user_data)
{
    return request_with_option(value, dialog_title, dialog_placeholder, 0, 2, callback, user_data,
                               IME_TYPE_NUMBER, max_digits);
}

bool radio_ime_busy(void)
{
    return requested || active;
}

static void start_requested(void)
{
    int32_t user_id = -1;
    if (sceUserServiceGetForegroundUser(&user_id) < 0)
    {
        requested = false;
        clear_sensitive_text();
        requested_option = 0;
        result_callback = NULL;
        result_user_data = NULL;
        return;
    }
    utf8_to_utf16(initial_text, text_buffer, sizeof(text_buffer) / sizeof(text_buffer[0]));
    utf8_to_utf16(requested_placeholder, placeholder, sizeof(placeholder) / sizeof(placeholder[0]));
    utf8_to_utf16(requested_title, title, sizeof(title) / sizeof(title[0]));
    sce_ime_dialog_param_t param{};

    param.user_id = user_id;
    param.type = requested_type;
    param.enter_label = requested_enter_label;
    param.option = requested_option;
    param.max_text_length = requested_max_length;
    param.input_text_buffer = text_buffer;
    param.horizontal_alignment = 1;
    param.vertical_alignment = 1;
    param.placeholder = placeholder;
    param.title = title;
    active = sceImeDialogInit(&param, NULL) == 0;
    if (!active && param.type != IME_TYPE_DEFAULT)
    {
        // The number pad was refused: the full keyboard still enters digits.
        param.type = IME_TYPE_DEFAULT;
        param.max_text_length = IME_TEXT_CHARACTERS;
        active = sceImeDialogInit(&param, NULL) == 0;
    }
    started_at = monotonic_milliseconds();
    requested = false;
    if (!active)
        clear_sensitive_text();
}

void radio_ime_poll(void)
{
    if (requested && !radio_input_pressed(RADIO_INPUT_CROSS))
        start_requested();
    if (!active)
        return;

    const int status = sceImeDialogGetStatus();
    if (status == 1 || (status == 0 && monotonic_milliseconds() - started_at < 1000U))
        return;
    if (status == 2)
    {
        sce_ime_dialog_result_t result = {};
        if (sceImeDialogGetResult(&result) >= 0 && result.outcome == 0 && result_callback != NULL)
        {
            char text[IME_TEXT_CHARACTERS * 4U + 1U];
            utf16_to_utf8(text_buffer, text, sizeof(text));
            result_callback(text, result_user_data);
            volatile unsigned char *sensitive = (volatile unsigned char *)text;
            for (size_t index = 0; index < sizeof(text); ++index)
                sensitive[index] = 0;
        }
        sceImeDialogTerm();
    }
    clear_sensitive_text();
    requested_option = 0;
    result_callback = NULL;
    result_user_data = NULL;
    active = false;
}

void radio_ime_cancel(void)
{
    requested = false;
    if (active)
        sceImeDialogAbort();
    else
        clear_sensitive_text();
    requested_option = 0;
    result_callback = NULL;
    result_user_data = NULL;
}

void radio_ime_shutdown(void)
{
    radio_ime_cancel();
    if (module_loaded)
    {
        sceSysmoduleUnloadModule(SCE_SYSMODULE_IME_DIALOG);
        module_loaded = false;
    }
    active = false;
    clear_sensitive_text();
}
