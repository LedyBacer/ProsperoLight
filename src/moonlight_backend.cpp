/*
 * ps5-native-app-boilerplate - ProsperoLight component.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "moonlight_backend.hpp"

#include "app_storage.hpp"

#include "gamestream/certgen.h"
#include "gamestream/client.h"
#include "gamestream/gs_errors.h"
#include "gamestream/gs_http.h"
#include "gamestream/mini_xml.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MOONLIGHT_IDENTITY_DIRECTORY (storage::paths().pairing)
#define MOONLIGHT_ARTWORK_SLOTS 6

typedef struct artwork_slot
{
    int app_id;
    unsigned char *data;
    size_t size;
} artwork_slot_t;

static artwork_slot_t artwork_slots[MOONLIGHT_ARTWORK_SLOTS];

extern "C"
{
    int scePthreadCreate(void **thread, const void *attributes, void *(*entry)(void *),
                         void *argument, const char *name);
    int scePthreadDetach(void *thread);
}

typedef struct pairing_job
{
    volatile int state;
    char host[128];
    uint16_t http_port;
    char pin[5];
    moonlight_backend_snapshot_t snapshot;
    client_identity_t identity;
    gs_server_t server;
} pairing_job_t;

static pairing_job_t pairing_job;

int moonlight_backend_refresh(const char *host, uint16_t http_port,
                              moonlight_backend_snapshot_t *snapshot)
{
    client_identity_t identity;
    gs_server_t server;
    app_entry_t *apps = NULL;
    app_entry_t *app;
    int result;

    if (!snapshot || !host || !host[0])
        return GS_INVALID;

    memset(snapshot, 0, sizeof(*snapshot));
    memset(&identity, 0, sizeof(identity));
    memset(&server, 0, sizeof(server));
    snprintf(snapshot->host, sizeof(snapshot->host), "%s", host);
    snapshot->http_port = http_port;

    result = identity_init(&identity, MOONLIGHT_IDENTITY_DIRECTORY);
    if (result != GS_OK)
        goto done;

    http_init(&identity, 0);
    http_set_timeout_ms(3000);
    result = gs_init(&server, &identity, host, http_port);
    if (result != GS_OK)
        goto done;

    snapshot->online = 1;
    snapshot->paired = server.paired ? 1u : 0u;
    snapshot->https_port = server.https_port;
    snapshot->current_app_id = server.current_game;
    snapshot->hevc_supported = (server.server_codec_mode_support & SCM_MASK_HEVC) != 0;
    snapshot->main10_supported = (server.server_codec_mode_support & SCM_HEVC_MAIN10) != 0;
    snprintf(snapshot->server_version, sizeof(snapshot->server_version), "%s", server.app_version);
    snprintf(snapshot->name, sizeof(snapshot->name), "%s",
             server.hostname[0] ? server.hostname : "Sunshine PC");
    snprintf(snapshot->unique_id, sizeof(snapshot->unique_id), "%s", server.unique_id);
    if (!server.paired)
        goto done;

    result = gs_applist(&server, &apps);
    if (result != GS_OK)
        goto done;
    for (app = apps; app && snapshot->app_count < MOONLIGHT_BACKEND_MAX_APPS; app = app->next)
    {
        moonlight_backend_app_t *output = &snapshot->apps[snapshot->app_count++];
        output->id = app->id;
        output->hdr_supported = app->hdr_supported ? 1u : 0u;
        output->app_collector_game = app->app_collector_game ? 1u : 0u;
        snprintf(output->name, sizeof(output->name), "%s", app->name ? app->name : "Unnamed app");
    }

done:
    snapshot->result = result;
    snprintf(snapshot->error, sizeof(snapshot->error), "%s",
             result == GS_OK ? "" : (gs_error ? gs_error : "Unknown error"));
    xml_applist_free(apps);
    http_set_timeout_ms(0);
    identity_free(&identity);
    return result;
}

static void *pairing_thread(void *unused)
{
    client_identity_t *identity = &pairing_job.identity;
    gs_server_t *server = &pairing_job.server;
    unsigned char random[2];
    int result;

    (void)unused;
    result = identity_init(identity, MOONLIGHT_IDENTITY_DIRECTORY);
    if (result != GS_OK)
        goto done;
    http_init(identity, 0);
    http_set_timeout_ms(3000);
    result = gs_init(server, identity, pairing_job.host, pairing_job.http_port);
    if (result == GS_OK && !server->paired)
        result = rng_fill(identity, random, sizeof(random));
    if (result == GS_OK && !server->paired)
    {
        snprintf(pairing_job.pin, sizeof(pairing_job.pin), "%04u",
                 ((unsigned)random[0] << 8 | random[1]) % 10000u);
        __atomic_store_n(&pairing_job.state, MOONLIGHT_BACKEND_PAIR_WAITING, __ATOMIC_RELEASE);
        http_set_timeout_ms(MOONLIGHT_BACKEND_PAIR_TIMEOUT_SECONDS * 1000);
        result = gs_pair(server, pairing_job.pin);
    }
    http_set_timeout_ms(0);

done:
    identity_free(identity);
    if (result == GS_OK)
        result = moonlight_backend_refresh(pairing_job.host, pairing_job.http_port,
                                           &pairing_job.snapshot);
    pairing_job.snapshot.result = result;
    if (result != GS_OK)
    {
        snprintf(pairing_job.snapshot.error, sizeof(pairing_job.snapshot.error), "%s",
                 gs_error ? gs_error : "Pairing failed");
    }
    __atomic_store_n(&pairing_job.state,
                     result == GS_OK && pairing_job.snapshot.paired
                         ? MOONLIGHT_BACKEND_PAIR_SUCCEEDED
                         : MOONLIGHT_BACKEND_PAIR_FAILED,
                     __ATOMIC_RELEASE);
    return NULL;
}

int moonlight_backend_pair_start(const char *host, uint16_t http_port)
{
    void *thread = NULL;
    int create_result;
    const int state = __atomic_load_n(&pairing_job.state, __ATOMIC_ACQUIRE);

    if (!host || !host[0])
        return GS_INVALID;
    if (state == MOONLIGHT_BACKEND_PAIR_PREPARING || state == MOONLIGHT_BACKEND_PAIR_WAITING)
        return GS_WRONG_STATE;

    memset(&pairing_job, 0, sizeof(pairing_job));
    snprintf(pairing_job.host, sizeof(pairing_job.host), "%s", host);
    pairing_job.http_port = http_port;
    snprintf(pairing_job.snapshot.host, sizeof(pairing_job.snapshot.host), "%s", host);
    pairing_job.snapshot.http_port = http_port;
    __atomic_store_n(&pairing_job.state, MOONLIGHT_BACKEND_PAIR_PREPARING, __ATOMIC_RELEASE);

    create_result = scePthreadCreate(&thread, NULL, pairing_thread, NULL, "moonlight-pair");
    if (create_result != 0)
    {
        pairing_job.snapshot.result = GS_FAILED;
        snprintf(pairing_job.snapshot.error, sizeof(pairing_job.snapshot.error),
                 "Pair worker thread create failed (0x%08x)", (unsigned)create_result);
        __atomic_store_n(&pairing_job.state, MOONLIGHT_BACKEND_PAIR_FAILED, __ATOMIC_RELEASE);
        return GS_FAILED;
    }
    scePthreadDetach(thread);
    return GS_OK;
}

moonlight_backend_pair_state_t moonlight_backend_pair_poll(moonlight_backend_snapshot_t *snapshot,
                                                           char pin[5])
{
    const moonlight_backend_pair_state_t state =
        (moonlight_backend_pair_state_t)__atomic_load_n(&pairing_job.state, __ATOMIC_ACQUIRE);

    if (pin)
    {
        if (state >= MOONLIGHT_BACKEND_PAIR_WAITING)
            memcpy(pin, pairing_job.pin, sizeof(pairing_job.pin));
        else
            pin[0] = '\0';
    }
    if (snapshot &&
        (state == MOONLIGHT_BACKEND_PAIR_SUCCEEDED || state == MOONLIGHT_BACKEND_PAIR_FAILED))
        memcpy(snapshot, &pairing_job.snapshot, sizeof(*snapshot));
    return state;
}

static int run_paired_action(const char *host, uint16_t http_port,
                             moonlight_backend_snapshot_t *snapshot, int (*action)(gs_server_t *),
                             const char *not_paired_error)
{
    client_identity_t identity;
    gs_server_t server;
    char action_error[sizeof(snapshot->error)];
    int result;

    if (!snapshot || !host || !host[0] || !action)
        return GS_INVALID;

    memset(snapshot, 0, sizeof(*snapshot));
    memset(&identity, 0, sizeof(identity));
    memset(&server, 0, sizeof(server));
    snprintf(snapshot->host, sizeof(snapshot->host), "%s", host);
    snapshot->http_port = http_port;
    result = identity_init(&identity, MOONLIGHT_IDENTITY_DIRECTORY);
    if (result != GS_OK)
    {
        identity_free(&identity);
        goto done;
    }
    http_init(&identity, 0);
    http_set_timeout_ms(3000);
    result = gs_init(&server, &identity, host, http_port);
    if (result == GS_OK && !server.paired)
    {
        gs_error = not_paired_error;
        result = GS_WRONG_STATE;
    }
    if (result == GS_OK)
        result = action(&server);
    snprintf(action_error, sizeof(action_error), "%s",
             result == GS_OK ? "" : (gs_error ? gs_error : "Action failed"));
    http_set_timeout_ms(0);
    identity_free(&identity);
    if (result == GS_OK)
        return moonlight_backend_refresh(host, http_port, snapshot);

    (void)moonlight_backend_refresh(host, http_port, snapshot);
    snapshot->result = result;
    snprintf(snapshot->error, sizeof(snapshot->error), "%s", action_error);
    return result;

done:
    snapshot->result = result;
    snprintf(snapshot->error, sizeof(snapshot->error), "%s", gs_error ? gs_error : "Action failed");
    return result;
}

int moonlight_backend_unpair(const char *host, uint16_t http_port,
                             moonlight_backend_snapshot_t *snapshot)
{
    int result = run_paired_action(host, http_port, snapshot, gs_unpair, "Client is not paired");
    if (result != GS_UNSUPPORTED_VERSION)
        return result;

    /* Modern Sunshine has no NVHTTP unpair route. Forgetting our local
       identity makes this host require a new PIN without web-admin access. */
    result = identity_forget(MOONLIGHT_IDENTITY_DIRECTORY);
    if (result != GS_OK)
    {
        snapshot->result = result;
        snprintf(snapshot->error, sizeof(snapshot->error), "%s",
                 gs_error ? gs_error : "Could not forget client identity");
        return result;
    }
    return moonlight_backend_refresh(host, http_port, snapshot);
}

int moonlight_backend_stop_app(const char *host, uint16_t http_port,
                               moonlight_backend_snapshot_t *snapshot)
{
    return run_paired_action(host, http_port, snapshot, gs_quit_app,
                             "Pair this client before stopping an app");
}

void moonlight_backend_clear_app_artwork(void)
{
    unsigned index;

    for (index = 0; index < MOONLIGHT_ARTWORK_SLOTS; ++index)
    {
        free(artwork_slots[index].data);
        memset(&artwork_slots[index], 0, sizeof(artwork_slots[index]));
    }
}

const unsigned char *moonlight_backend_find_app_artwork(int app_id, size_t *size)
{
    unsigned index;

    if (size)
        *size = 0;
    for (index = 0; index < MOONLIGHT_ARTWORK_SLOTS; ++index)
    {
        if (artwork_slots[index].app_id == app_id && artwork_slots[index].data)
        {
            if (size)
                *size = artwork_slots[index].size;
            return artwork_slots[index].data;
        }
    }
    return NULL;
}

int moonlight_backend_fetch_app_artwork(const char *host, uint16_t https_port, int app_id)
{
    static const unsigned char png_signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    client_identity_t identity;
    http_response_t response = {};
    char path[128];
    unsigned slot;
    int result;

    if (!host || !host[0] || !https_port || app_id <= 0)
        return GS_INVALID;
    if (moonlight_backend_find_app_artwork(app_id, NULL))
        return GS_OK;

    memset(&identity, 0, sizeof(identity));
    result = identity_init(&identity, MOONLIGHT_IDENTITY_DIRECTORY);
    if (result != GS_OK)
    {
        identity_free(&identity);
        return result;
    }
    http_init(&identity, 0);
    http_set_timeout_ms(1500);
    snprintf(path, sizeof(path), "/appasset?appid=%d&AssetType=2&AssetIdx=0", app_id);
    result = http_get(host, https_port, 1, path, &response);
    http_set_timeout_ms(0);
    identity_free(&identity);
    if (result != GS_OK)
        return result;
    if (response.length < sizeof(png_signature) ||
        memcmp(response.body, png_signature, sizeof(png_signature)) != 0)
    {
        http_response_free(&response);
        gs_error = "Sunshine app artwork is not a PNG";
        return GS_INVALID;
    }

    for (slot = 0; slot < MOONLIGHT_ARTWORK_SLOTS; ++slot)
    {
        if (!artwork_slots[slot].data)
            break;
    }
    if (slot == MOONLIGHT_ARTWORK_SLOTS)
    {
        http_response_free(&response);
        return GS_WRONG_STATE;
    }
    artwork_slots[slot].data = (unsigned char *)response.body;
    artwork_slots[slot].size = response.length;
    artwork_slots[slot].app_id = app_id;
    return GS_OK;
}
