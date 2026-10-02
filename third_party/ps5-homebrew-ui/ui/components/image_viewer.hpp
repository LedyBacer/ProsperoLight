// ps5-homebrew-ui - Component: ImageViewer, one picture with zoom and pan, or a gallery of them.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui/components/card.hpp"
#include "ui/components/component.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace hui::ui
{

// One picture. Everything but the texture (or the `picture` slot) is optional.
struct ViewerImage
{
    std::string title;             // shown under the stage
    std::string caption;           // a quieter second part of that line
    std::uint32_t texture = 0;     // 0 draws the placeholder gradient (or the slot)
    gfx::Rect uv = gfx::kFullUv;   // gfx::kCanvasUv for textures you rendered into
    float aspect = 1.0f;           // width / height of the picture inside `uv`
    float pixel_width = 0.0f;      // its width in pixels, for 1:1; 0 leaves 1:1 out
    gfx::Color top{0, 0, 0, 0};    // placeholder gradient; alpha 0 uses the theme's well colour
    gfx::Color bottom{0, 0, 0, 0}; // ... its lower end; alpha 0 repeats `top`
    int tag = 0;                   // yours
};

enum class ZoomMode : std::uint8_t
{
    fit,    // the whole picture inside the stage
    fill,   // the stage covered, the picture cropped
    actual, // one picture pixel per screen pixel ("1:1")
    free,   // whatever the player zoomed to
};

struct ImageViewerStyle : ComponentStyle
{
    // ---- geometry ----
    float radius = -1.0f;        // the stage's corners; negative: from the theme
    float thumb_size = 60.0f;    // filmstrip thumbnails (square)
    float thumb_gap = 10.0f;     // between them
    float strip_gap = 14.0f;     // between the stage (or the caption) and the filmstrip
    float caption_size = 22.0f;  // the title line and the counter
    float caption_gap = 12.0f;   // between the stage and that line
    float slide_gap = 40.0f;     // between two pictures while one replaces the other
    float minimap_size = 150.0f; // the longer side of the minimap
    float overlay_inset = 14.0f; // minimap, readout and arrows from the stage's edge
    float readout_size = 20.0f;  // the zoom readout's text
    // ---- look ----
    bool frame = true;       // a sunken stage behind the picture (the letterbox)
    bool focus_ring = true;  // the theme's ring around the stage while it has the focus
    bool filmstrip = true;   // thumbnails under the stage (galleries only)
    bool caption = true;     // the title line under the stage
    bool counter = true;     // "03 / 12" at the end of that line (galleries only)
    bool minimap = true;     // where the view is in the picture, while it is cropped
    bool readout = true;     // "Fit 88%", "1:1 100%", "250%"
    bool arrows = true;      // chevrons at the stage's sides when there is a neighbour
    bool on_panel = false;   // the viewer sits on a themed panel, not on the page
    float thumb_dim = 0.45f; // 0..1: how far thumbnails other than the current one fade
    // ---- behaviour ----
    float max_zoom = 4.0f;     // times the fitted size (fill and 1:1 may exceed it)
    float zoom_step = 1.5f;    // one zoom_in() or zoom_out()
    float zoom_rate = 1.6f;    // analog triggers: e-folds per second at full pull
    bool trigger_zoom = true;  // read input.trigger_l / trigger_r in handle()
    float pan_speed = 1100.0f; // pixels per second at full deflection of the left stick
    float pan_step = 0.3f;     // of the stage, per D-pad press
    float rubber = 46.0f;      // pixels the picture can be pulled past its edge
    float pixel_scale = 1.0f;  // virtual pixels per picture pixel at 1:1
    bool wrap = false;         // past the last picture comes the first
    // left / right: at the first or last picture; up / down: whenever the
    // picture has nothing to pan that way. Hand the focus on instead of
    // refusing (left / right) or ignoring (up / down).
    EdgeExits exits;
};

// A picture fitted to a stage, with zoom and pan, and a gallery when it is
// given more than one. Only the visible part of the picture is drawn (its uv
// rectangle is cropped), so nothing depends on a clip.
//
//   ui::ImageViewer viewer;
//   viewer.style.theme = theme;
//   viewer.set_images(images);              // std::vector<ui::ViewerImage>
//   viewer.set_bounds({776, 290, 1048, 350});
//   ...
//   viewer.handle(input, feedback);  // every frame while it has the focus
//   viewer.update(dt);
//   viewer.draw(canvas);
//
// Input: confirm cycles fit / fill / 1:1, the triggers zoom, the left stick
// pans, the D-pad pans where the picture overflows and browses otherwise,
// back returns to fit before it cancels.
class ImageViewer
{
  public:
    // where is the whole picture on screen (it may be larger than the stage:
    // the viewer clips to it); alpha is its opacity.
    using Picture = std::function<void(Canvas &canvas, const gfx::Rect &where,
                                       const ViewerImage &image, int index, float alpha)>;

    ImageViewerStyle style;
    Picture picture; // draws a picture instead of its texture

    void set_images(std::vector<ViewerImage> images);
    const std::vector<ViewerImage> &images() const
    {
        return images_;
    }
    void set_bounds(const gfx::Rect &bounds);
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    void set_active(bool active)
    {
        active_ = active;
    }

    int index() const
    {
        return index_;
    }
    // Without sound; snap skips the slide. A picture arrives fitted, or in
    // the mode the last one was in when that was fill or 1:1.
    void set_index(int index, bool snap = true);

    Event handle(const InputFrame &input, Feedback &feedback);
    void update(float dt);
    void draw(Canvas &canvas) const;

    // ---- zoom, for a screen to bind to buttons of its choice ----
    Event toggle_mode(Feedback &feedback); // fit -> fill -> 1:1 -> fit: changed
    Event zoom_in(Feedback &feedback);     // changed, or refused at the limit
    Event zoom_out(Feedback &feedback);
    Event cycle_zoom(Feedback &feedback); // zoom_in(), and back to fit from the limit
    void set_mode(ZoomMode mode, bool snap = false);
    void set_zoom(float scale, bool snap = false); // 1 is fit
    ZoomMode mode() const
    {
        return mode_;
    }
    float zoom() const
    {
        return zoom_.target;
    }
    // The readout's number: of the picture's own size when pixel_width is
    // known, of the fitted size otherwise.
    int percent() const;
    // True when the picture is larger than fit.
    bool zoomed() const
    {
        return zoom_.target > 1.02f;
    }
    // The centre of the view in the picture, 0..1 on both axes.
    float pan_x() const
    {
        return pan_x_.target;
    }
    float pan_y() const
    {
        return pan_y_.target;
    }

    // The edge the focus asked to leave through in the last handle() (see
    // ImageViewerStyle::exits), or Direction::none. handle() returned none.
    Direction exit() const
    {
        return exit_;
    }
    // Where the stage is, and where the current picture is on screen right
    // now (unclipped: larger than the stage when zoomed).
    gfx::Rect stage() const;
    gfx::Rect picture_rect() const;

  private:
    struct Range
    {
        float lo = 0.5f;
        float hi = 0.5f;
    };
    gfx::Rect inner_stage() const;
    gfx::Rect strip() const;
    bool strip_shown() const;
    void fitted(int index, float *w, float *h) const;
    float fill_scale(int index) const;
    float actual_scale(int index) const; // 0 when unknown
    float scale_for(int index, ZoomMode mode) const;
    float min_scale() const;
    float max_scale() const;
    float mode_scale(ZoomMode mode) const;
    Range range(bool horizontal, float scale) const;
    bool overflows(bool horizontal) const;
    void apply_zoom(float scale, bool snap);
    void pan_axis(tween::Spring &axis, const Range &limits, float stick, float span, float dt);
    Event browse(int direction, const InputFrame &input, Feedback &feedback);
    Event pan_step(Direction direction, const InputFrame &input, Feedback &feedback);
    float wrapped(float distance) const;
    void draw_picture(Canvas &canvas, const ViewerImage &image, int index, const gfx::Rect &where,
                      const gfx::Rect &window, float radius, float alpha) const;
    void draw_overlays(Canvas &canvas, Painter &paint) const;
    void draw_caption(Canvas &canvas, Painter &paint) const;
    void draw_strip(Canvas &canvas, Painter &paint) const;

    std::vector<ViewerImage> images_;
    gfx::Rect bounds_{0.0f, 0.0f, 960.0f, 540.0f};
    int index_ = 0;
    // The current picture as a place in the row. It is index_ except when the
    // gallery wraps, where it keeps counting so the slide goes one step.
    int slot_ = 0;
    bool active_ = true;
    ZoomMode mode_ = ZoomMode::fit;
    Direction exit_ = Direction::none;
    tween::Bounce zoom_{1.0f, 0.0f, 1.0f};
    tween::Spring pan_x_{0.5f, 0.0f, 0.5f};
    tween::Spring pan_y_{0.5f, 0.0f, 0.5f};
    tween::Bounce position_;
    tween::Spring mark_;    // the filmstrip's marker, in thumbnails
    tween::Spring cropped_; // 0..1: the minimap's presence
    tween::Spring active_amount_{1.0f, 0.0f, 1.0f};
    Pulse refusal_;
    Pulse swap_; // the caption of a picture that just arrived fades in
    // Analog input of this frame, read by handle() and spent by update().
    float stick_x_ = 0.0f;
    float stick_y_ = 0.0f;
    float trigger_ = 0.0f;
};

} // namespace hui::ui
