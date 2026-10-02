/*
 * ps5-native-app-boilerplate / ProsperoLight - AGC link stub.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Host-link declarations for system libSceAgc; never included in the app.
 */

#include <stdint.h>

int32_t sceAgcInit(uint32_t version) { (void)version; return -1; }
int32_t sceAgcCreateShader(void **shader, void *header, void *code) { (void)shader; (void)header; (void)code; return -1; }
int32_t sceAgcLinkShaders(void *cx, void *uc, void *reserved, void *vertex, void *pixel, uint32_t primitive) { (void)cx; (void)uc; (void)reserved; (void)vertex; (void)pixel; (void)primitive; return -1; }
void *sceAgcGetRegisterDefaults(void) { return 0; }
uint32_t *sceAgcDcbSetCxRegistersIndirect(void *command, const void *registers, uint32_t count) { (void)command; (void)registers; (void)count; return 0; }
uint32_t *sceAgcDcbSetShRegistersIndirect(void *command, const void *registers, uint32_t count) { (void)command; (void)registers; (void)count; return 0; }
uint32_t *sceAgcDcbSetUcRegistersIndirect(void *command, const void *registers, uint32_t count) { (void)command; (void)registers; (void)count; return 0; }
uint32_t *sceAgcCbSetShRegisterRangeDirect(void *command, uint32_t offset, const uint32_t *values, uint32_t count) { (void)command; (void)offset; (void)values; (void)count; return 0; }
uint32_t *sceAgcDcbDrawIndexAuto(void *command, uint32_t count, uint64_t modifier) { (void)command; (void)count; (void)modifier; return 0; }
uint32_t *sceAgcDcbSetFlip(void *command, uint32_t handle, int buffer, uint32_t mode, int64_t argument) { (void)command; (void)handle; (void)buffer; (void)mode; (void)argument; return 0; }
uint32_t *sceAgcCbReleaseMem(void *command, uint8_t event, int16_t cache_action, uint64_t reserved0, int8_t reserved1, void *address, uint32_t data_select, uint64_t data, uint16_t gds_offset, uint16_t gds_size, int8_t interrupt_select, int32_t reserved2) { (void)command; (void)event; (void)cache_action; (void)reserved0; (void)reserved1; (void)address; (void)data_select; (void)data; (void)gds_offset; (void)gds_size; (void)interrupt_select; (void)reserved2; return 0; }
int32_t sceAgcSuspendPoint(void) { return -1; }
