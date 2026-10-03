// ps5-homebrew-ui - Component: LoadingScreen.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/loading_screen.hpp"

#include "ui/components/overlay.hpp"

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

constexpr int kReadyStage = -2; // the "stage" shown once the load is done
constexpr float kTipLine = 1.45f;

bool round_theme(const Theme &theme)
{
    return theme.corner == Corner::round && theme.radius >= 2.0f;
}

bool framed(const Theme &theme)
{
    return theme.style == SurfaceStyle::hard || theme.style == SurfaceStyle::bevel ||
           theme.style == SurfaceStyle::pixel;
}

// Blurred light suits materials that are soft themselves; on paper, pixels
// or hard ink it would be a foreign body.
bool carries_light(const Theme &theme)
{
    return theme.style == SurfaceStyle::soft || theme.style == SurfaceStyle::glass ||
           theme.style == SurfaceStyle::gloss || theme.style == SurfaceStyle::glow;
}

Color opaque(Color c)
{
    return {c.r, c.g, c.b, 1.0f};
}

// Where everything goes. It depends on the style and the bounds only, so
// update() can place the bar and the spinner that draw() then draws.
struct Layout
{
    Rect bar;     // h == 0: no bar
    Rect spinner; // w == 0: no spinner
    float left = 0.0f;
    float right = 0.0f;
    float title_x = 0.0f;
    float title_baseline = 0.0f;
    float text_width = 0.0f;
    gfx::Align align = gfx::Align::left;
    float stage_x = 0.0f;
    float row_baseline = 0.0f;
    float percent_x = 0.0f;
    float tip_x = 0.0f;
    float tip_width = 0.0f;
    float tip_label = 0.0f; // baseline of the "Tip" label
    float tip_first = 0.0f; // baseline of the tip's first line
    float prompt_x = 0.0f;
    float prompt_cy = 0.0f;
    gfx::Align prompt_align = gfx::Align::right;
};

Layout layout_of(const LoadingScreenStyle &style, const Rect &b)
{
    Layout l;
    const Theme &theme = style.theme;
    const bool has_bar =
        style.indicator == LoadingIndicator::both || style.indicator == LoadingIndicator::bar;
    const bool has_spinner =
        style.indicator == LoadingIndicator::both || style.indicator == LoadingIndicator::spinner;
    const float thick = framed(theme) ? style.bar_height + 2.0f * theme.border : style.bar_height;
    const float s = style.spinner_size;
    l.left = b.x + style.margin;
    l.right = b.x + b.w - style.margin;
    const float room = std::max(l.right - l.left, 80.0f);

    if (style.layout == LoadingLayout::center)
    {
        const float cx = b.cx();
        const float width = std::min(style.bar_width, room);
        l.align = gfx::Align::center;
        l.title_x = cx;
        l.title_baseline = b.y + b.h * 0.36f;
        l.text_width = room;
        const float bar_y = b.y + b.h * 0.5f;
        if (has_bar)
            l.bar = {cx - width * 0.5f, bar_y, width, thick};
        l.row_baseline = bar_y + (has_bar ? thick : 0.0f) + 18.0f + style.stage_size;
        l.stage_x = cx - width * 0.5f;
        l.percent_x = cx + width * 0.5f;
        l.tip_x = cx;
        l.tip_width = std::min(style.tip_width, room);
        l.tip_label = b.y + b.h * 0.67f;
        l.tip_first = l.tip_label + style.tip_size * 1.7f;
        l.prompt_x = cx;
        l.prompt_cy = b.y + b.h - style.bottom;
        l.prompt_align = gfx::Align::center;
    }
    else
    {
        l.title_x = l.left;
        l.title_baseline = b.y + b.h * 0.27f;
        l.text_width = room;
        const float bar_y = b.y + b.h - style.bottom - thick;
        if (has_bar)
            l.bar = {l.left, bar_y, room, thick};
        l.row_baseline = (has_bar ? bar_y : b.y + b.h - style.bottom) - 20.0f;
        l.stage_x = l.left;
        l.percent_x = l.right;
        l.tip_x = l.left;
        l.tip_width = std::min(style.tip_width, room);
        l.tip_first =
            l.row_baseline - style.stage_size - 50.0f -
            static_cast<float>(std::max(style.tip_lines, 1) - 1) * style.tip_size * kTipLine;
        l.tip_label = l.tip_first - style.tip_size * 1.7f;
        l.prompt_x = l.right;
        l.prompt_cy = l.row_baseline - style.stage_size * 0.35f;
        l.prompt_align = gfx::Align::right;
    }
    if (has_spinner)
    {
        l.spinner = {l.stage_x, l.row_baseline - style.stage_size * 0.35f - s * 0.5f, s, s};
        l.stage_x += s + 14.0f;
    }
    return l;
}

} // namespace

void LoadingScreen::set_stages(std::vector<LoadingStage> stages)
{
    stages_ = std::move(stages);
    shown_stage_ = ready_ ? kReadyStage : stage();
    previous_stage_ = shown_stage_;
}

void LoadingScreen::set_tips(std::vector<std::string> tips)
{
    tips_ = std::move(tips);
    tip_ = previous_tip_ = 0;
    tip_clock_ = 0.0f;
    tip_fade_ = {};
    cache_[0] = {};
    cache_[1] = {};
}

void LoadingScreen::set_progress(float value)
{
    progress_ = value < 0.0f ? -1.0f : tween::clamp01(value);
    bar_.set_value(std::max(progress_, 0.0f));
}

void LoadingScreen::set_stage(int index, float share)
{
    if (stages_.empty())
        return;
    index = std::clamp(index, 0, static_cast<int>(stages_.size()) - 1);
    float total = 0.0f;
    float before = 0.0f;
    for (std::size_t i = 0; i < stages_.size(); ++i)
    {
        const float weight = std::max(stages_[i].weight, 0.0f);
        if (static_cast<int>(i) < index)
            before += weight;
        total += weight;
    }
    const float own = std::max(stages_[static_cast<std::size_t>(index)].weight, 0.0f);
    set_progress(total > 0.0f ? (before + own * tween::clamp01(share)) / total : 0.0f);
}

int LoadingScreen::stage() const
{
    if (stages_.empty())
        return -1;
    float total = 0.0f;
    for (const LoadingStage &entry : stages_)
        total += std::max(entry.weight, 0.0f);
    const float at = std::max(progress_, 0.0f) * total;
    float sum = 0.0f;
    for (std::size_t i = 0; i < stages_.size(); ++i)
    {
        sum += std::max(stages_[i].weight, 0.0f);
        if (at < sum)
            return static_cast<int>(i);
    }
    return static_cast<int>(stages_.size()) - 1;
}

void LoadingScreen::set_ready(bool ready)
{
    if (ready == ready_)
        return;
    ready_ = ready;
    announced_ = false;
    if (ready)
    {
        progress_ = 1.0f;
        bar_.set_value(1.0f);
    }
}

void LoadingScreen::show(Feedback &feedback)
{
    open_ = true;
    ready_ = false;
    announced_ = false;
    age_ = 0.0f;
    progress_ = 0.0f;
    bar_.set_value(0.0f, true);
    spinner_.set_spinning(true, true);
    ready_amount_.snap(0.0f);
    refusal_.value = 0.0f;
    tip_clock_ = 0.0f;
    shown_stage_ = previous_stage_ = stage();
    stage_swap_ = {};
    sync_parts();
    play_cue(feedback, style, style.sounds.open);
}

void LoadingScreen::hide()
{
    open_ = false;
}

float LoadingScreen::opacity() const
{
    return tween::smoothstep(fade_);
}

void LoadingScreen::step_tip(int direction)
{
    const int count = static_cast<int>(tips_.size());
    if (count < 2)
        return;
    previous_tip_ = tip_;
    tip_ = (tip_ + (direction < 0 ? -1 : 1) + count) % count;
    tip_direction_ = direction < 0 ? -1.0f : 1.0f;
    tip_fade_.start(style.reduced_motion ? 0.2f : 0.45f);
    tip_clock_ = 0.0f;
}

Event LoadingScreen::handle(const InputFrame &input, Feedback &feedback)
{
    if (!open_)
        return Event::none;
    const float x = bounds_.cx();
    if (ready_ && !announced_)
    {
        // set_ready() has no Feedback to play it with; this is the next
        // place that does.
        announced_ = true;
        play_cue(feedback, style, style.sounds.notify, x);
    }
    if (input.nav == Direction::left || input.nav == Direction::right)
    {
        if (!style.tips_by_hand || tips_.size() < 2)
            return Event::none;
        step_tip(input.nav == Direction::right ? 1 : -1);
        // The tips are a ring, so there is no end to refuse at; the pitch
        // says which one this is.
        play_cue(feedback, style, style.sounds.page, x, 0.94f + 0.03f * static_cast<float>(tip_));
        return Event::moved;
    }
    if (input.is_pressed(Action::confirm))
    {
        if (!ready_)
            return refuse(feedback, style, input, refusal_, x);
        play_cue(feedback, style, style.sounds.activate, x);
        if (style.sounds.rumble > 0.0f)
            feedback.rumble(0.35f * style.sounds.rumble, 0.06f);
        if (style.close_on_continue)
            hide();
        return Event::activated;
    }
    if (input.is_pressed(Action::back))
    {
        play_cue(feedback, style, style.sounds.cancel, x);
        return Event::cancelled;
    }
    return Event::none;
}

// The bar and the spinner are components of their own: they take the screen's
// theme and their place here, so draw() can stay const.
void LoadingScreen::sync_parts()
{
    const Layout l = layout_of(style, bounds_);
    bar_.style.theme = style.theme;
    bar_.style.reduced_motion = style.reduced_motion;
    bar_.style.mode =
        progress_ < 0.0f && !ready_ ? ProgressMode::indeterminate : ProgressMode::determinate;
    bar_.style.height = l.bar.h;
    bar_.style.placement = LabelPlacement::none;
    bar_.style.percent = false;
    bar_.style.sheen = style.sheen && !ready_;
    bar_.set_bounds(l.bar);
    spinner_.style.theme = style.theme;
    spinner_.style.reduced_motion = style.reduced_motion;
    spinner_.style.kind = style.spinner;
    spinner_.set_bounds(l.spinner);
}

void LoadingScreen::update(float dt)
{
    const float rate =
        open_ ? 1.0f / std::max(style.fade_in, 0.01f) : -1.0f / std::max(style.fade_out, 0.01f);
    fade_ = tween::clamp01(fade_ + rate * dt);
    if (!visible())
        return;
    sync_parts();
    age_ += dt;
    bar_.update(dt);
    spinner_.set_spinning(!ready_);
    spinner_.update(dt);

    const int now = ready_ ? kReadyStage : stage();
    if (now != shown_stage_)
    {
        previous_stage_ = shown_stage_;
        shown_stage_ = now;
        stage_swap_.start(style.reduced_motion ? 0.16f : 0.32f);
    }
    stage_swap_.update(dt);

    tip_fade_.update(dt);
    if (open_ && tips_.size() > 1 && style.tip_seconds > 0.0f)
    {
        tip_clock_ += dt;
        if (tip_clock_ >= style.tip_seconds)
            step_tip(1);
    }
    ready_amount_.target = ready_ ? 1.0f : 0.0f;
    ready_amount_.update(dt, std::max(style.omega(), 12.0f));
    refusal_.update(dt, 9.0f);
}

float LoadingScreen::entrance(int order) const
{
    if (style.reduced_motion)
        return tween::cubic_out(age_ / 0.2f);
    return tween::stagger(age_, order, 0.08f, 0.5f);
}

// A tip is wrapped when it first appears, not every frame.
const std::vector<std::string> &LoadingScreen::tip_lines(const Canvas &canvas, const Painter &paint,
                                                         int index, int slot) const
{
    TipCache &cache = cache_[slot];
    const float width = layout_of(style, bounds_).tip_width;
    const int lines = std::max(style.tip_lines, 1);
    if (cache.fonts != &canvas.fonts || cache.theme != style.theme.id || cache.index != index ||
        cache.width != width || cache.size != style.tip_size || cache.lines_max != lines)
    {
        cache.fonts = &canvas.fonts;
        cache.theme = style.theme.id;
        cache.index = index;
        cache.width = width;
        cache.size = style.tip_size;
        cache.lines_max = lines;
        cache.lines =
            wrap_body(paint, tips_[static_cast<std::size_t>(index)], style.tip_size, width, lines);
    }
    return cache.lines;
}

void LoadingScreen::draw_tip(Canvas &canvas, Painter &paint, int index, int slot, float x,
                             float first, float alpha, float dx, gfx::Align align, Color ink) const
{
    if (alpha <= 0.001f || index < 0 || index >= static_cast<int>(tips_.size()))
        return;
    const std::vector<std::string> &lines = tip_lines(canvas, paint, index, slot);
    for (std::size_t i = 0; i < lines.size(); ++i)
        paint.body(lines[i], x + dx, first + static_cast<float>(i) * style.tip_size * kTipLine,
                   style.tip_size, ink.with_alpha(alpha), align);
}

void LoadingScreen::draw_stage(Canvas &canvas, Painter &paint, float x, float baseline,
                               gfx::Align align, Color ink, Color quiet) const
{
    (void)canvas;
    const auto name = [&](int index) -> const std::string &
    {
        static const std::string kNone;
        if (index == kReadyStage)
            return style.ready_text;
        if (index < 0 || index >= static_cast<int>(stages_.size()))
            return kNone;
        return stages_[static_cast<std::size_t>(index)].name;
    };
    const float size = style.stage_size;
    if (stage_swap_.running)
    {
        // The finished stage leaves upward as the next one rises into place.
        const float f = stage_swap_.progress();
        const float travel = style.reduced_motion ? 0.0f : 12.0f;
        paint.label(name(previous_stage_), x, baseline - travel * tween::cubic_in(f), size,
                    quiet.with_alpha(1.0f - tween::smoothstep(f * 2.0f)), align);
        paint.label(name(shown_stage_), x, baseline + travel * (1.0f - tween::cubic_out(f)), size,
                    ink.with_alpha(tween::smoothstep(f * 2.0f - 1.0f)), align);
        return;
    }
    paint.label(name(shown_stage_), x, baseline, size, ink, align);
}

float LoadingScreen::draw_prompt(Canvas &canvas, Painter &paint, float x, float cy,
                                 gfx::Align align, Color ink, bool dark) const
{
    GlyphStyle glyphs = dark ? GlyphStyle::dark() : GlyphStyle::light();
    glyphs.label = ink;
    const float glyph = button_width(style.prompt_button, style.glyph_size);
    const float size = style.prompt_size;
    const float total = glyph + 14.0f + paint.label_width(style.prompt, size);
    const float left = align == gfx::Align::right
                           ? x - total
                           : (align == gfx::Align::center ? x - total * 0.5f : x);
    draw_button(canvas.list, canvas.fonts, glyphs, style.prompt_button, left, cy, style.glyph_size);
    paint.label(style.prompt, left + glyph + 14.0f, cy + size * 0.35f, size, ink);
    return total;
}

void LoadingScreen::draw(Canvas &canvas) const
{
    const float alpha = opacity();
    if (alpha <= 0.001f)
        return;
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const Rect &b = bounds_;
    const Layout l = layout_of(style, b);

    // Everything is text on the tint, so the tint decides the ink.
    const bool custom = style.tint.a > 0.0f;
    const Color tint = opaque(custom ? style.tint : theme.page);
    const Color ink = custom ? Painter::on(tint) : paint.page_text();
    Color quiet = custom ? ink.with_alpha(0.7f) : paint.page_text_muted();
    // Over artwork the quiet colour was not chosen for what is behind it.
    if (art)
        quiet = gfx::mix(quiet, ink, 0.45f);
    const bool dark = custom ? ink.r > 0.5f : theme.dark;
    const bool still = style.reduced_motion;

    list.push_opacity(alpha);
    if (art)
    {
        art(canvas, b, bar_.shown());
        // A veil of the tint, heavier where the words are: all of a centred
        // column, or the left side and the bottom of the corner layout.
        const bool centred = style.layout == LoadingLayout::center;
        const float veil = tween::clamp01(centred ? std::max(style.veil, 0.74f) : style.veil);
        list.rounded_rect(b, 0.0f, tint.with_alpha(veil));
        if (!centred)
            list.gradient_rect_h({b.x, b.y, b.w * 0.62f, b.h}, 0.0f, tint.with_alpha(0.62f),
                                 tint.with_alpha(0.0f));
        list.gradient_rect({b.x, b.y + b.h * 0.4f, b.w, b.h * 0.6f}, 0.0f, tint.with_alpha(0.0f),
                           tint.with_alpha(0.9f));
    }
    else
    {
        list.rounded_rect(b, 0.0f, tint);
        if (style.ambience && carries_light(theme))
        {
            const float drift = still ? 0.0f : canvas.time * 0.11f;
            const float r = b.h * 0.3f;
            const float ax = b.x + b.w * 0.78f + std::sin(drift) * 60.0f;
            const float ay = b.y + b.h * 0.24f + std::cos(drift * 1.3f) * 40.0f;
            list.shadow({ax - r, ay - r, 2.0f * r, 2.0f * r}, r, r * 1.1f,
                        opaque(theme.primary).with_alpha(0.2f));
            const float bx = b.x + b.w * 0.2f + std::cos(drift * 0.8f) * 50.0f;
            const float by = b.y + b.h * 0.86f + std::sin(drift * 1.1f) * 30.0f;
            list.shadow({bx - r, by - r, 2.0f * r, 2.0f * r}, r, r * 1.1f,
                        opaque(theme.accent).with_alpha(0.14f));
        }
    }

    // A part of the screen arriving: it fades in and settles from below.
    const auto begin = [&](int order)
    {
        const float in = entrance(order);
        list.push_opacity(in);
        list.push_transform(1.0f, 0.0f, 0.0f, 0.0f, still ? 0.0f : 22.0f * (1.0f - in));
    };
    const auto end = [&]()
    {
        list.pop_transform();
        list.pop_opacity();
    };

    // ---- title ----
    begin(0);
    if (!title.empty())
    {
        const std::vector<std::string> lines =
            wrap_heading(canvas, theme, title, style.title_size, l.text_width, 1);
        if (!lines.empty())
            paint.heading(lines[0], l.title_x, l.title_baseline, style.title_size, ink, l.align);
    }
    end();
    begin(1);
    if (!subtitle.empty())
        paint.body(fit_body(paint, subtitle, style.subtitle_size, l.text_width), l.title_x,
                   l.title_baseline + style.subtitle_size * 1.9f, style.subtitle_size, quiet,
                   l.align);
    end();

    // ---- the tip: a label, dots, and two texts while one replaces the other ----
    if (!tips_.empty())
    {
        begin(2);
        const int count = static_cast<int>(tips_.size());
        const float label_size = 20.0f;
        float label_x = l.tip_x;
        float dots_x = l.tip_x;
        const float dots_w =
            count > 1 && style.tip_dots ? static_cast<float>(count - 1) * 14.0f + 18.0f : 0.0f;
        const float label_w = paint.label_width(style.tip_label, label_size);
        if (l.align == gfx::Align::center)
        {
            const float total = label_w + (dots_w > 0.0f ? 18.0f + dots_w : 0.0f);
            label_x = l.tip_x - total * 0.5f;
        }
        paint.label(style.tip_label, label_x, l.tip_label, label_size, quiet);
        dots_x = label_x + label_w + 18.0f;
        const float f = tip_fade_.running ? tip_fade_.progress() : 1.0f;
        if (dots_w > 0.0f)
        {
            float x = dots_x;
            const float cy = l.tip_label - label_size * 0.33f;
            for (int i = 0; i < count; ++i)
            {
                // The current dot is a dash; it stretches as the tip arrives.
                float grow = i == tip_ ? f : (i == previous_tip_ ? 1.0f - f : 0.0f);
                if (i == tip_ && i == previous_tip_)
                    grow = 1.0f;
                const float w = 6.0f + 12.0f * grow;
                list.rounded_rect({x, cy - 3.0f, w, 6.0f}, round_theme(theme) ? 3.0f : 0.0f,
                                  ink.with_alpha(0.3f + 0.6f * grow));
                x += w + 8.0f;
            }
        }
        if (tip_fade_.running)
        {
            const float slide = still ? 0.0f : 28.0f * tip_direction_;
            draw_tip(canvas, paint, previous_tip_, 1, l.tip_x, l.tip_first,
                     1.0f - tween::smoothstep(f * 2.0f), -slide * tween::cubic_in(f), l.align, ink);
            draw_tip(canvas, paint, tip_, 0, l.tip_x, l.tip_first,
                     tween::smoothstep(f * 2.0f - 1.0f), slide * (1.0f - tween::cubic_out(f)),
                     l.align, ink);
        }
        else
        {
            draw_tip(canvas, paint, tip_, 0, l.tip_x, l.tip_first, 1.0f, 0.0f, l.align, ink);
        }
        end();
    }

    // ---- the stage, the bar, the percentage and the prompt ----
    begin(3);
    list.push_transform(1.0f, 0.0f, 0.0f, shake(refusal_.value, canvas.time, 8.0f), 0.0f);
    const float ready = tween::clamp01(ready_amount_.value);
    if (l.spinner.w > 0.0f)
        spinner_.draw(canvas);
    // With the spinner gone the name moves back to where the spinner stood.
    const float stage_x =
        l.spinner.w > 0.0f ? tween::lerp(l.stage_x, l.spinner.x, ready) : l.stage_x;
    draw_stage(canvas, paint, stage_x, l.row_baseline, gfx::Align::left, ink, quiet);
    if (l.bar.h > 0.0f)
        bar_.draw(canvas);
    const bool prompt_takes_percent = l.prompt_align == gfx::Align::right;
    if (style.percent && progress_ >= 0.0f && l.bar.h > 0.0f)
    {
        char text[8];
        std::snprintf(text, sizeof(text), "%d%%",
                      static_cast<int>(std::lround(bar_.shown() * 100.0f)));
        const float shown = prompt_takes_percent ? 1.0f - ready : 1.0f;
        if (shown > 0.01f)
            paint.label(text, l.percent_x, l.row_baseline, style.stage_size,
                        quiet.with_alpha(shown), gfx::Align::right);
    }
    list.pop_transform();
    if (ready > 0.01f)
    {
        // The prompt breathes: it is the one thing on the screen that waits.
        const float idle = still ? 1.0f : 0.72f + 0.28f * breathe(canvas.time);
        list.push_opacity(ready * idle);
        list.push_transform(still ? 1.0f : 0.92f + 0.08f * ready, l.prompt_x, l.prompt_cy, 0.0f,
                            0.0f);
        draw_prompt(canvas, paint, l.prompt_x, l.prompt_cy, l.prompt_align, ink, dark);
        list.pop_transform();
        list.pop_opacity();
    }
    end();
    list.pop_opacity();
}

} // namespace hui::ui
