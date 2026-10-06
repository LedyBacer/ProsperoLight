/*
 * ps5-native-app-boilerplate - ProsperoLight component.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define MBEDTLS_ALLOW_PRIVATE_ACCESS

#include "client.h"
#include "../../include/server_endpoint.h"
#include "gs_errors.h"
#include "gs_http.h"
#include "gs_log.h"

#include <mbedtls/aes.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha1.h>
#include <mbedtls/sha256.h>

#include <arpa/inet.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RSA_SIGNATURE_LENGTH 256u

static void hash_data(int use_sha256, const unsigned char *input, size_t length,
                      unsigned char output[32])
{
    memset(output, 0, 32);
    if (use_sha256)
        (void)mbedtls_sha256(input, length, output, 0);
    else
        (void)mbedtls_sha1(input, length, output);
}

static int aes_ecb(int encrypt, const unsigned char key[16], const unsigned char *input,
                   size_t length, unsigned char *output)
{
    mbedtls_aes_context aes;
    int result;

    if ((length & 15) != 0)
        return GS_INVALID;
    mbedtls_aes_init(&aes);
    result =
        encrypt ? mbedtls_aes_setkey_enc(&aes, key, 128) : mbedtls_aes_setkey_dec(&aes, key, 128);
    for (size_t offset = 0; result == 0 && offset < length; offset += 16)
        result = mbedtls_aes_crypt_ecb(&aes, encrypt ? MBEDTLS_AES_ENCRYPT : MBEDTLS_AES_DECRYPT,
                                       input + offset, output + offset);
    mbedtls_aes_free(&aes);
    return result == 0 ? GS_OK : GS_FAILED;
}

static int sign_message(client_identity_t *identity, const unsigned char *message, size_t length,
                        unsigned char *signature, size_t capacity, size_t *signature_length)
{
    unsigned char hash[32];
    (void)mbedtls_sha256(message, length, hash, 0);
    return mbedtls_pk_sign(&identity->key, MBEDTLS_MD_SHA256, hash, sizeof(hash), signature,
                           capacity, signature_length, mbedtls_ctr_drbg_random, &identity->rng) == 0
               ? GS_OK
               : GS_FAILED;
}

static bool verify_signature(const unsigned char *data, size_t data_length,
                             const unsigned char *signature, size_t signature_length,
                             const char *certificate_pem)
{
    mbedtls_x509_crt certificate;
    unsigned char hash[32];
    bool valid = false;

    mbedtls_x509_crt_init(&certificate);
    if (mbedtls_x509_crt_parse(&certificate, (const unsigned char *)certificate_pem,
                               strlen(certificate_pem) + 1) == 0)
    {
        (void)mbedtls_sha256(data, data_length, hash, 0);
        valid = mbedtls_pk_verify(&certificate.pk, MBEDTLS_MD_SHA256, hash, sizeof(hash), signature,
                                  signature_length) == 0;
    }
    mbedtls_x509_crt_free(&certificate);
    return valid;
}

__attribute__((format(printf, 4, 5))) static int
api_get(gs_server_t *server, int https, http_response_t *response, const char *format, ...)
{
    char path[8192];
    va_list arguments;
    int length;

    va_start(arguments, format);
    length = vsnprintf(path, sizeof(path), format, arguments);
    va_end(arguments);
    if (length <= 0 || length >= (int)sizeof(path))
    {
        gs_error = "NVHTTP request path is too long";
        return GS_INVALID;
    }
    return http_get(server->address, https ? server->https_port : server->http_port, https, path,
                    response);
}

static int response_field(http_response_t *response, const char *field, char **output)
{
    *output = NULL;
    if (xml_status(response->body, response->length) != 200)
    {
        gs_error = "Sunshine returned an NVHTTP error status";
        return GS_ERROR;
    }
    return xml_search(response->body, response->length, field, output);
}

static int load_server_info(gs_server_t *server, bool https)
{
    char uuid[37];
    http_response_t response = {0};
    char *paired = NULL;
    char *current_game = NULL;
    char *state = NULL;
    char *https_port = NULL;
    char *app_version = NULL;
    char *gfe_version = NULL;
    char *codec_mode = NULL;
    char *hostname = NULL;
    char *unique_id = NULL;
    int result;

    uuid_v4(server->identity, uuid);
    result =
        api_get(server, https, &response, "/serverinfo?uniqueid=%s&uuid=%s", GS_UNIQUE_ID, uuid);
    if (result != GS_OK)
        return result;
    result = GS_INVALID;
    if (xml_status(response.body, response.length) != 200)
    {
        gs_error = "serverinfo returned an error status";
        goto done;
    }
    if (xml_search(response.body, response.length, "currentgame", &current_game) != GS_OK ||
        xml_search(response.body, response.length, "PairStatus", &paired) != GS_OK ||
        xml_search(response.body, response.length, "appversion", &app_version) != GS_OK ||
        xml_search(response.body, response.length, "state", &state) != GS_OK)
        goto done;
    if (!current_game[0] || !paired[0] || !app_version[0] || !state[0])
        goto done;
    (void)xml_search(response.body, response.length, "HttpsPort", &https_port);
    (void)xml_search(response.body, response.length, "GfeVersion", &gfe_version);
    (void)xml_search(response.body, response.length, "ServerCodecModeSupport", &codec_mode);
    (void)xml_search(response.body, response.length, "hostname", &hostname);
    (void)xml_search(response.body, response.length, "uniqueid", &unique_id);
    server->host_capabilities = 0;
    {
        const char *tags[] = {"VirtualDisplayCapable", "VirtualDisplayDriverReady",
                              "VirtualDisplayHDRCapable", "FrameLimiterSupported",
                              "VirtualDisplayFrameLimiterEnabled"};
        for (unsigned i = 0; i < 5; ++i)
        {
            char *value = NULL;
            if (xml_search(response.body, response.length, tags[i], &value) == GS_OK && value &&
                (!strcmp(value, "1") || !strcmp(value, "true")))
                server->host_capabilities |= 1u << i;
            free(value);
        }
    }
    server->paired = !strcmp(paired, "1");
    server->current_game = atoi(current_game);
    server->server_major_version = atoi(app_version);
    server->server_codec_mode_support = codec_mode && codec_mode[0] ? atoi(codec_mode) : SCM_H264;
    server->is_nvidia_software = strstr(state, "MJOLNIR") != NULL;
    snprintf(server->app_version, sizeof(server->app_version), "%s", app_version);
    if (gfe_version)
        snprintf(server->gfe_version, sizeof(server->gfe_version), "%s", gfe_version);
    if (hostname)
        snprintf(server->hostname, sizeof(server->hostname), "%s", hostname);
    if (unique_id)
        snprintf(server->unique_id, sizeof(server->unique_id), "%s", unique_id);
    /* Older servers omit HttpsPort. Malformed values must not wrap to another port. */
    server->https_port = server->http_port > 5 ? server->http_port - 5 : 47984;
    uint16_t negotiated_https_port;
    if (server_port_parse(https_port, &negotiated_https_port))
        server->https_port = negotiated_https_port;
    if (!strstr(state, "_SERVER_BUSY"))
        server->current_game = 0;
    result = GS_OK;

done:
    free(paired);
    free(current_game);
    free(state);
    free(https_port);
    free(app_version);
    free(gfe_version);
    free(codec_mode);
    free(hostname);
    free(unique_id);
    http_response_free(&response);
    return result;
}

int gs_init(gs_server_t *server, client_identity_t *identity, const char *address,
            unsigned short http_port)
{
    int result;

    gs_error = "";
    memset(server, 0, sizeof(*server));
    server->identity = identity;
    if (!server_endpoint_parse(address, server->address, sizeof(server->address), http_port,
                               &server->http_port))
    {
        gs_error = "Invalid server address or HTTP port";
        return GS_INVALID;
    }
    result = load_server_info(server, false);
    if (result != GS_OK)
        return result;
    LOGI("HTTP serverinfo paired=%d https=%u app=%s codecs=0x%x", server->paired,
         server->https_port, server->app_version, server->server_codec_mode_support);
    if (load_server_info(server, true) != GS_OK)
        server->paired = false;
    if (!server->paired)
        gs_error = "";
    LiInitializeServerInformation(&server->server_info);
    server->server_info.address = server->address;
    server->server_info.serverInfoAppVersion = server->app_version;
    server->server_info.serverInfoGfeVersion = server->gfe_version;
    server->server_info.serverCodecModeSupport = server->server_codec_mode_support;
    return GS_OK;
}

static int check_paired(http_response_t *response)
{
    char *paired = NULL;
    int result = response_field(response, "paired", &paired);
    if (result == GS_OK && (!paired || strcmp(paired, "1")))
    {
        gs_error = "Sunshine rejected pairing";
        result = GS_FAILED;
    }
    free(paired);
    http_response_free(response);
    return result;
}

static int valid_pin(const char pin[4])
{
    return pin && pin[0] >= '0' && pin[0] <= '9' && pin[1] >= '0' && pin[1] <= '9' &&
           pin[2] >= '0' && pin[2] <= '9' && pin[3] >= '0' && pin[3] <= '9' && pin[4] == '\0';
}

int gs_pair(gs_server_t *server, const char pin[4])
{
    char uuid[37];
    http_response_t response = {0};
    char *field = NULL;
    client_identity_t *identity = server->identity;
    unsigned char salt[16];
    char salt_hex[33];
    unsigned char salt_pin[20];
    unsigned char aes_key[32];
    int use_sha256;
    int hash_length;
    int result;

    gs_error = "";
    if (!valid_pin(pin))
    {
        gs_error = "Pairing PIN must contain exactly four digits";
        return GS_INVALID;
    }
    if (server->paired)
    {
        gs_error = "Client is already paired";
        return GS_WRONG_STATE;
    }
    if (server->current_game)
    {
        gs_error = "Close the active Sunshine session before pairing";
        return GS_WRONG_STATE;
    }
    if (rng_fill(identity, salt, sizeof(salt)) != GS_OK)
        return GS_FAILED;
    bytes_to_hex(salt, salt_hex, sizeof(salt));
    uuid_v4(identity, uuid);
    LOGI("pair phase 1: waiting for Sunshine PIN entry");
    result = api_get(server, 0, &response,
                     "/pair?uniqueid=%s&uuid=%s&devicename=roth&updateState=1"
                     "&phrase=getservercert&salt=%s&clientcert=%s",
                     GS_UNIQUE_ID, uuid, salt_hex, identity->cert_hex);
    if (result != GS_OK)
        return result;
    if (response_field(&response, "paired", &field) != GS_OK || !field || strcmp(field, "1"))
    {
        gs_error = "Sunshine rejected pairing phase 1";
        result = GS_FAILED;
        goto response_failed;
    }
    free(field);
    field = NULL;
    if (xml_search(response.body, response.length, "plaincert", &field) != GS_OK || !field ||
        !field[0])
    {
        gs_error = "Sunshine did not return its pairing certificate";
        result = GS_FAILED;
        goto response_failed;
    }
    {
        size_t certificate_length = 0;
        if ((strlen(field) & 1) || strlen(field) / 2 >= sizeof(server->server_certificate_pem))
        {
            gs_error = "Sunshine pairing certificate is invalid";
            result = GS_INVALID;
            goto response_failed;
        }
        hex_to_bytes(field, (unsigned char *)server->server_certificate_pem, &certificate_length);
        server->server_certificate_pem[certificate_length] = '\0';
    }
    free(field);
    field = NULL;
    http_response_free(&response);

    use_sha256 = server->server_major_version >= 7;
    hash_length = use_sha256 ? 32 : 20;
    memcpy(salt_pin, salt, sizeof(salt));
    memcpy(salt_pin + sizeof(salt), pin, 4);
    hash_data(use_sha256, salt_pin, sizeof(salt_pin), aes_key);

    {
        unsigned char challenge[16];
        unsigned char encrypted[16];
        char challenge_hex[33];
        unsigned char response_encrypted[64];
        unsigned char challenge_response[64];
        size_t response_length = 0;
        unsigned char client_secret[16];
        const unsigned char *certificate_signature;
        size_t certificate_signature_length;
        unsigned char proof[16 + RSA_SIGNATURE_LENGTH + 16];
        size_t proof_length = 0;
        unsigned char proof_hash[32];
        unsigned char proof_encrypted[32];
        char proof_hex[65];
        unsigned char pairing_secret[16 + RSA_SIGNATURE_LENGTH];
        size_t pairing_secret_length = 0;
        unsigned char client_signature[RSA_SIGNATURE_LENGTH];
        size_t client_signature_length = 0;
        unsigned char client_pairing_secret[16 + RSA_SIGNATURE_LENGTH];
        char client_pairing_secret_hex[(16 + RSA_SIGNATURE_LENGTH) * 2 + 1];

        if (rng_fill(identity, challenge, sizeof(challenge)) != GS_OK ||
            aes_ecb(1, aes_key, challenge, sizeof(challenge), encrypted) != GS_OK)
            goto pairing_failed;
        bytes_to_hex(encrypted, challenge_hex, sizeof(encrypted));
        uuid_v4(identity, uuid);
        LOGI("pair phase 2: client challenge");
        result = api_get(server, 0, &response,
                         "/pair?uniqueid=%s&uuid=%s&devicename=roth&updateState=1"
                         "&clientchallenge=%s",
                         GS_UNIQUE_ID, uuid, challenge_hex);
        if (result != GS_OK)
            goto pairing_failed;
        if (response_field(&response, "paired", &field) != GS_OK || !field || strcmp(field, "1"))
        {
            gs_error = "Sunshine rejected pairing phase 2";
            result = GS_FAILED;
            goto response_failed;
        }
        free(field);
        field = NULL;
        if (xml_search(response.body, response.length, "challengeresponse", &field) != GS_OK ||
            !field || (strlen(field) & 1) || strlen(field) / 2 > sizeof(response_encrypted))
        {
            gs_error = "Sunshine challenge response is invalid";
            result = GS_INVALID;
            goto response_failed;
        }
        hex_to_bytes(field, response_encrypted, &response_length);
        if (response_length < (size_t)hash_length + 16 ||
            aes_ecb(0, aes_key, response_encrypted, response_length, challenge_response) != GS_OK)
        {
            gs_error = "Sunshine challenge response is malformed";
            result = GS_INVALID;
            goto response_failed;
        }
        free(field);
        field = NULL;
        http_response_free(&response);

        if (rng_fill(identity, client_secret, sizeof(client_secret)) != GS_OK)
            goto pairing_failed;
        certificate_signature = identity->cert.sig.p;
        certificate_signature_length = identity->cert.sig.len;
        if (certificate_signature_length != RSA_SIGNATURE_LENGTH)
        {
            gs_error = "Client certificate is not RSA-2048";
            result = GS_INVALID;
            goto pairing_failed;
        }
        memcpy(proof, challenge_response + hash_length, 16);
        proof_length += 16;
        memcpy(proof + proof_length, certificate_signature, certificate_signature_length);
        proof_length += certificate_signature_length;
        memcpy(proof + proof_length, client_secret, sizeof(client_secret));
        proof_length += sizeof(client_secret);
        hash_data(use_sha256, proof, proof_length, proof_hash);
        if (aes_ecb(1, aes_key, proof_hash, sizeof(proof_hash), proof_encrypted) != GS_OK)
            goto pairing_failed;
        bytes_to_hex(proof_encrypted, proof_hex, sizeof(proof_encrypted));
        uuid_v4(identity, uuid);
        LOGI("pair phase 3: server challenge response");
        result = api_get(server, 0, &response,
                         "/pair?uniqueid=%s&uuid=%s&devicename=roth&updateState=1"
                         "&serverchallengeresp=%s",
                         GS_UNIQUE_ID, uuid, proof_hex);
        if (result != GS_OK)
            goto pairing_failed;
        if (response_field(&response, "paired", &field) != GS_OK || !field || strcmp(field, "1"))
        {
            gs_error = "Sunshine rejected pairing phase 3 (wrong PIN?)";
            result = GS_FAILED;
            goto response_failed;
        }
        free(field);
        field = NULL;
        if (xml_search(response.body, response.length, "pairingsecret", &field) != GS_OK ||
            !field || (strlen(field) & 1) || strlen(field) / 2 != sizeof(pairing_secret))
        {
            gs_error = "Sunshine pairing secret is malformed";
            result = GS_INVALID;
            goto response_failed;
        }
        hex_to_bytes(field, pairing_secret, &pairing_secret_length);
        free(field);
        field = NULL;
        http_response_free(&response);
        if (!verify_signature(pairing_secret, 16, pairing_secret + 16, pairing_secret_length - 16,
                              server->server_certificate_pem))
        {
            gs_error = "Sunshine pairing signature is invalid";
            result = GS_FAILED;
            goto pairing_failed;
        }
        if (sign_message(identity, client_secret, sizeof(client_secret), client_signature,
                         sizeof(client_signature), &client_signature_length) != GS_OK ||
            client_signature_length != RSA_SIGNATURE_LENGTH)
        {
            gs_error = "Could not sign the client pairing secret";
            result = GS_FAILED;
            goto pairing_failed;
        }
        memcpy(client_pairing_secret, client_secret, sizeof(client_secret));
        memcpy(client_pairing_secret + sizeof(client_secret), client_signature,
               client_signature_length);
        bytes_to_hex(client_pairing_secret, client_pairing_secret_hex,
                     sizeof(client_pairing_secret));
        uuid_v4(identity, uuid);
        LOGI("pair phase 4: signed client secret");
        result = api_get(server, 0, &response,
                         "/pair?uniqueid=%s&uuid=%s&devicename=roth&updateState=1"
                         "&clientpairingsecret=%s",
                         GS_UNIQUE_ID, uuid, client_pairing_secret_hex);
        if (result != GS_OK || check_paired(&response) != GS_OK)
            goto pairing_failed;
    }

    uuid_v4(identity, uuid);
    LOGI("pair confirmation: HTTPS client certificate");
    result = api_get(server, 1, &response,
                     "/pair?uniqueid=%s&uuid=%s&devicename=roth&updateState=1"
                     "&phrase=pairchallenge",
                     GS_UNIQUE_ID, uuid);
    if (result != GS_OK || check_paired(&response) != GS_OK)
        goto pairing_failed;
    server->paired = true;
    LOGI("pairing completed");
    return GS_OK;

response_failed:
    free(field);
    http_response_free(&response);
pairing_failed:
    (void)gs_unpair(server);
    return result == GS_OK ? GS_FAILED : result;
}

int gs_unpair(gs_server_t *server)
{
    char uuid[37];
    http_response_t response = {0};
    int status;
    int result;

    uuid_v4(server->identity, uuid);
    result = api_get(server, 0, &response, "/unpair?uniqueid=%s&uuid=%s", GS_UNIQUE_ID, uuid);
    if (result == GS_OK)
    {
        status = xml_status(response.body, response.length);
        if (status == 200)
        {
            server->paired = false;
        }
        else if (status == 404)
        {
            gs_error = "Sunshine does not support client-side unpair";
            result = GS_UNSUPPORTED_VERSION;
        }
        else
        {
            gs_error = "Sunshine rejected the unpair request";
            result = GS_ERROR;
        }
        http_response_free(&response);
    }
    return result;
}

int gs_applist(gs_server_t *server, app_entry_t **list)
{
    char uuid[37];
    http_response_t response = {0};
    int result;

    gs_error = "";
    uuid_v4(server->identity, uuid);
    result = api_get(server, 1, &response, "/applist?uniqueid=%s&uuid=%s", GS_UNIQUE_ID, uuid);
    if (result != GS_OK)
        return result;
    if (xml_status(response.body, response.length) != 200)
    {
        gs_error = "applist returned an error status";
        result = GS_ERROR;
    }
    else
    {
        result = xml_applist(response.body, response.length, list);
    }
    http_response_free(&response);
    return result;
}

int gs_start_app(gs_server_t *server, STREAM_CONFIGURATION *configuration, int app_id, bool sops,
                 bool local_audio, int gamepad_mask, const gs_host_options_t *options)
{
    char uuid[37];
    http_response_t response = {0};
    char *field = NULL;
    uint32_t key_id = 0;
    char key_hex[sizeof(configuration->remoteInputAesKey) * 2 + 1];
    char host_query[160];
    const char *hdr_capabilities;
    int fps;
    int surround_info;
    int result;

    gs_error = "";
    gamepad_mask &= 0xffff;
    if (options && options->extensions && options->virtual_display == 1 &&
        (server->host_capabilities & 3u) != 3u)
    {
        gs_error = "Host virtual display is unavailable or its driver is not ready";
        return GS_FAILED;
    }
    gs_host_query(host_query, sizeof(host_query), options, (unsigned)gamepad_mask);
    LOGI("Host launch options: gamepads=%x capabilities=%x extensions=%d optional_query=%s",
         (unsigned)gamepad_mask, server->host_capabilities, options && options->extensions,
         host_query[0] ? host_query : "none");
    if (rng_fill(server->identity, (unsigned char *)configuration->remoteInputAesKey,
                 sizeof(configuration->remoteInputAesKey)) != GS_OK)
        return GS_FAILED;
    memset(configuration->remoteInputAesIv, 0, sizeof(configuration->remoteInputAesIv));
    if (rng_fill(server->identity, (unsigned char *)configuration->remoteInputAesIv, 4) != GS_OK)
        return GS_FAILED;
    memcpy(&key_id, configuration->remoteInputAesIv, sizeof(key_id));
    key_id = htonl(key_id);
    bytes_to_hex((unsigned char *)configuration->remoteInputAesKey, key_hex,
                 sizeof(configuration->remoteInputAesKey));
    fps = server->is_nvidia_software && configuration->fps > 60 ? 0 : configuration->fps;
    hdr_capabilities = (configuration->supportedVideoFormats & VIDEO_FORMAT_MASK_10BIT)
                           ? "&hdrMode=1&clientHdrCapVersion=0&clientHdrCapSupportedFlagsInUint32=0"
                             "&clientHdrCapMetaDataId=NV_STATIC_METADATA_TYPE_1"
                             "&clientHdrCapDisplayData=0x0x0x0x0x0x0x0x0x0x0"
                           : "";
    surround_info = SURROUNDAUDIOINFO_FROM_AUDIO_CONFIGURATION(configuration->audioConfiguration);
    uuid_v4(server->identity, uuid);
    result = api_get(server, 1, &response,
                     "/%s?uniqueid=%s&uuid=%s&appid=%d&mode=%dx%dx%d"
                     "&additionalStates=1&sops=%d&rikey=%s&rikeyid=%u"
                     "&localAudioPlayMode=%d&surroundAudioInfo=%d"
                     "&remoteControllersBitmap=%d&gcmap=%d%s%s%s",
                     server->current_game ? "resume" : "launch", GS_UNIQUE_ID, uuid, app_id,
                     configuration->width, configuration->height, fps, sops ? 1 : 0, key_hex,
                     (unsigned)key_id, local_audio ? 1 : 0, surround_info, gamepad_mask,
                     gamepad_mask, hdr_capabilities, LiGetLaunchUrlQueryParameters(), host_query);
    if (result != GS_OK)
        return result;
    if (xml_status(response.body, response.length) != 200)
    {
        gs_error = "launch returned an error status";
        result = GS_ERROR;
        goto done;
    }
    if (xml_search(response.body, response.length, "gamesession", &field) != GS_OK &&
        xml_search(response.body, response.length, "resume", &field) != GS_OK)
    {
        result = GS_INVALID;
        goto done;
    }
    if (!strcmp(field, "0"))
    {
        gs_error = "Sunshine could not start the session";
        result = GS_FAILED;
        goto done;
    }
    free(field);
    field = NULL;
    if (xml_search(response.body, response.length, "sessionUrl0", &field) == GS_OK)
    {
        snprintf(server->rtsp_session_url, sizeof(server->rtsp_session_url), "%s", field);
        server->server_info.rtspSessionUrl = server->rtsp_session_url;
    }
    server->current_game = app_id;
    result = GS_OK;

done:
    free(field);
    http_response_free(&response);
    return result;
}

int gs_quit_app(gs_server_t *server)
{
    char uuid[37];
    http_response_t response = {0};
    char *field = NULL;
    int result;

    uuid_v4(server->identity, uuid);
    result = api_get(server, 1, &response, "/cancel?uniqueid=%s&uuid=%s", GS_UNIQUE_ID, uuid);
    if (result != GS_OK)
        return result;
    result = response_field(&response, "cancel", &field);
    if (result == GS_OK && !strcmp(field, "0"))
    {
        gs_error = "Sunshine could not cancel the session";
        result = GS_FAILED;
    }
    if (result == GS_OK)
        server->current_game = 0;
    free(field);
    http_response_free(&response);
    return result;
}

int gs_refresh_status(gs_server_t *server)
{
    int result;
    if (!server)
        return GS_INVALID;
    result = load_server_info(server, server->paired && server->https_port != 0);
    if (result != GS_OK && server->paired)
        result = load_server_info(server, false);
    return result;
}
