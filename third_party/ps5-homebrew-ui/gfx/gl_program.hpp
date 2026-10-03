// ps5-homebrew-ui - GLSL program compilation.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <GL/glcorearb.h>

namespace hui::gfx
{

// Shader sources carry no #version line; this prefix is prepended at compile
// time. It defaults to "#version 460 core" (PS5); the host preview selects
// the version its driver supports.
void set_glsl_prefix(const char *prefix);

// Compiles and links a vertex/fragment program. Returns 0 and logs the info
// log on failure.
GLuint build_program(const char *label, const char *vertex_source, const char *fragment_source);

} // namespace hui::gfx
