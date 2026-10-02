/*
 * ps5-native-app-boilerplate / ProsperoLight - VideoOut link stub.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Host-link declarations for public libSceVideoOut imports. These bodies are
 * never packaged or executed; the native module writer replaces them with
 * imports from the system module.
 */

#include <stddef.h>
#include <stdint.h>

int32_t sceVideoOutOpen(int32_t user_id, int32_t bus_type, int32_t index, const void *parameter)
{
    (void)user_id;
    (void)bus_type;
    (void)index;
    (void)parameter;
    return -1;
}

int32_t sceVideoOutClose(int32_t handle)
{
    (void)handle;
    return -1;
}

int32_t sceVideoOutSetFlipRate(int32_t handle, int32_t rate)
{
    (void)handle;
    (void)rate;
    return -1;
}

int32_t sceVideoOutConfigureOutput(int32_t handle, uint32_t request_type, const void *param3,
                                   const void *param4, const void *param5)
{
    (void)handle;
    (void)request_type;
    (void)param3;
    (void)param4;
    (void)param5;
    return -1;
}

int32_t sceVideoOutIsOutputSupported(int32_t handle, uint32_t request_type, const void *param3,
                                     const void *param4, const void *param5)
{
    (void)handle;
    (void)request_type;
    (void)param3;
    (void)param4;
    (void)param5;
    return -1;
}

int32_t sceVideoOutConfigureOutputMode_(int32_t handle, uint32_t option, const void *mode,
                                        const void *color, uint32_t mode_size, uint32_t color_size)
{
    (void)handle;
    (void)option;
    (void)mode;
    (void)color;
    (void)mode_size;
    (void)color_size;
    return -1;
}

int32_t sceVideoOutGetResolutionStatus(int32_t handle, void *status)
{
    (void)handle;
    (void)status;
    return -1;
}

int32_t sceVideoOutGetOutputStatus(int32_t handle, void *status)
{
    (void)handle;
    (void)status;
    return -1;
}

void sceVideoOutSetBufferAttribute2(void *attribute, uint64_t pixel_format, uint32_t tiling_mode,
                                    uint32_t width, uint32_t height, uint64_t option,
                                    uint32_t dcc_control, uint64_t dcc_clear_color)
{
    (void)attribute;
    (void)pixel_format;
    (void)tiling_mode;
    (void)width;
    (void)height;
    (void)option;
    (void)dcc_control;
    (void)dcc_clear_color;
}

int32_t sceVideoOutRegisterBuffers2(int32_t handle, int32_t set_index, int32_t buffer_index_start,
                                    const void *buffers, int32_t buffer_num, const void *attribute,
                                    int32_t category, void *option)
{
    (void)handle;
    (void)set_index;
    (void)buffer_index_start;
    (void)buffers;
    (void)buffer_num;
    (void)attribute;
    (void)category;
    (void)option;
    return -1;
}

int32_t sceVideoOutUnregisterBuffers(int32_t handle, int32_t set_index)
{
    (void)handle;
    (void)set_index;
    return -1;
}

int32_t sceVideoOutSubmitFlip(int32_t handle, int32_t buffer_index, uint32_t flip_mode,
                              int64_t flip_arg)
{
    (void)handle;
    (void)buffer_index;
    (void)flip_mode;
    (void)flip_arg;
    return -1;
}

int32_t sceVideoOutIsFlipPending(int32_t handle)
{
    (void)handle;
    return -1;
}

int32_t sceVideoOutWaitVblank(int32_t handle)
{
    (void)handle;
    return -1;
}

int32_t sceVideoOutGetFlipStatus(int32_t handle, void *status)
{
    (void)handle;
    (void)status;
    return -1;
}

int32_t sceVideoOutAddFlipEvent(void *queue, int32_t handle, void *data)
{
    (void)queue;
    (void)handle;
    (void)data;
    return -1;
}

int32_t sceVideoOutDeleteFlipEvent(void *queue, int32_t handle)
{
    (void)queue;
    (void)handle;
    return -1;
}

int32_t sceVideoOutVrrUnpegFromFixedRate(int32_t handle)
{
    (void)handle;
    return -1;
}

/* Physical refresh validation for the optional RADV presenter. */
int sceVideoOutGetVblankStatus(int32_t handle, void *status) { (void)handle; (void)status; return -1; }
