// ps5-homebrew-ui - Persistent offscreen canvas (MSAA render target + texture).
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gfx/canvas.hpp"

#include "platform/ps5/system.hpp"

#include <algorithm>

namespace hui::gfx
{

Canvas::~Canvas()
{
    destroy();
}

void Canvas::destroy()
{
    if (color_buffer_ != 0)
        glDeleteRenderbuffers(1, &color_buffer_);
    if (draw_framebuffer_ != 0 && draw_framebuffer_ != resolve_framebuffer_)
        glDeleteFramebuffers(1, &draw_framebuffer_);
    if (resolve_framebuffer_ != 0)
        glDeleteFramebuffers(1, &resolve_framebuffer_);
    if (texture_ != 0)
        glDeleteTextures(1, &texture_);
    color_buffer_ = draw_framebuffer_ = resolve_framebuffer_ = texture_ = 0;
    width_ = height_ = 0;
}

bool Canvas::create(int width, int height, int samples)
{
    destroy();
    width_ = std::max(1, width);
    height_ = std::max(1, height);

    glGenTextures(1, &texture_);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &resolve_framebuffer_);
    glBindFramebuffer(GL_FRAMEBUFFER, resolve_framebuffer_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture_, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        sys::log("[HUI] canvas %dx%d texture target incomplete", width_, height_);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }

    GLint max_samples = 1;
    glGetIntegerv(GL_MAX_SAMPLES, &max_samples);
    samples_ = std::clamp(samples, 1, std::max(1, static_cast<int>(max_samples)));
    draw_framebuffer_ = resolve_framebuffer_;
    if (samples_ > 1)
    {
        glGenRenderbuffers(1, &color_buffer_);
        glBindRenderbuffer(GL_RENDERBUFFER, color_buffer_);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples_, GL_RGBA8, width_, height_);
        glGenFramebuffers(1, &draw_framebuffer_);
        glBindFramebuffer(GL_FRAMEBUFFER, draw_framebuffer_);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER,
                                  color_buffer_);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            sys::log("[HUI] canvas %dx MSAA unavailable, drawing without it", samples_);
            glDeleteFramebuffers(1, &draw_framebuffer_);
            glDeleteRenderbuffers(1, &color_buffer_);
            color_buffer_ = 0;
            draw_framebuffer_ = resolve_framebuffer_;
            samples_ = 1;
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, draw_framebuffer_);
    glViewport(0, 0, width_, height_);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    resolve();
    return true;
}

void Canvas::bind() const
{
    glBindFramebuffer(GL_FRAMEBUFFER, draw_framebuffer_);
    glViewport(0, 0, width_, height_);
}

void Canvas::unbind(GLuint previous_framebuffer) const
{
    glBindFramebuffer(GL_FRAMEBUFFER, previous_framebuffer);
}

void Canvas::resolve() const
{
    if (draw_framebuffer_ == resolve_framebuffer_)
        return;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, draw_framebuffer_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolve_framebuffer_);
    glBlitFramebuffer(0, 0, width_, height_, 0, 0, width_, height_, GL_COLOR_BUFFER_BIT,
                      GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

GLuint Canvas::save_region(int x, int y, int w, int h) const
{
    resolve();
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    // Canvas rows are stored bottom-up: convert the top-left region origin.
    const int src_y = height_ - (y + h);
    const int cx = std::clamp(x, 0, width_);
    const int cy = std::clamp(src_y, 0, height_);
    const int cw = std::clamp(w - (cx - x), 0, width_ - cx);
    const int ch = std::clamp(h - (cy - src_y), 0, height_ - cy);
    if (cw > 0 && ch > 0)
        glCopyImageSubData(texture_, GL_TEXTURE_2D, 0, cx, cy, 0, texture, GL_TEXTURE_2D, 0, cx - x,
                           cy - src_y, 0, cw, ch, 1);
    return texture;
}

} // namespace hui::gfx
