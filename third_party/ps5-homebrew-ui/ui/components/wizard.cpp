// ps5-homebrew-ui - Component: Wizard.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/wizard.hpp"

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

constexpr float kButtonsGap = 20.0f; // between the indicator and the button row
constexpr float kMargin = 6.0f;      // room around the markers for a pop and a halo

// Languages whose controls are square: a round marker would not belong.
bool square_language(const Theme &theme)
{
    return theme.corner != Corner::round || theme.radius < 2.0f ||
           theme.style == SurfaceStyle::bevel || theme.style == SurfaceStyle::sketch;
}

bool chunky(const Theme &theme)
{
    return theme.style == SurfaceStyle::hard || theme.style == SurfaceStyle::bevel ||
           theme.style == SurfaceStyle::pixel;
}

// A filled plate in a status colour, built the way the theme builds its main
// button. Returns the colour that reads on it.
Color plate(Painter &paint, const Rect &r, float radius, Color tone)
{
    const Theme &theme = paint.theme();
    if (theme.style == SurfaceStyle::glow)
    {
        // On lit consoles the edge carries the colour, not the fill.
        paint.surface(r, radius, theme.surface, tone, 1.0f, std::max(theme.border, 2.0f));
        return tone;
    }
    const bool framed = chunky(theme);
    const bool pen = theme.style == SurfaceStyle::sketch;
    const float line = theme.button_border < 0.0f ? theme.border : theme.button_border;
    paint.surface(r, radius, tone, framed ? theme.outline : (pen ? Painter::on(tone) : tone), 1.0f,
                  framed || pen ? theme.border : line);
    return Painter::on(tone);
}

} // namespace

void Wizard::set_steps(std::vector<WizardStep> steps)
{
    steps_ = std::move(steps);
    step_ = std::clamp(step_, 0, count());
    retarget(true);
}

void Wizard::set_bounds(const Rect &bounds)
{
    bounds_ = bounds;
    highlight_.snap(button_rect(button_));
}

void Wizard::set_step(int step, bool snap)
{
    step_ = std::clamp(step, 0, count());
    retarget(snap);
}

void Wizard::set_error(int index, bool error)
{
    if (index >= 0 && index < count())
        steps_[static_cast<std::size_t>(index)].error = error;
}

void Wizard::set_button(int button)
{
    button_ = std::clamp(button, 0, 1);
    highlight_.snap(button_rect(button_));
}

void Wizard::retarget(bool snap)
{
    position_.target = static_cast<float>(step_);
    if (snap)
        position_.snap(position_.target);
    else if (!style.reduced_motion)
        bump_.trigger();
}

// ---- layout ----------------------------------------------------------------

Rect Wizard::button_rect(int button) const
{
    const float width = 2.0f * style.button_width + style.button_gap;
    const float x = bounds_.x + bounds_.w - width +
                    static_cast<float>(button) * (style.button_width + style.button_gap);
    // Beside the dots when compact, under the markers otherwise.
    const float y = style.kind == WizardKind::compact ? bounds_.cy() - style.button_height * 0.5f
                                                      : bounds_.y + bounds_.h - style.button_height;
    return {x, y, style.button_width, style.button_height};
}

Rect Wizard::indicator() const
{
    if (!style.buttons)
        return bounds_;
    if (style.kind == WizardKind::compact)
    {
        const float taken = 2.0f * style.button_width + style.button_gap + 24.0f;
        return {bounds_.x, bounds_.y, std::max(bounds_.w - taken, 40.0f), bounds_.h};
    }
    return {bounds_.x, bounds_.y, bounds_.w,
            std::max(bounds_.h - style.button_height - kButtonsGap, 40.0f)};
}

// Vertical: the distance between two steps.
float Wizard::pitch() const
{
    const int n = count();
    if (n < 2)
        return style.step_height;
    const float room = indicator().h - style.marker_size * style.current_scale - 2.0f * kMargin;
    return std::min(style.step_height, room / static_cast<float>(n - 1));
}

float Wizard::centre_x(int index) const
{
    const Rect in = indicator();
    if (style.kind == WizardKind::vertical)
        return in.x + style.marker_size * style.current_scale * 0.5f + kMargin;
    const float slot = in.w / static_cast<float>(std::max(count(), 1));
    return in.x + slot * (static_cast<float>(index) + 0.5f);
}

float Wizard::centre_y(int index) const
{
    const Rect in = indicator();
    const float first = in.y + style.marker_size * style.current_scale * 0.5f + kMargin;
    if (style.kind == WizardKind::vertical)
        return first + pitch() * static_cast<float>(index);
    return first;
}

Rect Wizard::marker_rect(int index) const
{
    const float half = style.marker_size * 0.5f;
    return {centre_x(index) - half, centre_y(index) - half, style.marker_size, style.marker_size};
}

// ---- behaviour -------------------------------------------------------------

Event Wizard::next(Feedback &feedback)
{
    const int n = count();
    const float x = style.kind == WizardKind::horizontal && n > 0 ? centre_x(std::min(step_, n - 1))
                                                                  : bounds_.cx();
    if (n == 0 || finished())
        return refuse(feedback, style, InputFrame{}, highlight_.refusal(), x);
    ++step_;
    retarget(false);
    if (finished())
    {
        play_cue(feedback, style, style.sounds.activate, x);
        if (style.sounds.rumble > 0.0f)
            feedback.rumble(0.3f * style.sounds.rumble, 0.05f);
        return Event::activated;
    }
    // The flow climbs: every step forward sounds a little higher.
    const float along = n > 1 ? static_cast<float>(step_) / static_cast<float>(n - 1) : 0.0f;
    play_cue(feedback, style, style.sounds.page, x,
             style.pitch_by_position ? tween::lerp(0.96f, 1.12f, along) : 1.0f);
    return Event::changed;
}

Event Wizard::back(Feedback &feedback)
{
    const int n = count();
    const float x = style.kind == WizardKind::horizontal && n > 0 ? centre_x(std::min(step_, n - 1))
                                                                  : bounds_.cx();
    if (n == 0 || step_ == 0)
        return refuse(feedback, style, InputFrame{}, highlight_.refusal(), x);
    --step_;
    retarget(false);
    const float along = n > 1 ? static_cast<float>(step_) / static_cast<float>(n - 1) : 0.0f;
    play_cue(feedback, style, style.sounds.page, x,
             style.pitch_by_position ? tween::lerp(0.92f, 1.08f, along) : 1.0f);
    return Event::changed;
}

Event Wizard::handle(const InputFrame &input, Feedback &feedback)
{
    exit_ = Direction::none;
    if (!style.buttons || steps_.empty())
        return Event::none;
    const float x = button_rect(button_).cx();
    if (input.nav == Direction::left || input.nav == Direction::right)
    {
        const int wanted = button_ + (input.nav == Direction::right ? 1 : -1);
        if (wanted < 0 || wanted > 1)
        {
            if (style.exits.allows(input.nav))
            {
                exit_ = input.nav;
                return Event::none;
            }
            return refuse(feedback, style, input, highlight_.refusal(), x);
        }
        button_ = wanted;
        play_cue(feedback, style, style.sounds.move, button_rect(button_).cx());
        return Event::moved;
    }
    if (input.nav == Direction::up || input.nav == Direction::down)
    {
        if (style.exits.allows(input.nav))
            exit_ = input.nav;
        return Event::none;
    }
    if (input.is_pressed(Action::confirm))
    {
        press_.trigger();
        pressed_ = button_;
        return button_ == 1 ? next(feedback) : back(feedback);
    }
    if (input.is_pressed(Action::back))
    {
        // Back goes exactly one step up; on the first step it leaves the flow.
        if (step_ > 0)
            return back(feedback);
        play_cue(feedback, style, style.sounds.cancel, x);
        return Event::cancelled;
    }
    return Event::none;
}

void Wizard::update(float dt)
{
    // A marker may pop, but a track that swings past its end reads as a
    // glitch: the theme's bounce is kept mild here.
    position_.update(dt, std::max(style.omega(), 12.0f), std::max(style.damping(), 0.85f));
    bump_.update(dt, 7.0f);
    focus_amount_.target = focused_ ? 1.0f : 0.0f;
    focus_amount_.update(dt, 20.0f);
    highlight_.target(button_rect(button_));
    highlight_.update(dt, style);
    press_.update(dt, 8.0f);
}

// ---- drawing ---------------------------------------------------------------

void Wizard::draw_track(Canvas &canvas, float value) const
{
    const int n = count();
    if (n < 2)
        return;
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const bool upright = style.kind == WizardKind::vertical;
    const float x0 = centre_x(0);
    const float y0 = centre_y(0);
    const float x1 = centre_x(n - 1);
    const float y1 = centre_y(n - 1);
    value = tween::clamp01(value);
    if (theme.style == SurfaceStyle::sketch)
    {
        // A pen line for the track and a marker stroke for what is done.
        list.line(x0, y0, x1, y1, std::max(theme.border, 1.5f), theme.outline);
        if (value > 0.005f)
            list.line(x0, y0, x0 + (x1 - x0) * value, y0 + (y1 - y0) * value, style.track + 3.0f,
                      theme.accent.with_alpha(0.85f));
        return;
    }
    // Framed languages need room for their border around the fill.
    const float inset = chunky(theme) ? theme.border : 0.0f;
    const float across = style.track + 2.0f * inset;
    const Rect well = upright ? Rect{x0 - across * 0.5f, y0, across, y1 - y0}
                              : Rect{x0, y0 - across * 0.5f, x1 - x0, across};
    const float radius = std::min(theme.radius, across * 0.5f);
    paint.well(well, radius, theme.surface_high);
    const Rect filled = upright
                            ? Rect{well.x + inset, well.y, well.w - 2.0f * inset, well.h * value}
                            : Rect{well.x, well.y + inset, well.w * value, well.h - 2.0f * inset};
    if (filled.w < 1.0f || filled.h < 1.0f)
        return;
    if (theme.style == SurfaceStyle::glow)
        list.glow(filled, radius, 10.0f, theme.accent.with_alpha(0.5f));
    list.rounded_rect(filled, inset > 0.0f ? 0.0f : radius, theme.accent);
}

void Wizard::draw_marker(Canvas &canvas, int index) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const WizardStep &step = steps_[static_cast<std::size_t>(index)];
    const float at = position_.value - static_cast<float>(index);
    const float done = tween::clamp01(at);                      // the position has passed it
    const float current = tween::clamp01(1.0f - std::fabs(at)); // the position is on it
    const float active = step.error ? 1.0f : std::max(done, current);
    const Color tone = step.error ? theme.danger : theme.accent;

    const float pop = index == step_ && !style.reduced_motion ? 0.12f * bump_.value : 0.0f;
    const float size = style.marker_size * (1.0f + (style.current_scale - 1.0f) * current + pop);
    const float cx = centre_x(index);
    const float cy = centre_y(index);
    const Rect r{cx - size * 0.5f, cy - size * 0.5f, size, size};
    const bool square = square_language(theme);
    const float radius = square ? std::min(theme.radius, size * 0.3f) : size * 0.5f;

    // A soft light marks the current step where the language has soft light.
    const bool lights = theme.corner == Corner::round && !chunky(theme) &&
                        theme.style != SurfaceStyle::sketch &&
                        theme.style != SurfaceStyle::neumorphic;
    if (lights && current > 0.02f)
        paint.halo(r, radius, 10.0f, tone.with_alpha(0.3f * current));

    Color ink = theme.text_muted;
    if (active < 0.99f)
    {
        list.push_opacity(1.0f - active);
        paint.well(r, radius, theme.surface_high);
        list.pop_opacity();
    }
    if (active > 0.01f)
    {
        list.push_opacity(active);
        const Color on = plate(paint, r, radius, tone);
        list.pop_opacity();
        ink = gfx::mix(theme.text_muted, on, active);
    }

    const float pen = std::max(3.0f, size * 0.09f);
    if (step.error)
    {
        list.line(cx, cy - size * 0.2f, cx, cy + size * 0.05f, pen, ink);
        list.circle(cx, cy + size * 0.22f, pen * 0.62f, ink);
    }
    else
    {
        // The number gives way to a check mark that draws itself: the short
        // stroke first, then the long one.
        const float number = 1.0f - tween::clamp01(done * 3.0f);
        if (number > 0.01f)
        {
            if (style.numbers)
            {
                char text[8];
                std::snprintf(text, sizeof(text), "%d", index + 1);
                paint.label(text, cx, cy + style.number_size * 0.35f, style.number_size,
                            ink.with_alpha(number), gfx::Align::center);
            }
            else if (current > 0.01f)
            {
                list.circle(cx, cy, size * 0.14f, ink.with_alpha(number * current));
            }
        }
        if (done > 0.25f)
        {
            const float t = (done - 0.25f) / 0.75f;
            const float xa = r.x + size * 0.27f, ya = r.y + size * 0.52f;
            const float xb = r.x + size * 0.44f, yb = r.y + size * 0.68f;
            const float xc = r.x + size * 0.74f, yc = r.y + size * 0.34f;
            const float first = tween::clamp01(t * 2.5f);
            const float second = tween::clamp01((t - 0.4f) / 0.6f);
            list.line(xa, ya, xa + (xb - xa) * first, ya + (yb - ya) * first, pen, ink);
            if (second > 0.0f)
                list.line(xb, yb, xb + (xc - xb) * second, yb + (yc - yb) * second, pen, ink);
        }
    }

    // ---- the words ----
    const Color strong = style.on_page ? paint.page_text() : theme.text;
    const Color quiet = style.on_page ? paint.page_text_muted() : theme.text_muted;
    const Color title = gfx::mix(quiet, strong, tween::clamp01(current + 0.45f * done));
    const Color note = step.error ? gfx::mix(theme.danger, strong, 0.15f) : quiet;
    const Rect in = indicator();
    const float reach = style.marker_size * style.current_scale * 0.5f;
    if (style.kind == WizardKind::vertical)
    {
        const float x = cx + reach + style.label_gap;
        const float room = std::max(in.x + in.w - x, 40.0f);
        if (step.caption.empty())
        {
            paint.label(fit_label(paint, step.label, style.label_size, room), x,
                        cy + style.label_size * 0.35f, style.label_size, title);
        }
        else
        {
            paint.label(fit_label(paint, step.label, style.label_size, room), x, cy - 3.0f,
                        style.label_size, title);
            paint.body(fit_body(paint, step.caption, style.caption_size, room), x,
                       cy + style.caption_size + 3.0f, style.caption_size, note);
        }
        return;
    }
    const float room = std::max(in.w / static_cast<float>(count()) - 12.0f, 40.0f);
    const float baseline = cy + reach + style.label_gap + style.label_size * 0.8f;
    paint.label(fit_label(paint, step.label, style.label_size, room), cx, baseline,
                style.label_size, title, gfx::Align::center);
    if (!step.caption.empty())
        paint.body(fit_body(paint, step.caption, style.caption_size, room), cx,
                   baseline + style.caption_size * 1.4f, style.caption_size, note,
                   gfx::Align::center);
}

void Wizard::draw_compact(Canvas &canvas) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const int n = count();
    const Rect in = indicator();
    const Color strong = style.on_page ? paint.page_text() : theme.text;
    const Color quiet = style.on_page ? paint.page_text_muted() : theme.text_muted;

    // "Step 2 of 5", then the step's own name, over a row of dots.
    const float block = style.label_size + 16.0f + style.dot_size;
    const float baseline = in.cy() - block * 0.5f + style.label_size * 0.84f;
    char counter[96];
    if (finished())
        std::snprintf(counter, sizeof(counter), "%s", style.done_text.c_str());
    else
        std::snprintf(counter, sizeof(counter), "%s %d %s %d", style.step_word.c_str(), step_ + 1,
                      style.of_word.c_str(), n);
    const float used = paint.label(fit_label(paint, counter, style.label_size, in.w), in.x,
                                   baseline, style.label_size, finished() ? strong : quiet);
    if (!finished() && in.w - used > 80.0f)
    {
        const WizardStep &step = steps_[static_cast<std::size_t>(step_)];
        paint.label(fit_label(paint, step.label, style.label_size, in.w - used - 18.0f),
                    in.x + used + 18.0f, baseline, style.label_size, strong);
    }

    float x = in.x;
    const float cy = in.cy() + block * 0.5f - style.dot_size * 0.5f;
    for (int i = 0; i < n; ++i)
    {
        const float at = position_.value - static_cast<float>(i);
        const float done = tween::clamp01(at);
        const float current = tween::clamp01(1.0f - std::fabs(at));
        const bool error = steps_[static_cast<std::size_t>(i)].error;
        const float width = tween::lerp(style.dot_size, style.active_width, current);
        const Color tone = error ? theme.danger : theme.accent;
        const Color color =
            gfx::mix(quiet.with_alpha(0.4f), tone, error ? 1.0f : std::max(done, current));
        paint.fill({x, cy - style.dot_size * 0.5f, width, style.dot_size},
                   std::min(theme.radius, style.dot_size * 0.5f), color);
        x += width + style.dot_gap;
    }
}

void Wizard::draw(Canvas &canvas) const
{
    if (steps_.empty())
        return;
    const Theme &theme = style.theme;
    Painter paint(canvas.list, canvas.fonts, theme, canvas.glass);
    if (style.kind == WizardKind::compact)
    {
        draw_compact(canvas);
    }
    else
    {
        const int n = count();
        draw_track(canvas, n > 1 ? position_.value / static_cast<float>(n - 1) : 0.0f);
        for (int i = 0; i < n; ++i)
            draw_marker(canvas, i);
    }
    if (!style.buttons)
        return;

    const int last = count() - 1;
    for (int i = 0; i < 2; ++i)
    {
        const Rect r = button_rect(i);
        Look look;
        look.press = i == pressed_ ? tween::clamp01(press_.value) : 0.0f;
        // A button with nowhere to go is dimmed: disabled is visible.
        look.disabled = i == 0 ? step_ == 0 : finished();
        const std::string &text =
            i == 0 ? style.back_label : (step_ >= last ? style.finish_label : style.next_label);
        paint.button(r, fit_label(paint, text, 24.0f, r.w - 28.0f),
                     i == 0 ? ButtonKind::secondary : ButtonKind::primary, look);
    }
    // One ring that glides between the two buttons.
    HighlightStyle ring;
    ring.kind = HighlightKind::ring;
    highlight_.draw(canvas, style, ring, focus_amount_.value);
}

} // namespace hui::ui
