/*
 * ps5-native-app-boilerplate - ProsperoLight component.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <stddef.h>

#ifndef MBEDTLS_CONFIG_FILE
#define MBEDTLS_CONFIG_FILE "mbedtls_ps5_config.h"
#endif

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/pk.h>
#include <mbedtls/x509_crt.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        mbedtls_pk_context key;
        mbedtls_x509_crt cert;
        mbedtls_entropy_context entropy;
        mbedtls_ctr_drbg_context rng;
        char cert_pem[4096];
        char key_pem[4096];
        char cert_hex[8193];
    } client_identity_t;

    int identity_init(client_identity_t *identity, const char *key_directory);
    /* Read-only test session: never generates or overwrites pairing material. */
    int identity_init_existing(client_identity_t *identity, const char *key_directory);
    int identity_forget(const char *key_directory);
    void identity_free(client_identity_t *identity);
    void bytes_to_hex(const unsigned char *input, char *output, size_t length);
    void hex_to_bytes(const char *input, unsigned char *output, size_t *output_length);
    void uuid_v4(client_identity_t *identity, char output[37]);
    int rng_fill(client_identity_t *identity, unsigned char *buffer, size_t length);

#ifdef __cplusplus
}
#endif
