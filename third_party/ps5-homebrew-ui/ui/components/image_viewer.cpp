// ps5-homebrew-ui - Component: ImageViewer.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/image_viewer.hpp"

#include "ui/components/progress.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace hui::ui
{

using gfx::Color;
using gfx::Rect;

namespace
{

constexpr float kMarkRoom = 10.0f; // under the thumbnails, for the marker

bool round_theme(const Theme &theme)
{
    return theme.corner == Corner::round && theme.radius >= 2.0f;
}

// The one colour of a theme that is always opaque enough for a small mark.
Color mark_color(const Theme &theme)
{
    return theme.focus.a > 0.6f ? theme.focus : theme.primary;
}

} // namespace

void ImageViewer::set_images(std::vector<ViewerImage> images)
{
    images_ = std::move(images);
    const int count = static_cast<int>(images_.size());
    set_index(std::clamp(index_, 0, std::max(count - 1, 0)), true);
}

void ImageViewer::set_bounds(const Rect &bounds)
{
    bounds_ = bounds;
    if (!images_.empty())
        apply_zoom(mode_ == ZoomMode::free ? zoom_.target : mode_scale(mode_), true);
}

void ImageViewer::set_index(int index, bool snap)
{
    if (images_.empty())
    {
        index_ = slot_ = 0;
        return;
    }
    index_ = std::clamp(index, 0, static_cast<int>(images_.size()) - 1);
    slot_ = index_;
    position_.target = static_cast<float>(slot_);
    mark_.target = static_cast<float>(index_);
    if (snap)
    {
        position_.snap(position_.target);
        mark_.snap(mark_.target);
    }
    // Fill and 1:1 are ways of looking that carry over to the next picture;
    // a free zoom belonged to the last one.
    if (mode_ == ZoomMode::free)
        mode_ = ZoomMode::fit;
    zoom_.snap(std::clamp(mode_scale(mode_), min_scale(), max_scale()));
    pan_x_.snap(0.5f);
    pan_y_.snap(0.5f);
}

bool ImageViewer::strip_shown() const
{
    return style.filmstrip && images_.size() > 1;
}

Rect ImageViewer::stage() const
{
    float below = 0.0f;
    if (style.caption)
        below += style.caption_gap + style.caption_size * 1.5f;
    if (strip_shown())
        below += style.strip_gap + style.thumb_size + kMarkRoom;
    return {bounds_.x, bounds_.y, bounds_.w, std::max(bounds_.h - below, 40.0f)};
}

Rect ImageViewer::inner_stage() const
{
    // The well's edge is drawn inside its rectangle: the picture stays off it.
    return stage().inset(style.frame ? style.theme.border : 0.0f);
}

Rect ImageViewer::strip() const
{
    const float height = style.thumb_size + kMarkRoom;
    return {bounds_.x, bounds_.y + bounds_.h - height, bounds_.w, height};
}

void ImageViewer::fitted(int index, float *w, float *h) const
{
    const Rect in = inner_stage();
    const float aspect = std::max(images_[static_cast<std::size_t>(index)].aspect, 0.01f);
    *w = std::max(std::min(in.w, in.h * aspect), 1.0f);
    *h = *w / aspect;
}

float ImageViewer::fill_scale(int index) const
{
    const Rect in = inner_stage();
    float w = 1.0f;
    float h = 1.0f;
    fitted(index, &w, &h);
    return std::max(in.w / w, in.h / h);
}

float ImageViewer::actual_scale(int index) const
{
    const ViewerImage &image = images_[static_cast<std::size_t>(index)];
    if (image.pixel_width <= 0.0f)
        return 0.0f;
    float w = 1.0f;
    float h = 1.0f;
    fitted(index, &w, &h);
    return image.pixel_width * style.pixel_scale / w;
}

float ImageViewer::min_scale() const
{
    const float actual = actual_scale(index_);
    return actual > 0.0f && actual < 1.0f ? actual : 1.0f;
}

float ImageViewer::max_scale() const
{
    return std::max({style.max_zoom, fill_scale(index_), actual_scale(index_), 1.0f});
}

float ImageViewer::scale_for(int index, ZoomMode mode) const
{
    if (mode == ZoomMode::fill)
        return fill_scale(index);
    if (mode == ZoomMode::actual)
    {
        const float actual = actual_scale(index);
        return actual > 0.0f ? actual : 1.0f;
    }
    return 1.0f;
}

float ImageViewer::mode_scale(ZoomMode mode) const
{
    return images_.empty() ? 1.0f : scale_for(index_, mode);
}

// How far the centre of the view may travel in the picture, 0..1, at a
// scale. A picture that fits on an axis stays centred on it.
ImageViewer::Range ImageViewer::range(bool horizontal, float scale) const
{
    const Rect in = inner_stage();
    float w = 1.0f;
    float h = 1.0f;
    fitted(index_, &w, &h);
    const float size = (horizontal ? w : h) * scale;
    const float room = horizontal ? in.w : in.h;
    if (size <= room + 0.5f)
        return {};
    const float half = room / (2.0f * size);
    return {half, 1.0f - half};
}

bool ImageViewer::overflows(bool horizontal) const
{
    if (images_.empty())
        return false;
    return range(horizontal, zoom_.target).lo < 0.5f - 1e-4f;
}

void ImageViewer::apply_zoom(float scale, bool snap)
{
    zoom_.target = std::clamp(scale, min_scale(), max_scale());
    const Range x = range(true, zoom_.target);
    const Range y = range(false, zoom_.target);
    pan_x_.target = std::clamp(pan_x_.target, x.lo, x.hi);
    pan_y_.target = std::clamp(pan_y_.target, y.lo, y.hi);
    if (snap)
    {
        zoom_.snap(zoom_.target);
        pan_x_.snap(pan_x_.target);
        pan_y_.snap(pan_y_.target);
    }
}

void ImageViewer::set_mode(ZoomMode mode, bool snap)
{
    if (images_.empty())
        return;
    mode_ = mode;
    if (mode != ZoomMode::free)
        apply_zoom(mode_scale(mode), snap);
}

void ImageViewer::set_zoom(float scale, bool snap)
{
    if (images_.empty())
        return;
    apply_zoom(scale, snap);
    mode_ = std::fabs(zoom_.target - 1.0f) < 0.01f ? ZoomMode::fit : ZoomMode::free;
}

int ImageViewer::percent() const
{
    if (images_.empty())
        return 100;
    const float actual = actual_scale(index_);
    const float basis = actual > 0.0f ? actual : 1.0f;
    return static_cast<int>(std::lround(zoom_.target / basis * 100.0f));
}

Rect ImageViewer::picture_rect() const
{
    if (images_.empty())
        return stage();
    const Rect in = inner_stage();
    float w = 1.0f;
    float h = 1.0f;
    fitted(index_, &w, &h);
    w *= zoom_.value;
    h *= zoom_.value;
    return {in.cx() - pan_x_.value * w, in.cy() - pan_y_.value * h, w, h};
}

// The shortest way round to a picture `distance` places away when the
// gallery is a ring.
float ImageViewer::wrapped(float distance) const
{
    const float count = static_cast<float>(images_.size());
    if (!style.wrap || count < 3.0f)
        return distance;
    return distance - count * std::round(distance / count);
}

Event ImageViewer::toggle_mode(Feedback &feedback)
{
    if (images_.empty())
        return Event::none;
    const float x = stage().cx();
    // The next mode whose size differs from what is on screen: a picture
    // that already fills the stage has no "fill" step.
    constexpr ZoomMode kOrder[] = {ZoomMode::fit, ZoomMode::fill, ZoomMode::actual};
    int at = mode_ == ZoomMode::fill ? 1 : (mode_ == ZoomMode::actual ? 2 : 0);
    if (mode_ == ZoomMode::free)
        at = 2; // a free zoom goes home first
    for (int i = 0; i < 3; ++i)
    {
        at = (at + 1) % 3;
        const ZoomMode next = kOrder[at];
        if (next == ZoomMode::actual && actual_scale(index_) <= 0.0f)
            continue;
        const float scale = std::clamp(mode_scale(next), min_scale(), max_scale());
        if (std::fabs(scale - zoom_.target) <= 0.03f * zoom_.target)
            continue;
        set_mode(next);
        play_cue(feedback, style, style.sounds.change, x, scale > 1.0f ? 1.08f : 0.96f);
        return Event::changed;
    }
    const InputFrame fresh;
    return refuse(feedback, style, fresh, refusal_, x);
}

Event ImageViewer::zoom_in(Feedback &feedback)
{
    if (images_.empty())
        return Event::none;
    const float limit = max_scale();
    const float x = stage().cx();
    if (zoom_.target >= limit - 0.005f)
    {
        const InputFrame fresh;
        return refuse(feedback, style, fresh, refusal_, x);
    }
    set_zoom(std::min(zoom_.target * std::max(style.zoom_step, 1.05f), limit));
    play_cue(feedback, style, style.sounds.step, x,
             tween::lerp(0.96f, 1.16f, tween::inverse_lerp(1.0f, limit, zoom_.target)));
    return Event::changed;
}

Event ImageViewer::zoom_out(Feedback &feedback)
{
    if (images_.empty())
        return Event::none;
    const float limit = min_scale();
    const float x = stage().cx();
    if (zoom_.target <= limit + 0.005f)
    {
        const InputFrame fresh;
        return refuse(feedback, style, fresh, refusal_, x);
    }
    set_zoom(std::max(zoom_.target / std::max(style.zoom_step, 1.05f), limit));
    play_cue(feedback, style, style.sounds.step, x,
             tween::lerp(0.96f, 1.16f, tween::inverse_lerp(1.0f, max_scale(), zoom_.target)));
    return Event::changed;
}

Event ImageViewer::cycle_zoom(Feedback &feedback)
{
    if (images_.empty())
        return Event::none;
    if (zoom_.target >= max_scale() - 0.005f)
    {
        set_mode(ZoomMode::fit);
        play_cue(feedback, style, style.sounds.change, stage().cx(), 0.94f);
        return Event::changed;
    }
    return zoom_in(feedback);
}

Event ImageViewer::browse(int direction, const InputFrame &input, Feedback &feedback)
{
    const int count = static_cast<int>(images_.size());
    const Rect st = stage();
    int next = index_ + direction;
    if (next < 0 || next >= count)
    {
        if (style.wrap && count > 1 && !input.nav_repeat)
        {
            next = (next + count) % count;
        }
        else
        {
            const Direction edge = direction < 0 ? Direction::left : Direction::right;
            if (style.exits.allows(edge))
            {
                exit_ = edge;
                return Event::none;
            }
            return refuse(feedback, style, input, refusal_, st.cx());
        }
    }
    const int slot = slot_ + direction;
    set_index(next, false);
    slot_ = slot;
    position_.target = static_cast<float>(slot_);
    swap_.trigger();
    const float along =
        count > 1 ? static_cast<float>(index_) / static_cast<float>(count - 1) : 0.5f;
    play_cue(feedback, style, style.sounds.move, st.x + st.w * along,
             tween::lerp(0.97f, 1.04f, along));
    return Event::moved;
}

Event ImageViewer::pan_step(Direction direction, const InputFrame &input, Feedback &feedback)
{
    const bool horizontal = direction == Direction::left || direction == Direction::right;
    const float sign = direction == Direction::right || direction == Direction::down ? 1.0f : -1.0f;
    tween::Spring &axis = horizontal ? pan_x_ : pan_y_;
    const Range limits = range(horizontal, zoom_.target);
    // lo is half the view, in pictures: a step is a share of what is in view.
    const float step = style.pan_step * 2.0f * limits.lo;
    const float next = std::clamp(axis.target + sign * step, limits.lo, limits.hi);
    const float x = stage().cx();
    if (std::fabs(next - axis.target) < 1e-4f)
    {
        // Nothing left to pan this way: the edge of the picture is an edge of
        // the viewer, and the focus may leave through it.
        if (style.exits.allows(direction))
        {
            exit_ = direction;
            return Event::none;
        }
        // Against the edge the picture gives a little and comes back.
        if (!style.reduced_motion && !input.nav_repeat)
        {
            float w = 1.0f;
            float h = 1.0f;
            fitted(index_, &w, &h);
            const float size = (horizontal ? w : h) * zoom_.target;
            axis.value = axis.target + sign * 0.6f * style.rubber / std::max(size, 1.0f);
        }
        Pulse none;
        return refuse(feedback, style, input, none, x);
    }
    axis.target = next;
    play_cue(feedback, style, style.sounds.step, x, 1.0f, 0.7f);
    return Event::changed;
}

Event ImageViewer::handle(const InputFrame &input, Feedback &feedback)
{
    exit_ = Direction::none;
    if (images_.empty())
        return Event::none;
    stick_x_ = input.stick_x;
    stick_y_ = input.stick_y;
    trigger_ = style.trigger_zoom ? input.trigger_r - input.trigger_l : 0.0f;

    if (input.nav != Direction::none)
    {
        const bool horizontal = input.nav == Direction::left || input.nav == Direction::right;
        const bool cropped = overflows(true) || overflows(false);
        // While the picture is cropped the left stick is an analog pan: the
        // steps the tracker derives from it must not also browse.
        if (cropped && input.nav_from_stick)
            return Event::none;
        if (overflows(horizontal))
            return pan_step(input.nav, input, feedback);
        if (horizontal)
            return browse(input.nav == Direction::right ? 1 : -1, input, feedback);
        if (style.exits.allows(input.nav))
            exit_ = input.nav;
        return Event::none;
    }
    if (input.is_pressed(Action::confirm))
        return toggle_mode(feedback);
    if (input.is_pressed(Action::back))
    {
        play_cue(feedback, style, style.sounds.cancel, stage().cx());
        // Back goes one step up: out of the zoom first, then out of the viewer.
        if (mode_ != ZoomMode::fit || std::fabs(zoom_.target - 1.0f) > 0.02f)
        {
            set_mode(ZoomMode::fit);
            return Event::changed;
        }
        return Event::cancelled;
    }
    return Event::none;
}

void ImageViewer::pan_axis(tween::Spring &axis, const Range &limits, float stick, float span,
                           float dt)
{
    const float rubber = style.reduced_motion ? 0.0f : style.rubber / std::max(span, 1.0f);
    if (std::fabs(stick) > 0.01f && limits.lo < limits.hi)
    {
        const float delta = stick * style.pan_speed * dt / std::max(span, 1.0f);
        float next = axis.target + delta;
        const float over =
            next < limits.lo ? limits.lo - next : (next > limits.hi ? next - limits.hi : 0.0f);
        if (over > 0.0f)
        {
            // Past the edge the pull meets a resistance that grows to a stop.
            const float give = rubber > 0.0f ? 1.0f - tween::clamp01(over / rubber) : 0.0f;
            next = std::clamp(axis.target + delta * give, limits.lo - rubber, limits.hi + rubber);
        }
        // The picture follows the stick itself; the spring is for the return.
        axis.value += next - axis.target;
        axis.target = next;
    }
    else
    {
        axis.target = std::clamp(axis.target, limits.lo, limits.hi);
    }
    axis.update(dt, std::max(style.omega(), 14.0f));
}

void ImageViewer::update(float dt)
{
    active_amount_.target = active_ ? 1.0f : 0.0f;
    active_amount_.update(dt, 18.0f);
    refusal_.update(dt, 9.0f);
    swap_.update(dt, 7.0f);
    if (images_.empty())
        return;

    if (std::fabs(trigger_) > 0.02f)
    {
        const float scale = std::clamp(zoom_.target * std::exp(trigger_ * style.zoom_rate * dt),
                                       min_scale(), max_scale());
        // An analog zoom is followed, not chased: the spring would lag it.
        zoom_.value += scale - zoom_.target;
        zoom_.target = scale;
        mode_ = std::fabs(scale - 1.0f) < 0.01f ? ZoomMode::fit : ZoomMode::free;
    }
    zoom_.update(dt, std::max(style.omega(), 12.0f), style.damping());
    zoom_.value = std::max(zoom_.value, 0.05f);

    float w = 1.0f;
    float h = 1.0f;
    fitted(index_, &w, &h);
    pan_axis(pan_x_, range(true, zoom_.target), stick_x_, w * zoom_.target, dt);
    pan_axis(pan_y_, range(false, zoom_.target), stick_y_, h * zoom_.target, dt);
    stick_x_ = stick_y_ = trigger_ = 0.0f;

    position_.update(dt, std::max(style.omega(), 12.0f), std::max(style.damping(), 0.85f));
    mark_.target = static_cast<float>(index_);
    mark_.update(dt, 18.0f);
    cropped_.target = overflows(true) || overflows(false) ? 1.0f : 0.0f;
    cropped_.update(dt, 14.0f);
}

// Draws the part of a picture that lies inside `window`. A texture is cut by
// its uv rectangle and a gradient by arithmetic, so neither needs a clip; a
// slot cannot be cut that way and gets one.
void ImageViewer::draw_picture(Canvas &canvas, const ViewerImage &image, int index,
                               const Rect &where, const Rect &window, float radius,
                               float alpha) const
{
    const float x0 = std::max(where.x, window.x);
    const float y0 = std::max(where.y, window.y);
    const float x1 = std::min(where.x + where.w, window.x + window.w);
    const float y1 = std::min(where.y + where.h, window.y + window.h);
    if (x1 <= x0 || y1 <= y0 || where.w <= 0.0f || where.h <= 0.0f || alpha <= 0.0f)
        return;
    gfx::DrawList &list = canvas.list;
    if (picture)
    {
        list.push_clip(window);
        picture(canvas, where, image, index, alpha);
        list.pop_clip();
        return;
    }
    const Rect visible{x0, y0, x1 - x0, y1 - y0};
    const float corner = std::min(radius, std::min(visible.w, visible.h) * 0.5f);
    const float u = (x0 - where.x) / where.w;
    const float v = (y0 - where.y) / where.h;
    if (image.texture != 0)
    {
        const Rect part{image.uv.x + image.uv.w * u, image.uv.y + image.uv.h * v,
                        image.uv.w * visible.w / where.w, image.uv.h * visible.h / where.h};
        list.image(image.texture, visible, part, Color{1.0f, 1.0f, 1.0f, alpha}, corner);
        return;
    }
    const Theme &theme = style.theme;
    const Color well = solid_surface(theme);
    const Color top = image.top.a > 0.0f ? image.top : gfx::mix(well, theme.text_muted, 0.2f);
    const Color bottom = image.bottom.a > 0.0f
                             ? image.bottom
                             : (image.top.a > 0.0f ? top : gfx::mix(well, theme.text_muted, 0.5f));
    list.gradient_rect(visible, corner, gfx::mix(top, bottom, v).with_alpha(alpha),
                       gfx::mix(top, bottom, (y1 - where.y) / where.h).with_alpha(alpha));
}

void ImageViewer::draw_overlays(Canvas &canvas, Painter &paint) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    const Rect in = inner_stage();
    const float inset = style.overlay_inset;
    const Color solid = solid_surface(theme);
    const float line = std::clamp(theme.border, 1.5f, 3.0f);
    const ViewerImage &image = images_[static_cast<std::size_t>(index_)];
    const int count = static_cast<int>(images_.size());

    // ---- the zoom readout ----
    const float shown = std::max(cropped_.value, active_amount_.value);
    if (style.readout && shown > 0.01f)
    {
        const char *name = mode_ == ZoomMode::fit      ? "Fit  "
                           : mode_ == ZoomMode::fill   ? "Fill  "
                           : mode_ == ZoomMode::actual ? "1:1  "
                                                       : "";
        char text[24];
        std::snprintf(text, sizeof(text), "%s%d%%", name, percent());
        const float size = style.readout_size;
        // Sized for the longest readout, so the plate does not twitch as the
        // digits change.
        const float width =
            std::max(paint.label_width(text, size), paint.label_width("Fill  000%", size)) + 28.0f;
        const float height = size + 18.0f;
        const Rect pill{in.x + inset, in.y + in.h - inset - height, width, height};
        const float corner = theme.pill_chips ? height * 0.5f : paint.control_radius(pill);
        list.push_opacity(tween::clamp01(shown));
        paint.fill(pill, corner, solid.with_alpha(0.92f));
        paint.stroke(pill, corner, line, theme.outline);
        paint.label(text, pill.cx(), pill.cy() + size * 0.34f, size, theme.text,
                    gfx::Align::center);
        list.pop_opacity();
    }

    // ---- the minimap ----
    const float map = tween::clamp01(cropped_.value);
    if (style.minimap && map > 0.01f)
    {
        const float aspect = std::max(image.aspect, 0.01f);
        float w = aspect >= 1.0f ? style.minimap_size : style.minimap_size * aspect;
        float h = w / aspect;
        if (h > in.h * 0.5f)
        {
            h = in.h * 0.5f;
            w = h * aspect;
        }
        const float rise = style.reduced_motion ? 0.0f : 10.0f * (1.0f - map);
        const Rect box{in.x + in.w - inset - w - 4.0f, in.y + in.h - inset - h - 4.0f + rise, w, h};
        const Rect plate = box.inset(-4.0f);
        const float corner = round_theme(theme) ? std::min(theme.radius, 8.0f) : 0.0f;
        list.push_opacity(map);
        paint.fill(plate, corner, solid);
        draw_picture(canvas, image, index_, box, box, 0.0f, 1.0f);

        float pw = 1.0f;
        float ph = 1.0f;
        fitted(index_, &pw, &ph);
        const float half_x = std::min(0.5f, in.w / (2.0f * pw * zoom_.value));
        const float half_y = std::min(0.5f, in.h / (2.0f * ph * zoom_.value));
        const float cx = std::clamp(pan_x_.value, half_x, 1.0f - half_x);
        const float cy = std::clamp(pan_y_.value, half_y, 1.0f - half_y);
        const Rect seen{box.x + (cx - half_x) * w, box.y + (cy - half_y) * h, 2.0f * half_x * w,
                        2.0f * half_y * h};
        // What is out of view is veiled; what is in view keeps its colours.
        const Color veil = solid.with_alpha(0.62f);
        list.rounded_rect({box.x, box.y, box.w, seen.y - box.y}, 0.0f, veil);
        list.rounded_rect({box.x, seen.y + seen.h, box.w, box.y + box.h - seen.y - seen.h}, 0.0f,
                          veil);
        list.rounded_rect({box.x, seen.y, seen.x - box.x, seen.h}, 0.0f, veil);
        list.rounded_rect({seen.x + seen.w, seen.y, box.x + box.w - seen.x - seen.w, seen.h}, 0.0f,
                          veil);
        list.bordered_rect(seen, 0.0f, Color{0.0f, 0.0f, 0.0f, 0.0f}, 2.5f, mark_color(theme));
        paint.stroke(plate, corner, line, theme.outline);
        list.pop_opacity();
    }

    // ---- the neighbours ----
    const float arrows = active_amount_.value * (1.0f - map);
    if (style.arrows && count > 1 && arrows > 0.01f)
    {
        const bool before = style.wrap || index_ > 0;
        const bool after = style.wrap || index_ + 1 < count;
        for (int side = 0; side < 2; ++side)
        {
            if ((side == 0 && !before) || (side == 1 && !after))
                continue;
            const float dir = side == 0 ? -1.0f : 1.0f;
            const float cx = side == 0 ? in.x + inset + 20.0f : in.x + in.w - inset - 20.0f;
            const float cy = in.cy();
            const Rect plate{cx - 20.0f, cy - 20.0f, 40.0f, 40.0f};
            const float corner = round_theme(theme) ? 20.0f : 0.0f;
            list.push_opacity(arrows);
            paint.fill(plate, corner, solid.with_alpha(0.88f));
            paint.stroke(plate, corner, line, theme.outline);
            list.line(cx - 4.0f * dir, cy - 9.0f, cx + 5.0f * dir, cy, 3.0f, theme.text);
            list.line(cx + 5.0f * dir, cy, cx - 4.0f * dir, cy + 9.0f, 3.0f, theme.text);
            list.pop_opacity();
        }
    }
}

void ImageViewer::draw_caption(Canvas &canvas, Painter &paint) const
{
    const Theme &theme = style.theme;
    const Rect st = stage();
    const ViewerImage &image = images_[static_cast<std::size_t>(index_)];
    const Color ink = style.on_panel ? theme.text : paint.page_text();
    const Color quiet = style.on_panel ? theme.text_muted : paint.page_text_muted();
    const float size = style.caption_size;
    const float baseline = st.y + st.h + style.caption_gap + size * 1.02f;
    float right = st.x + st.w;
    if (style.counter && images_.size() > 1)
    {
        char text[24];
        std::snprintf(text, sizeof(text), "%02d / %02d", index_ + 1,
                      static_cast<int>(images_.size()));
        right -= paint.label(text, right, baseline, size, quiet, gfx::Align::right) + 24.0f;
    }
    // The words of the picture that just arrived fade in after it.
    canvas.list.push_opacity(1.0f - 0.85f * swap_.value);
    const float room = std::max(right - st.x, 40.0f);
    const float taken =
        paint.bounded_label(image.title, st.x, baseline, size, ink, room);
    if (!image.caption.empty() && room - taken > 120.0f)
        paint.bounded_body(image.caption, st.x + taken + 18.0f, baseline, size, quiet, room - taken - 18.0f);
    canvas.list.pop_opacity();
}

void ImageViewer::draw_strip(Canvas &canvas, Painter &paint) const
{
    const Theme &theme = style.theme;
    const Rect box = strip();
    const int count = static_cast<int>(images_.size());
    const float thumb = style.thumb_size;
    const float pitch = thumb + style.thumb_gap;
    const float total = pitch * static_cast<float>(count) - style.thumb_gap;
    const bool scrolls = total > box.w;
    // A row that fits is centred; a longer one keeps the current picture in
    // the middle and stops at its ends.
    const float start = scrolls
                            ? box.x - std::clamp(mark_.value * pitch + thumb * 0.5f - box.w * 0.5f,
                                                 0.0f, total - box.w)
                            : box.cx() - total * 0.5f;
    const float corner = round_theme(theme) ? std::min(theme.radius, 10.0f) : 0.0f;
    for (int i = 0; i < count; ++i)
    {
        const Rect cell{start + pitch * static_cast<float>(i), box.y, thumb, thumb};
        if (cell.x + cell.w < box.x || cell.x > box.x + box.w)
            continue;
        const float near =
            tween::clamp01(1.0f - std::fabs(wrapped(static_cast<float>(i) - position_.value)));
        float alpha = tween::lerp(1.0f - style.thumb_dim, 1.0f, near);
        if (scrolls)
            alpha *=
                tween::clamp01(std::min(cell.x + cell.w - box.x, box.x + box.w - cell.x) / thumb);
        const float scale = style.reduced_motion ? 1.0f : 0.88f + 0.12f * near;
        const Rect shown = cell.inset(thumb * (1.0f - scale) * 0.5f);
        // The thumbnail is a square cut from the middle of the picture.
        const ViewerImage &image = images_[static_cast<std::size_t>(i)];
        const float aspect = std::max(image.aspect, 0.01f);
        const Rect where =
            aspect >= 1.0f
                ? Rect{shown.cx() - shown.h * aspect * 0.5f, shown.y, shown.h * aspect, shown.h}
                : Rect{shown.x, shown.cy() - shown.w / aspect * 0.5f, shown.w, shown.w / aspect};
        draw_picture(canvas, image, i, where, shown, corner, alpha);
    }
    const float x = start + pitch * mark_.value;
    if (x + thumb > box.x && x < box.x + box.w)
        paint.fill({x + thumb * 0.2f, box.y + thumb + 5.0f, thumb * 0.6f, 4.0f},
                   round_theme(theme) ? 2.0f : 0.0f,
                   mark_color(theme).with_alpha(0.55f + 0.45f * active_amount_.value));
}

void ImageViewer::draw(Canvas &canvas) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const Rect st = stage();
    const Rect in = inner_stage();
    // The picture cannot be chamfered or notched: a stage in such a language
    // is square, so the picture never pokes out of a cut corner.
    float corner = style.radius >= 0.0f ? style.radius : std::min(theme.radius_card, 18.0f);
    if (theme.corner != Corner::round)
        corner = 0.0f;
    corner = std::min(corner, std::min(st.w, st.h) * 0.5f);
    if (style.frame)
        paint.well(st, corner, theme.surface_high);
    if (images_.empty())
    {
        if (style.focus_ring)
            paint.focus_ring(st, corner, active_amount_.value);
        return;
    }

    const float inner_corner = std::max(0.0f, corner - (style.frame ? theme.border : 0.0f));
    const float nudge = shake(refusal_.value, canvas.time, 10.0f);
    const int count = static_cast<int>(images_.size());
    for (int i = 0; i < count; ++i)
    {
        const float away = wrapped(static_cast<float>(i) - position_.value);
        if (std::fabs(away) >= 1.0f)
            continue;
        const ViewerImage &image = images_[static_cast<std::size_t>(i)];
        float w = 1.0f;
        float h = 1.0f;
        fitted(i, &w, &h);
        float cx = 0.5f;
        float cy = 0.5f;
        if (i == index_)
        {
            // The centre is held inside what the size on screen allows, so a
            // zoom on its way out never shows past the picture's edge.
            const Range rx = range(true, zoom_.value);
            const Range ry = range(false, zoom_.value);
            const float give_x = style.rubber / (w * zoom_.value);
            const float give_y = style.rubber / (h * zoom_.value);
            cx = std::clamp(pan_x_.value, rx.lo - give_x, rx.hi + give_x);
            cy = std::clamp(pan_y_.value, ry.lo - give_y, ry.hi + give_y);
            w *= zoom_.value;
            h *= zoom_.value;
        }
        else
        {
            const float scale = scale_for(i, mode_);
            w *= scale;
            h *= scale;
        }
        const Rect where{in.cx() - cx * w + away * (in.w + style.slide_gap) + nudge,
                         in.cy() - cy * h, w, h};
        draw_picture(canvas, image, i, where, in, inner_corner, 1.0f);
    }

    if (style.focus_ring)
        paint.focus_ring(st, corner, active_amount_.value);
    draw_overlays(canvas, paint);
    if (style.caption)
        draw_caption(canvas, paint);
    if (strip_shown())
        draw_strip(canvas, paint);
}

} // namespace hui::ui
