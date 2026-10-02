// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <cstddef>
#include "Limelight.h"
namespace prosperolight::pyrowave
{
void prepare_callbacks(void (*on_error)(int), bool vsync, bool tv_safe);
void set_hdr_mode(bool enabled, const SS_HDR_METADATA *metadata);
DECODER_RENDERER_CALLBACKS *callbacks();
uint64_t presented();
void clear_error();
void copy_error(char *destination, size_t capacity);
void cleanup();
} // namespace prosperolight::pyrowave
