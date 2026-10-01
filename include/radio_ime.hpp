/*
 * ps5-native-app-boilerplate - ProsperoLight component.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef RADIO_IME_HPP
#define RADIO_IME_HPP

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*radio_ime_result_fn)(const char * text, void * user_data);

bool radio_ime_init(void);
bool radio_ime_request(const char *initial_text, const char *title,
                       const char *placeholder,
                       radio_ime_result_fn callback, void *user_data);
bool radio_ime_request_password(const char *title, const char *placeholder,
                                radio_ime_result_fn callback, void *user_data);
/* Number pad for up to max_digits digits. */
bool radio_ime_request_number(const char *initial_text, unsigned max_digits,
                              const char *title, const char *placeholder,
                              radio_ime_result_fn callback, void *user_data);
bool radio_ime_busy(void);
void radio_ime_poll(void);
void radio_ime_cancel(void);
void radio_ime_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
