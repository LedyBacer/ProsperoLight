/*
 * ps5-native-app-boilerplate - Portable launcher artwork decoder.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once
#include <cstddef>
namespace launcher
{
struct ArtworkImage;
bool DecodePoster(const unsigned char *png, std::size_t size, ArtworkImage *image);
} // namespace launcher
