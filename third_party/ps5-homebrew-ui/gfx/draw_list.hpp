// ps5-homebrew-ui - 2D draw list: instanced shapes, glyphs and images.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gfx/font.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace hui::gfx
{

// The virtual canvas every screen is laid out in; the viewport scales it to
// the surface (1080p, 1440p or 4K) so layouts never deal with real pixels.
constexpr float kVirtualWidth = 1920.0f;
constexpr float kVirtualHeight = 1080.0f;

struct Color
{
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;

    static Color rgb(std::uint32_t hex, float alpha = 1.0f)
    {
        return {static_cast<float>((hex >> 16) & 0xff) / 255.0f,
                static_cast<float>((hex >> 8) & 0xff) / 255.0f,
                static_cast<float>(hex & 0xff) / 255.0f, alpha};
    }
    Color with_alpha(float alpha) const
    {
        return {r, g, b, a * alpha};
    }
};

// Linear blend of two colours (alpha included).
inline Color mix(Color a, Color b, float t)
{
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t,
            a.a + (b.a - a.a) * t};
}

struct Rect
{
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    float cx() const
    {
        return x + w * 0.5f;
    }
    float cy() const
    {
        return y + h * 0.5f;
    }
    // Shrinks the rectangle on every side (grows it when amount is negative).
    Rect inset(float amount) const
    {
        return {x + amount, y + amount, w - 2.0f * amount, h - 2.0f * amount};
    }
};

// Font atlases live on texture units of their own, so text never breaks a
// run of shapes: GlBatch::create_font_texture returns a handle made of this
// base plus a slot number (1..kFontSlots) instead of a GL texture name.
constexpr std::uint32_t kFontHandleBase = 0xf0000000u;
constexpr std::uint32_t kFontSlots = 6;
constexpr bool is_font_handle(std::uint32_t texture)
{
    return (texture & 0xfffffff0u) == kFontHandleBase;
}

// Shape modes understood by the batch shader (keep in sync with gl_batch.cpp).
enum class Shape : std::uint8_t
{
    rounded_rect = 0, // fill (gradient) plus optional border; can rotate
    glyph = 1,        // SDF text from the run's texture
    shadow = 2,       // soft rounded-rect falloff (also used for glows)
    image = 3,        // texture over the rect, tinted, optional rounded corners
    capsule = 4,      // line segment with round caps
    triangle = 5,     // isosceles triangle pointing up inside the rect
    star = 6,         // five-pointed star inscribed in the rect
    arc = 7,          // ring sector: gauges, spinners, radial menus
    chamfer = 8,      // rectangle with cut (45 degree) corners, optional border
};

// One instanced quad; field order matches the vertex attributes.
struct Instance
{
    float rect[4];         // x, y, w, h in virtual pixels
    float color_top[4];    // rgba (straight alpha)
    float color_bottom[4]; // rgba
    float border_color[4]; // rgba
    float params[4];       // radius|range|start angle, border|thickness, softness|font slot, shape
    float extra[4];        // uv rect, segment endpoints, rotation or sweep (per shape)
};
static_assert(sizeof(Instance) == 96);

// One vertex of a filled polygon mesh.
struct MeshVertex
{
    float x, y;
    float r, g, b, a;
};

// Consecutive instances (or mesh vertices) sharing a texture and clip rectangle.
struct Run
{
    std::uint32_t first = 0;
    std::uint32_t count = 0;
    std::uint32_t texture = 0; // opaque GL texture name, 0 for none
    bool clipped = false;
    bool mesh = false; // first/count index mesh_vertices() instead of instances()
    Rect clip;         // virtual pixels
};

// Records a frame of 2D drawing in the virtual coordinate space. Transforms
// (for transitions) apply on the CPU; clip rectangles and texture changes
// start new runs. The GL backend uploads once per draw.
//
// Angles are in radians, measured clockwise from 12 o'clock.
class DrawList
{
  public:
    void clear();

    // ---- shapes ----
    void rounded_rect(const Rect &r, float radius, Color fill);
    // Vertical gradient (top to bottom).
    void gradient_rect(const Rect &r, float radius, Color top, Color bottom);
    // Horizontal gradient (left to right).
    void gradient_rect_h(const Rect &r, float radius, Color left, Color right);
    void bordered_rect(const Rect &r, float radius, Color fill, float border, Color border_color);
    // Rectangle whose corners are cut at 45 degrees by `cut` pixels (sci-fi
    // panels, tags). border > 0 adds an inner border like bordered_rect.
    void chamfer_rect(const Rect &r, float cut, Color fill, float border = 0.0f,
                      Color border_color = {});
    // Rounded rectangle turned about its centre.
    void rotated_rect(const Rect &r, float radius, float angle, Color fill);
    void shadow(const Rect &r, float radius, float softness, Color color);
    // Coloured light spilling `spread` pixels out of a rounded rectangle.
    void glow(const Rect &r, float radius, float spread, Color color);
    void circle(float cx, float cy, float radius, Color fill);
    void ring(float cx, float cy, float radius, float thickness, Color color);
    // Ring sector centred on (cx, cy): radius is the outer edge. With
    // round_caps the ends are half discs, otherwise they are cut straight.
    void arc(float cx, float cy, float radius, float thickness, float start, float sweep,
             Color color, bool round_caps = true);
    void line(float x1, float y1, float x2, float y2, float thickness, Color color);
    // Triangle filling r, pointing up; angle turns it about its centre
    // (1.5708 points right). outline > 0 draws only a stroke of that width.
    void triangle(const Rect &r, Color fill, float outline = 0.0f, float angle = 0.0f);
    // Five-pointed star centred at (cx, cy); outline > 0 draws only a stroke.
    void star(float cx, float cy, float radius, Color fill, float outline = 0.0f);
    // uv is the texture rectangle (0..1); see gfx::kFullUv and gfx::kCanvasUv.
    void image(std::uint32_t texture, const Rect &r, const Rect &uv, Color tint,
               float radius = 0.0f);
    // The same with a tint that blends from top to bottom: reflections and
    // images that fade out need no cover-up rectangle.
    void image_gradient(std::uint32_t texture, const Rect &r, const Rect &uv, Color top,
                        Color bottom, float radius = 0.0f);
    // The blurred copy of what is behind r (see gfx::Renderer), for frosted
    // panels. texture is the frame's glass texture.
    void glass(std::uint32_t texture, const Rect &r, float radius, Color tint);
    // Filled simple polygon (convex or concave) from interleaved x, y pairs.
    // Edges are not anti-aliased by the shader: keep polygons small or thin.
    void polygon(const float *xy, int count, Color fill);

    // ---- text ----
    // font_texture is the GL texture holding font's atlas. tracking adds
    // pixels between glyphs (small caps labels use 2-4). Returns the width.
    float text(const Font &font, std::uint32_t font_texture, std::string_view text, float x,
               float baseline, float size, Color color, Align align = Align::left,
               float tracking = 0.0f);

    // ---- state ----
    void push_clip(const Rect &r); // intersected with the current clip
    void pop_clip();
    // Scales about (origin_x, origin_y), then translates; composes with the
    // current transform. Used for card zooms, slides and fades.
    void push_transform(float scale, float origin_x, float origin_y, float dx, float dy);
    void pop_transform();
    void push_opacity(float opacity); // multiplies every colour's alpha
    void pop_opacity();

    const std::vector<Instance> &instances() const
    {
        return instances_;
    }
    const std::vector<Run> &runs() const
    {
        return runs_;
    }
    const std::vector<MeshVertex> &mesh_vertices() const
    {
        return mesh_;
    }
    bool empty() const
    {
        return instances_.empty() && mesh_.empty();
    }

  private:
    struct Transform
    {
        float scale = 1.0f;
        float dx = 0.0f;
        float dy = 0.0f;
    };

    Instance &append(std::uint32_t texture);
    Rect apply(const Rect &r) const;
    float apply_x(float x) const
    {
        return x * transform_.scale + transform_.dx;
    }
    float apply_y(float y) const
    {
        return y * transform_.scale + transform_.dy;
    }
    void set_color(float *out, Color c) const;

    std::vector<Instance> instances_;
    std::vector<Run> runs_;
    std::vector<Rect> clips_;
    std::vector<Transform> transforms_;
    std::vector<float> opacities_;
    Transform transform_;
    float opacity_ = 1.0f;
    std::vector<GlyphQuad> glyph_scratch_;
    std::vector<MeshVertex> mesh_;
    std::vector<std::uint32_t> index_scratch_;
};

// The whole texture, for images uploaded top row first.
constexpr Rect kFullUv{0.0f, 0.0f, 1.0f, 1.0f};
// The whole texture, for canvases (rendered bottom row first).
constexpr Rect kCanvasUv{0.0f, 1.0f, 1.0f, -1.0f};

// Letterboxes a virtual canvas into a surface: surface = virtual * scale + offset.
struct Viewport
{
    float scale = 1.0f;
    float offset_x = 0.0f;
    float offset_y = 0.0f;
};
Viewport fit_viewport(int surface_width, int surface_height, float virtual_width = kVirtualWidth,
                      float virtual_height = kVirtualHeight);

} // namespace hui::gfx
