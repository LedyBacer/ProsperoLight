// ps5-homebrew-ui - Persistent offscreen canvas (MSAA render target + texture).
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <GL/glcorearb.h>

#include <cstdint>

namespace hui::gfx
{

// A render target that keeps its contents between frames, for front ends that
// redraw incrementally (the Tatham puzzles). Drawing goes into a multisampled
// framebuffer when available; resolve() copies it into a sampleable texture.
class Canvas
{
  public:
    Canvas() = default;
    Canvas(const Canvas &) = delete;
    Canvas &operator=(const Canvas &) = delete;
    ~Canvas();

    // (Re)creates the target; samples is clamped to what the driver allows
    // and falls back to 1 if the multisampled target is incomplete.
    bool create(int width, int height, int samples = 4);
    void destroy();

    // Binds the draw target (viewport included). Restore with unbind().
    void bind() const;
    void unbind(GLuint previous_framebuffer = 0) const;
    // Resolves (or copies) the draw target into texture().
    void resolve() const;

    // Blitter support: copy a region of the resolved image into a new texture,
    // and draw callers can composite it back. Returns the texture name.
    GLuint save_region(int x, int y, int w, int h) const;

    GLuint texture() const
    {
        return texture_;
    }
    // The framebuffer to draw into (Renderer::present takes it).
    GLuint framebuffer() const
    {
        return draw_framebuffer_;
    }
    int width() const
    {
        return width_;
    }
    int height() const
    {
        return height_;
    }
    int samples() const
    {
        return samples_;
    }

  private:
    GLuint draw_framebuffer_ = 0; // multisampled (or the texture FBO itself)
    GLuint color_buffer_ = 0;     // multisampled renderbuffer
    GLuint resolve_framebuffer_ = 0;
    GLuint texture_ = 0;
    int width_ = 0;
    int height_ = 0;
    int samples_ = 1;
};

} // namespace hui::gfx
