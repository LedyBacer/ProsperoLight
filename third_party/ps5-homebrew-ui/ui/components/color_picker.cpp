// ps5-homebrew-ui - Component: ColorPicker.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/color_picker.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace hui::ui
{

using gfx::Color;
using gfx::Rect;

namespace
{

// The presets a picker starts with: content, like the options of a list.
constexpr std::uint32_t kPalette[] = {
    0xe5484d, 0xff7a59, 0xf5a524, 0xf2d024, 0x8ccf3f, 0x30a46c, 0x12a594, 0x22b8cf, 0x3e9eff,
    0x4263eb, 0x6e56cf, 0xab4aba, 0xe64980, 0xa5673f, 0xffffff, 0xb5bac4, 0x5c6370, 0x16181d,
};

float wrap_degrees(float h)
{
    h = std::fmod(h, 360.0f);
    return h < 0.0f ? h + 360.0f : h;
}

bool same(Color a, Color b)
{
    return std::fabs(a.r - b.r) < 0.002f && std::fabs(a.g - b.g) < 0.002f &&
           std::fabs(a.b - b.b) < 0.002f;
}

// The check mark of Painter::checkbox, on its own.
void draw_check(gfx::DrawList &list, const Rect &r, float value, Color ink)
{
    if (value <= 0.01f)
        return;
    const float s = r.w;
    const float x0 = r.x + s * 0.24f, y0 = r.y + s * 0.52f;
    const float x1 = r.x + s * 0.43f, y1 = r.y + s * 0.70f;
    const float x2 = r.x + s * 0.77f, y2 = r.y + s * 0.31f;
    const float first = tween::clamp01(value * 2.5f);
    const float second = tween::clamp01((value - 0.4f) / 0.6f);
    const float width = std::max(3.0f, s * 0.1f);
    list.line(x0, y0, x0 + (x1 - x0) * first, y0 + (y1 - y0) * first, width, ink);
    if (second > 0.0f)
        list.line(x1, y1, x1 + (x2 - x1) * second, y1 + (y2 - y1) * second, width, ink);
}

// The line that parts a colour from the surface it lies on. Some themes have
// no outline colour at all; a white swatch on a white panel still needs one.
Color edge_color(const Theme &theme)
{
    return theme.outline.a > 0.2f ? theme.outline : theme.text_muted.with_alpha(0.4f);
}

} // namespace

ColorPicker::ColorPicker()
{
    for (const std::uint32_t hex : kPalette)
        palette_.push_back(Color::rgb(hex));
    checks_.assign(palette_.size(), tween::Spring{});
    set_index(8);
}

Color ColorPicker::from_hsv(float h, float s, float v)
{
    h = wrap_degrees(h) / 60.0f;
    s = tween::clamp01(s);
    v = tween::clamp01(v);
    const int sector = static_cast<int>(h) % 6;
    const float f = h - std::floor(h);
    const float p = v * (1.0f - s);
    const float q = v * (1.0f - s * f);
    const float t = v * (1.0f - s * (1.0f - f));
    switch (sector)
    {
    case 0:
        return {v, t, p, 1.0f};
    case 1:
        return {q, v, p, 1.0f};
    case 2:
        return {p, v, t, 1.0f};
    case 3:
        return {p, q, v, 1.0f};
    case 4:
        return {t, p, v, 1.0f};
    default:
        return {v, p, q, 1.0f};
    }
}

void ColorPicker::to_hsv(Color color, float *h, float *s, float *v)
{
    const float high = std::max(color.r, std::max(color.g, color.b));
    const float low = std::min(color.r, std::min(color.g, color.b));
    const float span = high - low;
    *v = high;
    *s = high > 0.0f ? span / high : 0.0f;
    if (span <= 1e-5f)
    {
        *h = 0.0f;
        return;
    }
    float hue = 0.0f;
    if (high == color.r)
        hue = (color.g - color.b) / span;
    else if (high == color.g)
        hue = 2.0f + (color.b - color.r) / span;
    else
        hue = 4.0f + (color.r - color.g) / span;
    *h = wrap_degrees(hue * 60.0f);
}

std::string ColorPicker::hex_of(Color color)
{
    const auto byte = [](float value)
    { return static_cast<int>(std::lround(tween::clamp01(value) * 255.0f)); };
    char text[16];
    std::snprintf(text, sizeof(text), "#%02X%02X%02X", byte(color.r), byte(color.g), byte(color.b));
    return text;
}

void ColorPicker::set_palette(std::vector<Color> colors)
{
    palette_ = std::move(colors);
    checks_.assign(palette_.size(), tween::Spring{});
    focus_ = std::clamp(focus_, 0, std::max(static_cast<int>(palette_.size()) - 1, 0));
    set_color(color_);
}

void ColorPicker::set_color(Color color)
{
    color_ = {color.r, color.g, color.b, 1.0f};
    // A grey has no hue of its own: it keeps the strip where it was.
    float hue = h_;
    to_hsv(color_, &hue, &s_, &v_);
    if (s_ > 0.001f && v_ > 0.001f)
        h_ = hue;
    index_ = -1;
    for (std::size_t i = 0; i < palette_.size(); ++i)
    {
        if (same(palette_[i], color_))
            index_ = static_cast<int>(i);
    }
    if (index_ >= 0)
        focus_ = index_;
    shown_h_.snap(h_);
    placed_ = false;
}

void ColorPicker::set_index(int index)
{
    if (index < 0 || index >= static_cast<int>(palette_.size()))
        return;
    set_color(palette_[static_cast<std::size_t>(index)]);
    index_ = index;
    focus_ = index;
}

void ColorPicker::set_focus(int index)
{
    focus_ = std::clamp(index, 0, std::max(static_cast<int>(palette_.size()) - 1, 0));
}

void ColorPicker::apply_hsv()
{
    color_ = from_hsv(h_, s_, v_);
    index_ = -1;
    for (std::size_t i = 0; i < palette_.size(); ++i)
    {
        if (same(palette_[i], color_))
            index_ = static_cast<int>(i);
    }
}

ColorPicker::Layout ColorPicker::layout() const
{
    Layout out;
    const float top = title_.empty() ? 0.0f : style.title_size + style.title_gap;
    Rect area{0.0f, top, bounds_.w, std::max(bounds_.h - top, 1.0f)};
    if (style.preview && bounds_.w > style.preview_width * 2.0f)
    {
        area.w -= style.preview_width + style.preview_gap;
        out.preview = {area.x + area.w + style.preview_gap, top, style.preview_width, area.h};
    }
    out.area = area;
    const int count = static_cast<int>(palette_.size());
    out.cols = std::clamp(style.columns, 1, std::max(count, 1));
    const float fit = (area.w - static_cast<float>(out.cols - 1) * style.swatch_gap) /
                      static_cast<float>(out.cols);
    out.cell = style.swatch_size > 0.0f ? std::min(style.swatch_size, fit) : fit;
    const float strip = std::min(style.hue_height, area.h * 0.4f);
    out.sv = {area.x, area.y, area.w, std::max(area.h - strip - style.hue_gap, 1.0f)};
    out.hue = {area.x, area.y + area.h - strip, area.w, strip};
    return out;
}

Rect ColorPicker::swatch_rect(const Layout &at, int index) const
{
    const float pitch = at.cell + style.swatch_gap;
    return {at.area.x + static_cast<float>(index % at.cols) * pitch,
            at.area.y + static_cast<float>(index / at.cols) * pitch, at.cell, at.cell};
}

float ColorPicker::preferred_height() const
{
    const float top = title_.empty() ? 0.0f : style.title_size + style.title_gap;
    if (style.kind == ColorPickerKind::hsv)
        return top + style.sv_height + style.hue_gap + style.hue_height;
    const Layout at = layout();
    const int count = static_cast<int>(palette_.size());
    const int rows = (count + at.cols - 1) / std::max(at.cols, 1);
    return top + static_cast<float>(rows) * at.cell +
           static_cast<float>(std::max(rows - 1, 0)) * style.swatch_gap;
}

float ColorPicker::center_x() const
{
    const Layout at = layout();
    return bounds_.x + at.area.cx();
}

Event ColorPicker::pick(int index, Feedback &feedback)
{
    const int count = static_cast<int>(palette_.size());
    const Layout at = layout();
    // set_color asks for a snap, which is right for a colour loaded from
    // storage; a pick must ease from where things are.
    const bool was_placed = placed_;
    set_index(index);
    placed_ = was_placed;
    press_.trigger();
    const float along =
        count > 1 ? static_cast<float>(index) / static_cast<float>(count - 1) : 0.0f;
    play_cue(feedback, style, style.sounds.change, bounds_.x + swatch_rect(at, index).cx(),
             style.pitch_by_value ? tween::lerp(0.92f, 1.14f, along) : 1.0f);
    return Event::changed;
}

Event ColorPicker::handle_swatches(const InputFrame &input, Feedback &feedback)
{
    const int count = static_cast<int>(palette_.size());
    const Layout at = layout();
    if (input.nav != Direction::none)
    {
        const Direction d = input.nav;
        int next = -1;
        if (count > 0)
        {
            const int col = focus_ % at.cols;
            if (d == Direction::left && col > 0)
                next = focus_ - 1;
            else if (d == Direction::right && col < at.cols - 1 && focus_ + 1 < count)
                next = focus_ + 1;
            else if (d == Direction::up && focus_ - at.cols >= 0)
                next = focus_ - at.cols;
            else if (d == Direction::down && focus_ + at.cols < count)
                next = focus_ + at.cols;
        }
        const float x = count > 0 ? bounds_.x + swatch_rect(at, focus_).cx() : bounds_.cx();
        if (next < 0)
        {
            if (style.exits.allows(d))
            {
                exit_ = d;
                return Event::none;
            }
            return refuse(feedback, style, input, highlight_.refusal(), x);
        }
        focus_ = next;
        if (style.select_on_move)
            return pick(focus_, feedback);
        const float along =
            count > 1 ? static_cast<float>(focus_) / static_cast<float>(count - 1) : 0.0f;
        play_cue(feedback, style, style.sounds.move, bounds_.x + swatch_rect(at, focus_).cx(),
                 style.pitch_by_value ? tween::lerp(0.95f, 1.1f, along) : 1.0f);
        return Event::moved;
    }
    if (input.is_pressed(Action::confirm) && count > 0)
    {
        if (index_ == focus_)
        {
            if (!style.reduced_motion)
                press_.trigger(0.6f);
            return Event::none;
        }
        return pick(focus_, feedback);
    }
    return Event::none;
}

Event ColorPicker::handle_hsv(const InputFrame &input, Feedback &feedback)
{
    const float x = center_x();
    stick_x_ = input.stick_x;
    stick_y_ = input.stick_y;
    stick_hue_ = input.stick2_x;
    stick_live_ = true;

    // What the sticks did since the last frame is reported now: update() has
    // the time step, handle() has the feedback.
    Event event = Event::none;
    if (stick_changed_)
    {
        stick_changed_ = false;
        event = Event::changed;
        if (travelled_ >= 0.05f)
        {
            travelled_ = 0.0f;
            const float pitch = zone_ == 1 ? 0.9f + 0.3f * h_ / 360.0f : 0.9f + 0.3f * v_;
            play_cue(feedback, style, style.tick, x, style.pitch_by_value ? pitch : 1.0f, 0.7f);
        }
    }

    // A step that came from the stick is the same tilt that already moves
    // the marker smoothly.
    if (input.nav != Direction::none && !input.nav_from_stick)
    {
        const Direction d = input.nav;
        held_ = input.nav_repeat ? held_ + 1 : 0;
        const float stride = style.fast_after > 0 && held_ >= style.fast_after
                                 ? static_cast<float>(std::max(style.fast_factor, 1))
                                 : 1.0f;
        const bool sideways = d == Direction::left || d == Direction::right;
        const float sign = d == Direction::right || d == Direction::up ? 1.0f : -1.0f;
        if (zone_ == 1)
        {
            if (sideways)
            {
                const float turn = sign * style.hue_step * stride;
                h_ = wrap_degrees(h_ + turn);
                shown_h_.target += turn;
                apply_hsv();
                play_cue(feedback, style, style.sounds.step, x,
                         style.pitch_by_value ? 0.9f + 0.3f * h_ / 360.0f : 1.0f);
                return Event::changed;
            }
            if (d == Direction::up)
            {
                zone_ = 0;
                play_cue(feedback, style, style.sounds.move, x, 1.04f);
                return Event::moved;
            }
        }
        else
        {
            float &channel = sideways ? s_ : v_;
            const float next = tween::clamp01(channel + sign * style.sv_step * stride);
            if (next != channel)
            {
                channel = next;
                apply_hsv();
                play_cue(feedback, style, style.sounds.step, x,
                         style.pitch_by_value ? 0.9f + 0.3f * (s_ + v_) * 0.5f : 1.0f);
                return Event::changed;
            }
            // The strip lies under the field: down from the bottom reaches it.
            if (d == Direction::down && !input.nav_repeat)
            {
                zone_ = 1;
                play_cue(feedback, style, style.sounds.move, x, 0.96f);
                return Event::moved;
            }
        }
        if (style.exits.allows(d))
        {
            exit_ = d;
            return Event::none;
        }
        return refuse(feedback, style, input, highlight_.refusal(), x);
    }
    if (input.is_pressed(Action::confirm))
    {
        zone_ = 1 - zone_;
        press_.trigger();
        play_cue(feedback, style, style.sounds.move, x, zone_ == 1 ? 0.96f : 1.04f);
        return Event::moved;
    }
    return event;
}

Event ColorPicker::handle(const InputFrame &input, Feedback &feedback)
{
    exit_ = Direction::none;
    const Event event = style.kind == ColorPickerKind::hsv ? handle_hsv(input, feedback)
                                                           : handle_swatches(input, feedback);
    if (event != Event::none || exit_ != Direction::none)
        return event;
    if (input.is_pressed(Action::back))
    {
        play_cue(feedback, style, style.sounds.cancel, center_x());
        return Event::cancelled;
    }
    return Event::none;
}

void ColorPicker::update(float dt)
{
    active_amount_.target = active_ ? 1.0f : 0.0f;
    active_amount_.update(dt, 18.0f);
    press_.update(dt, 10.0f);

    if (stick_live_ && style.kind == ColorPickerKind::hsv)
    {
        const float move = style.stick_speed * dt;
        const float before_s = s_;
        const float before_v = v_;
        const float before_h = h_;
        float turn = stick_hue_ * move * 360.0f;
        if (zone_ == 0)
        {
            s_ = tween::clamp01(s_ + stick_x_ * move);
            v_ = tween::clamp01(v_ - stick_y_ * move);
        }
        else
        {
            turn += stick_x_ * move * 360.0f;
        }
        if (turn != 0.0f)
        {
            h_ = wrap_degrees(h_ + turn);
            shown_h_.target += turn;
        }
        if (s_ != before_s || v_ != before_v || h_ != before_h)
        {
            apply_hsv();
            stick_changed_ = true;
            travelled_ +=
                std::fabs(s_ - before_s) + std::fabs(v_ - before_v) + std::fabs(turn) / 360.0f;
        }
    }
    stick_live_ = false;

    const Layout at = layout();
    Rect target = at.area;
    if (style.kind == ColorPickerKind::swatches)
    {
        const int count = static_cast<int>(palette_.size());
        const int rows = (count + at.cols - 1) / std::max(at.cols, 1);
        const float pitch = at.cell + style.swatch_gap;
        target = {at.area.x, at.area.y, static_cast<float>(at.cols) * pitch - style.swatch_gap,
                  static_cast<float>(rows) * pitch - style.swatch_gap};
        if (engaged_ && count > 0)
            target = swatch_rect(at, focus_);
    }
    else if (engaged_)
    {
        target = zone_ == 1 ? at.hue : at.sv;
    }
    highlight_.target(target);

    const float omega = std::max(style.omega(), 16.0f) * 1.2f;
    shown_s_.target = s_;
    shown_v_.target = v_;
    shown_color_.target(color_);
    for (std::size_t i = 0; i < checks_.size(); ++i)
        checks_[i].target = static_cast<int>(i) == index_ ? 1.0f : 0.0f;
    if (!placed_)
    {
        highlight_.snap(target);
        shown_s_.snap(s_);
        shown_v_.snap(v_);
        shown_color_.snap(color_);
        for (tween::Spring &check : checks_)
            check.snap(check.target);
        placed_ = true;
    }
    highlight_.update(dt, style);
    shown_s_.update(dt, omega);
    shown_v_.update(dt, omega);
    shown_h_.update(dt, omega);
    shown_color_.update(dt, omega);
    for (tween::Spring &check : checks_)
        check.update(dt, omega);
    // The hue is kept unwrapped while it eases, so 350 to 10 goes the short
    // way round; pull both ends back before the numbers grow.
    if (std::fabs(shown_h_.target) > 3600.0f)
    {
        const float shift = shown_h_.target - wrap_degrees(shown_h_.target);
        shown_h_.target -= shift;
        shown_h_.value -= shift;
    }
}

void ColorPicker::draw(Canvas &canvas) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const Color quiet = style.on_page ? paint.page_text_muted() : theme.text_muted;
    const Color resting = style.on_page ? paint.page_text() : theme.text;
    if (!title_.empty())
        paint.label(fit_label(paint, title_, style.title_size, bounds_.w), bounds_.x,
                    bounds_.y + style.title_size * 0.82f, style.title_size, quiet);

    const Layout at = layout();
    const auto placed = [&](const Rect &r)
    { return Rect{bounds_.x + r.x, bounds_.y + r.y, r.w, r.h}; };
    const Color edge = edge_color(theme);
    const float line = std::max(theme.border, 1.5f);
    const bool square = theme.corner != Corner::round || theme.radius < 2.0f;
    const float active = active_amount_.value;
    const float pop = style.reduced_motion ? 0.0f : press_.value;
    const Rect glide = placed(highlight_.rect(canvas.time));
    float ring_radius = 0.0f;

    if (style.kind == ColorPickerKind::swatches)
    {
        for (std::size_t i = 0; i < palette_.size(); ++i)
        {
            const Rect r = placed(swatch_rect(at, static_cast<int>(i)));
            const float radius = style.swatch_radius < 0.0f
                                     ? paint.control_radius(r)
                                     : std::min(style.swatch_radius, r.w * 0.5f);
            ring_radius = radius;
            const bool pressed = static_cast<int>(i) == focus_ && pop > 0.01f;
            if (pressed)
                list.push_transform(1.0f - 0.1f * pop, r.cx(), r.cy(), 0.0f, 0.0f);
            if (style.swatch_radius < 0.0f)
            {
                paint.fill(r, radius, palette_[i]);
                paint.stroke(r, radius, line, edge);
            }
            else
            {
                // A radius of the caller's own is a shape of the caller's
                // own: round stays round in themes that cut or notch corners.
                list.rounded_rect(r, radius, palette_[i]);
                list.bordered_rect(r, radius, edge.with_alpha(0.0f), line, edge);
            }
            draw_check(list, r, checks_[i].value, Painter::on(palette_[i]));
            if (pressed)
                list.pop_transform();
        }
        // Around the whole grid the ring is a panel's, around a swatch its own.
        if (!engaged_)
            ring_radius = std::min(theme.radius, 12.0f);
    }
    else
    {
        const Rect sv = placed(at.sv);
        const Rect strip = placed(at.hue);
        const float radius = square ? 0.0f : std::min(theme.radius, 10.0f);
        ring_radius = radius;
        const float hue = wrap_degrees(shown_h_.value);
        const Color pure = from_hsv(hue, 1.0f, 1.0f);
        const Color white = from_hsv(0.0f, 0.0f, 1.0f);
        const Color black = from_hsv(0.0f, 0.0f, 0.0f);

        // Saturation runs left to right from white to the pure hue; value is
        // a veil of black that thickens toward the bottom. Two gradients
        // give every colour of the hue exactly.
        // (A pixel-art frame has no corner dots: the colours stay inside it.)
        const float notch = theme.corner == Corner::pixel ? line : 0.0f;
        list.gradient_rect_h(sv.inset(notch), radius, white, pure);
        list.gradient_rect(sv.inset(notch), radius, black.with_alpha(0.0f), black);
        paint.stroke(sv, radius, line, edge);

        // Hue is linear between the six primaries, so six gradients are the
        // whole wheel. Both ends are red: a rounded red base gives the strip
        // its caps and the gradients fill the straight part.
        const float cap = std::max(std::min(radius, strip.h * 0.5f), notch);
        list.rounded_rect(strip.inset(notch), notch > 0.0f ? 0.0f : cap,
                          from_hsv(0.0f, 1.0f, 1.0f));
        const float run = strip.w - 2.0f * cap;
        for (int i = 0; i < 6; ++i)
        {
            const float x0 = strip.x + cap + run * static_cast<float>(i) / 6.0f;
            // Half a pixel of overlap hides the seams between the pieces.
            list.gradient_rect_h(
                {x0, strip.y + notch, run / 6.0f + (i < 5 ? 0.5f : 0.0f), strip.h - 2.0f * notch},
                0.0f, from_hsv(60.0f * static_cast<float>(i), 1.0f, 1.0f),
                from_hsv(60.0f * static_cast<float>(i + 1), 1.0f, 1.0f));
        }
        paint.stroke(strip, cap, line, edge);

        // ---- the markers: the one whose zone is active is the larger ----
        const float in_field = engaged_ && zone_ == 0 ? active : 0.0f;
        const float in_strip = engaged_ && zone_ == 1 ? active : 0.0f;
        const Color shown = from_hsv(hue, shown_s_.value, shown_v_.value);
        const Color rim = Painter::on(shown);
        const float size = style.cursor_size * (1.0f + 0.3f * in_field - 0.1f * pop);
        const float half = size * 0.5f;
        const float mx = std::clamp(sv.x + sv.w * tween::clamp01(shown_s_.value), sv.x + half,
                                    sv.x + sv.w - half);
        const float my = std::clamp(sv.y + sv.h * (1.0f - tween::clamp01(shown_v_.value)),
                                    sv.y + half, sv.y + sv.h - half);
        list.shadow({mx - half, my - half + 2.0f, size, size}, square ? 0.0f : half, 6.0f,
                    black.with_alpha(0.35f));
        if (square)
        {
            list.rounded_rect({mx - half, my - half, size, size}, 0.0f, rim);
            list.rounded_rect({mx - half + 3.0f, my - half + 3.0f, size - 6.0f, size - 6.0f}, 0.0f,
                              shown);
        }
        else
        {
            list.circle(mx, my, half, rim);
            list.circle(mx, my, half - 3.0f, shown);
        }

        const float grip = 14.0f + 4.0f * in_strip;
        const float reach = 4.0f + 2.0f * in_strip;
        const float hx = strip.x + cap + run * hue / 360.0f;
        const Rect thumb{hx - grip * 0.5f, strip.y - reach, grip, strip.h + 2.0f * reach};
        const float thumb_radius = square ? 0.0f : grip * 0.5f;
        list.shadow({thumb.x, thumb.y + 2.0f, thumb.w, thumb.h}, thumb_radius, 6.0f,
                    black.with_alpha(0.35f));
        list.rounded_rect(thumb, thumb_radius, Painter::on(pure));
        list.rounded_rect(thumb.inset(3.0f), std::max(thumb_radius - 3.0f, 0.0f), pure);
        if (!engaged_)
            ring_radius = std::min(theme.radius, 12.0f);
    }

    if (style.focus_ring)
        paint.focus_ring(glide, std::min(ring_radius, std::min(glide.w, glide.h) * 0.5f), active);

    // ---- the preview ----
    if (at.preview.w > 0.0f)
    {
        const Rect column = placed(at.preview);
        const float lines = style.hex ? style.hex_size * 1.5f + 20.0f * 1.4f : 0.0f;
        const Rect box{column.x, column.y, column.w, std::max(column.h - lines - 6.0f, 24.0f)};
        const float radius = std::min(theme.radius_card, std::min(box.w, box.h) * 0.3f);
        paint.fill(box, radius, shown_color_.value());
        paint.stroke(box, radius, line, edge);
        if (style.hex)
        {
            const float first = box.y + box.h + 6.0f + style.hex_size;
            paint.label(fit_label(paint, hex(), style.hex_size, column.w), column.x, first,
                        style.hex_size, resting);
            char text[48];
            if (style.kind == ColorPickerKind::hsv)
            {
                const int h = static_cast<int>(std::lround(h_)) % 360;
                const int s = static_cast<int>(std::lround(s_ * 100.0f));
                const int v = static_cast<int>(std::lround(v_ * 100.0f));
                std::snprintf(text, sizeof(text), "H %d S %d V %d", h, s, v);
                // Wide faces have no room for the letters: the numbers alone.
                if (paint.body_width(text, 19.0f) > column.w)
                    std::snprintf(text, sizeof(text), "%d %d %d", h, s, v);
            }
            else if (index_ >= 0)
            {
                std::snprintf(text, sizeof(text), "Preset %d of %d", index_ + 1,
                              static_cast<int>(palette_.size()));
                if (paint.body_width(text, 19.0f) > column.w)
                    std::snprintf(text, sizeof(text), "%d of %d", index_ + 1,
                                  static_cast<int>(palette_.size()));
            }
            else
            {
                std::snprintf(text, sizeof(text), "Custom");
            }
            paint.body(fit_body(paint, text, 19.0f, column.w), column.x, first + 19.0f * 1.4f,
                       19.0f, quiet);
        }
    }
}

} // namespace hui::ui
