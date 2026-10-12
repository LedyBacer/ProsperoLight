/*
 * ps5-native-app-boilerplate - ProsperoLight component.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "certgen.h"
#include "gs_errors.h"
#include "gs_log.h"

#include <mbedtls/rsa.h>
#include <mbedtls/x509_crt.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CERT_FILE "cert.pem"
#define KEY_FILE "key.pem"

const char *gs_error = "";

void bytes_to_hex(const unsigned char *input, char *output, size_t length)
{
    static const char digits[] = "0123456789abcdef";
    for (size_t index = 0; index < length; ++index)
    {
        output[index * 2] = digits[input[index] >> 4];
        output[index * 2 + 1] = digits[input[index] & 15];
    }
    output[length * 2] = '\0';
}

void hex_to_bytes(const char *input, unsigned char *output, size_t *output_length)
{
    size_t written = 0;
    size_t length = strlen(input);
    for (size_t index = 0; index + 1 < length; index += 2)
    {
        unsigned int value;
        if (sscanf(input + index, "%2x", &value) != 1)
            break;
        output[written++] = (unsigned char)value;
    }
    if (output_length)
        *output_length = written;
}

int rng_fill(client_identity_t *identity, unsigned char *buffer, size_t length)
{
    return mbedtls_ctr_drbg_random(&identity->rng, buffer, length) == 0 ? GS_OK : GS_FAILED;
}

void uuid_v4(client_identity_t *identity, char output[37])
{
    unsigned char bytes[16];
    (void)mbedtls_ctr_drbg_random(&identity->rng, bytes, sizeof(bytes));
    bytes[6] = (bytes[6] & 0x0f) | 0x40;
    bytes[8] = (bytes[8] & 0x3f) | 0x80;
    snprintf(output, 37,
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-"
             "%02x%02x%02x%02x%02x%02x",
             bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7],
             bytes[8], bytes[9], bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
}

static int read_text_file(const char *path, char *buffer, size_t buffer_size)
{
    FILE *file = fopen(path, "rb");
    size_t count;
    if (!file)
        return -1;
    count = fread(buffer, 1, buffer_size - 1, file);
    if (fclose(file) != 0 || count == 0)
    {
        buffer[0] = '\0';
        return -1;
    }
    buffer[count] = '\0';
    return 0;
}

static int write_text_file_atomic(const char *path, const char *temporary, const char *contents)
{
    FILE *file;
    size_t length = strlen(contents);
    size_t written;

    (void)unlink(temporary);
    file = fopen(temporary, "wb");
    if (!file)
        return -1;
    written = fwrite(contents, 1, length, file);
    if (fclose(file) != 0 || written != length)
    {
        (void)unlink(temporary);
        return -1;
    }
    if (rename(temporary, path) != 0)
    {
        (void)unlink(temporary);
        return -1;
    }
    return 0;
}

static int generate_identity(client_identity_t *identity)
{
    mbedtls_x509write_cert certificate;
    unsigned char serial[1] = {1};
    int result;

    mbedtls_x509write_crt_init(&certificate);
    result = mbedtls_pk_setup(&identity->key, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA));
    if (result != 0)
        goto failed;
    result = mbedtls_rsa_gen_key(mbedtls_pk_rsa(identity->key), mbedtls_ctr_drbg_random,
                                 &identity->rng, 2048, 65537);
    if (result != 0)
        goto failed;
    mbedtls_x509write_crt_set_subject_key(&certificate, &identity->key);
    mbedtls_x509write_crt_set_issuer_key(&certificate, &identity->key);
    mbedtls_x509write_crt_set_md_alg(&certificate, MBEDTLS_MD_SHA256);
    result = mbedtls_x509write_crt_set_subject_name(&certificate, "CN=NVIDIA GameStream Client");
    if (result == 0)
        result = mbedtls_x509write_crt_set_issuer_name(&certificate, "CN=NVIDIA GameStream Client");
    if (result == 0)
        result = mbedtls_x509write_crt_set_serial_raw(&certificate, serial, sizeof(serial));
    if (result == 0)
        result =
            mbedtls_x509write_crt_set_validity(&certificate, "20240101000000", "20440101000000");
    if (result == 0)
        result = mbedtls_x509write_crt_pem(&certificate, (unsigned char *)identity->cert_pem,
                                           sizeof(identity->cert_pem), mbedtls_ctr_drbg_random,
                                           &identity->rng);
    if (result == 0)
        result = mbedtls_pk_write_key_pem(&identity->key, (unsigned char *)identity->key_pem,
                                          sizeof(identity->key_pem));
    if (result == 0)
    {
        mbedtls_x509write_crt_free(&certificate);
        return GS_OK;
    }

failed:
    mbedtls_x509write_crt_free(&certificate);
    gs_error = "Could not generate client certificate";
    return GS_FAILED;
}

static int parse_identity(client_identity_t *identity)
{
    int result = mbedtls_pk_parse_key(&identity->key, (const unsigned char *)identity->key_pem,
                                      strlen(identity->key_pem) + 1, NULL, 0,
                                      mbedtls_ctr_drbg_random, &identity->rng);
    if (result == 0)
        result = mbedtls_x509_crt_parse(&identity->cert, (const unsigned char *)identity->cert_pem,
                                        strlen(identity->cert_pem) + 1);
    if (result == 0)
        result = mbedtls_pk_check_pair(&identity->cert.pk, &identity->key, mbedtls_ctr_drbg_random,
                                       &identity->rng);
    return result;
}

static int identity_init_mode(client_identity_t *identity, const char *key_directory, int create)
{
    static const char personalization[] = "moonlight-ps5";
    char certificate_path[256];
    char certificate_temporary[256];
    char key_path[256];
    char key_temporary[256];
    int loaded;

    gs_error = "";
    memset(identity, 0, sizeof(*identity));
    mbedtls_pk_init(&identity->key);
    mbedtls_x509_crt_init(&identity->cert);
    mbedtls_entropy_init(&identity->entropy);
    mbedtls_ctr_drbg_init(&identity->rng);
    if (mbedtls_ctr_drbg_seed(&identity->rng, mbedtls_entropy_func, &identity->entropy,
                              (const unsigned char *)personalization,
                              sizeof(personalization) - 1) != 0)
    {
        gs_error = "Could not initialize client RNG";
        return GS_FAILED;
    }
    if (create && mkdir(key_directory, 0755) != 0 && errno != EEXIST)
    {
        gs_error = "Could not create identity directory";
        return GS_IO_ERROR;
    }
    if (snprintf(certificate_path, sizeof(certificate_path), "%s/%s", key_directory, CERT_FILE) >=
            (int)sizeof(certificate_path) ||
        snprintf(certificate_temporary, sizeof(certificate_temporary), "%s/%s.tmp", key_directory,
                 CERT_FILE) >= (int)sizeof(certificate_temporary) ||
        snprintf(key_path, sizeof(key_path), "%s/%s", key_directory, KEY_FILE) >=
            (int)sizeof(key_path) ||
        snprintf(key_temporary, sizeof(key_temporary), "%s/%s.tmp", key_directory, KEY_FILE) >=
            (int)sizeof(key_temporary))
    {
        gs_error = "Identity path is too long";
        return GS_INVALID;
    }

    loaded =
        read_text_file(certificate_path, identity->cert_pem, sizeof(identity->cert_pem)) == 0 &&
        read_text_file(key_path, identity->key_pem, sizeof(identity->key_pem)) == 0;
    if (loaded && parse_identity(identity) != 0)
    {
        LOGW("stored client identity is invalid; regenerating");
        mbedtls_pk_free(&identity->key);
        mbedtls_x509_crt_free(&identity->cert);
        mbedtls_pk_init(&identity->key);
        mbedtls_x509_crt_init(&identity->cert);
        loaded = 0;
    }
    if (!loaded)
    {
        if (!create)
        {
            gs_error = "NOT_PAIRED: existing client identity is missing or invalid";
            return GS_WRONG_STATE;
        }
        LOGI("generating persistent RSA client identity in %s", key_directory);
        if (generate_identity(identity) != GS_OK)
            return GS_FAILED;
        if (write_text_file_atomic(certificate_path, certificate_temporary, identity->cert_pem) !=
                0 ||
            write_text_file_atomic(key_path, key_temporary, identity->key_pem) != 0)
        {
            gs_error = "Could not save generated client identity";
            return GS_IO_ERROR;
        }
        if (mbedtls_x509_crt_parse(&identity->cert, (const unsigned char *)identity->cert_pem,
                                   strlen(identity->cert_pem) + 1) != 0)
        {
            gs_error = "Generated client certificate is invalid";
            return GS_FAILED;
        }
    }
    else
    {
        LOGI("loaded persistent client identity from %s", key_directory);
    }
    if (strlen(identity->cert_pem) * 2 + 1 > sizeof(identity->cert_hex))
    {
        gs_error = "Client certificate is too large";
        return GS_INVALID;
    }
    bytes_to_hex((const unsigned char *)identity->cert_pem, identity->cert_hex,
                 strlen(identity->cert_pem));
    return GS_OK;
}

int identity_init(client_identity_t *identity, const char *key_directory)
{
    return identity_init_mode(identity, key_directory, 1);
}
int identity_init_existing(client_identity_t *identity, const char *key_directory)
{
    return identity_init_mode(identity, key_directory, 0);
}

int identity_forget(const char *key_directory)
{
    char certificate_path[256];
    char certificate_temporary[256];
    char key_path[256];
    char key_temporary[256];

    gs_error = "";
    if (!key_directory || !key_directory[0] ||
        snprintf(certificate_path, sizeof(certificate_path), "%s/%s", key_directory, CERT_FILE) >=
            (int)sizeof(certificate_path) ||
        snprintf(certificate_temporary, sizeof(certificate_temporary), "%s/%s.tmp", key_directory,
                 CERT_FILE) >= (int)sizeof(certificate_temporary) ||
        snprintf(key_path, sizeof(key_path), "%s/%s", key_directory, KEY_FILE) >=
            (int)sizeof(key_path) ||
        snprintf(key_temporary, sizeof(key_temporary), "%s/%s.tmp", key_directory, KEY_FILE) >=
            (int)sizeof(key_temporary))
    {
        gs_error = "Identity path is too long";
        return GS_INVALID;
    }

    (void)unlink(certificate_temporary);
    (void)unlink(key_temporary);
    if ((unlink(certificate_path) != 0 && errno != ENOENT) ||
        (unlink(key_path) != 0 && errno != ENOENT))
    {
        gs_error = "Could not remove the local client identity";
        return GS_IO_ERROR;
    }
    return GS_OK;
}

void identity_free(client_identity_t *identity)
{
    mbedtls_pk_free(&identity->key);
    mbedtls_x509_crt_free(&identity->cert);
    mbedtls_ctr_drbg_free(&identity->rng);
    mbedtls_entropy_free(&identity->entropy);
}
