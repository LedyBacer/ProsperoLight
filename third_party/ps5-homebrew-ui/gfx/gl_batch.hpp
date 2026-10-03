// ps5-homebrew-ui - OpenGL backend for the 2D draw list.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gfx/draw_list.hpp"
#include "gfx/font.hpp"

#include <GL/glcorearb.h>

#include <cstddef>
#include <cstdint>

namespace hui::gfx
{

// Draws a DrawList with one instanced program: all instances are uploaded
// once per frame into an orphaned stream buffer, then each run is one
// glDrawArraysInstancedBaseInstance call (triangle lists only).
class GlBatch
{
  public:
    GlBatch() = default;
    GlBatch(const GlBatch &) = delete;
    GlBatch &operator=(const GlBatch &) = delete;
    ~GlBatch();

    bool init();
    // Deletes the programs, vertex arrays and buffers; init() recreates them.
    void release();
    // Uploads a font atlas as a single-level R8 texture and gives it one of
    // the font slots; returns the handle DrawList::text takes. With every
    // slot taken it returns a plain texture name, which also works but starts
    // a new draw call wherever that font is used.
    std::uint32_t create_font_texture(const Font &font);
    // Uploads RGBA8 pixels as a single-level texture; returns its name.
    std::uint32_t create_texture(int width, int height, const std::uint8_t *rgba);

    // Draws into the currently bound framebuffer of the given size.
    void draw(const DrawList &list, const Viewport &viewport, int surface_width,
              int surface_height);

    std::size_t last_draw_calls() const
    {
        return draw_calls_;
    }

  private:
    GLuint program_ = 0;
    GLuint vao_ = 0;
    GLuint buffer_ = 0;
    std::size_t capacity_ = 0; // instances
    GLuint mesh_program_ = 0;
    GLuint mesh_vao_ = 0;
    GLuint mesh_buffer_ = 0;
    std::size_t mesh_capacity_ = 0; // vertices
    std::size_t draw_calls_ = 0;
    GLuint font_textures_[kFontSlots] = {};
    std::uint32_t font_count_ = 0;
};

} // namespace hui::gfx
