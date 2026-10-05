/*
 * ProsperoLight - Paths for the self-update kit.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The kit's defaults name /app0 and /download0. With filesystem access the
 * app's folder and its settings are elsewhere (include/app_storage.hpp), so
 * the app says where: 0 the helper in the app's folder, 1 the app's
 * param.json, 2 the file that keeps the catalog's highest sequence.
 */
#ifndef PROSPEROLIGHT_SELF_UPDATE_PATHS_H
#define PROSPEROLIGHT_SELF_UPDATE_PATHS_H

#ifdef __cplusplus
extern "C"
#endif
const char *prosperolight_self_update_path(int which);

#define SELF_UPDATE_HELPER_PATH prosperolight_self_update_path(0)
#define SELF_UPDATE_PARAM_PATH prosperolight_self_update_path(1)
#define SELF_UPDATE_SEQUENCE_PATH prosperolight_self_update_path(2)

#endif
