// ps5-homebrew-ui - Component: Accordion.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/accordion.hpp"

#include "ui/components/overlay.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace hui::ui
{

using gfx::Color;
using gfx::Rect;

namespace
{

constexpr float kChevronRoom = 30.0f; // the room a chevron takes in a header

// A chevron pointing right at angle 0, turned clockwise about its centre.
void draw_chevron(gfx::DrawList &list, float cx, float cy, float half, float angle, float width,
                  Color color)
{
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    const auto px = [&](float x, float y) { return cx + x * c - y * s; };
    const auto py = [&](float x, float y) { return cy + x * s + y * c; };
    const float reach = half * 0.55f;
    list.line(px(-reach, -half), py(-reach, -half), px(reach, 0.0f), py(reach, 0.0f), width, color);
    list.line(px(reach, 0.0f), py(reach, 0.0f), px(-reach, half), py(-reach, half), width, color);
}

} // namespace

void Accordion::set_sections(std::vector<AccordionSection> sections)
{
    sections_ = std::move(sections);
    open_.assign(sections_.size(), false);
    amounts_.assign(sections_.size(), tween::Bounce{});
    measured_.assign(sections_.size(), -1.0f);
    focus_ = std::clamp(focus_, 0, std::max(static_cast<int>(sections_.size()) - 1, 0));
    retarget(true);
    place_highlight();
}

void Accordion::set_bounds(const Rect &bounds)
{
    bounds_ = bounds;
    retarget(true);
    place_highlight();
}

void Accordion::measure(const Fonts &fonts)
{
    // Measuring needs a painter and a painter needs a list; nothing is drawn.
    gfx::DrawList scratch;
    const Painter paint(scratch, fonts, style.theme, 0);
    const float width = std::max(inner().w - 2.0f * style.padding, 40.0f);
    measured_.assign(sections_.size(), -1.0f);
    for (std::size_t i = 0; i < sections_.size(); ++i)
    {
        if (sections_[i].body.empty())
            continue;
        const std::size_t lines =
            wrap_body(paint, sections_[i].body, style.body_size, width, style.body_lines).size();
        measured_[i] = static_cast<float>(lines) * style.body_size * style.body_line +
                       2.0f * style.body_padding;
    }
    retarget(true);
    place_highlight();
}

void Accordion::set_focus(int index, bool snap)
{
    if (sections_.empty())
        return;
    const int next = std::clamp(index, 0, static_cast<int>(sections_.size()) - 1);
    if (!snap)
        glide_.value += top(focus_, false) - top(next, false);
    focus_ = next;
    retarget(snap);
    place_highlight();
}

bool Accordion::is_open(int index) const
{
    return index >= 0 && index < static_cast<int>(open_.size()) &&
           open_[static_cast<std::size_t>(index)];
}

void Accordion::set_open(int index, bool open, bool snap)
{
    if (index < 0 || index >= static_cast<int>(sections_.size()))
        return;
    for (std::size_t i = 0; i < open_.size(); ++i)
    {
        if (static_cast<int>(i) == index)
            open_[i] = open;
        else if (open && style.single)
            open_[i] = false;
        if (snap)
            amounts_[i].snap(open_[i] ? 1.0f : 0.0f);
    }
    retarget(snap);
    place_highlight();
}

void Accordion::enter()
{
    age_ = 0.0f;
}

Rect Accordion::inner() const
{
    return style.panel ? bounds_.inset(style.panel_padding) : bounds_;
}

float Accordion::content_height(int index) const
{
    const AccordionSection &section = sections_[static_cast<std::size_t>(index)];
    if (section.height > 0.0f)
        return section.height;
    if (section.body.empty())
        return 0.0f;
    const float known = measured_[static_cast<std::size_t>(index)];
    if (known >= 0.0f)
        return known;
    // Not measured yet: assume letters about half as wide as they are tall.
    const float width = std::max(inner().w - 2.0f * style.padding, 40.0f);
    const float per_line = std::max(width / (style.body_size * 0.5f), 1.0f);
    const float lines = std::clamp(std::ceil(static_cast<float>(section.body.size()) / per_line),
                                   1.0f, static_cast<float>(std::max(style.body_lines, 1)));
    return lines * style.body_size * style.body_line + 2.0f * style.body_padding;
}

// A bouncy theme lets a section swing a little past open; it never goes
// below closed, where the header would cover its neighbour.
float Accordion::amount(int index) const
{
    return std::max(amounts_[static_cast<std::size_t>(index)].value, 0.0f);
}

// Where a section starts in content space: as it is now, or as it will be
// once every spring has settled (the scroll aims for that).
float Accordion::top(int index, bool settled) const
{
    float y = 0.0f;
    for (int i = 0; i < index; ++i)
    {
        const float open = settled ? (open_[static_cast<std::size_t>(i)] ? 1.0f : 0.0f) : amount(i);
        y += style.header_height + open * content_height(i) + style.gap;
    }
    return y;
}

float Accordion::total(bool settled) const
{
    if (sections_.empty())
        return 0.0f;
    return top(static_cast<int>(sections_.size()), settled) - style.gap;
}

Rect Accordion::header_rect(int index) const
{
    const Rect in = inner();
    return {in.x, in.y + top(index, false) - scroll_.offset(), in.w, style.header_height};
}

Rect Accordion::content_rect(int index) const
{
    const Rect header = header_rect(index);
    return {header.x + style.padding, header.y + header.h + style.body_padding,
            header.w - 2.0f * style.padding,
            std::max(content_height(index) - 2.0f * style.body_padding, 0.0f)};
}

void Accordion::retarget(bool snap)
{
    if (sections_.empty())
        return;
    const Rect in = inner();
    const float start = top(focus_, true);
    const float full = style.header_height +
                       (open_[static_cast<std::size_t>(focus_)] ? content_height(focus_) : 0.0f);
    // Show the focused section whole when it fits; its header always.
    const float shown = std::min(full, std::max(in.h - style.header_height * 0.5f, 1.0f));
    scroll_.reveal(start, start + shown, in.h, style.header_height * 0.25f, total(true));
    if (snap)
    {
        scroll_.position.snap(scroll_.position.target);
        glide_.snap(0.0f);
    }
}

void Accordion::place_highlight()
{
    if (sections_.empty())
        return;
    highlight_.snap({0.0f, top(focus_, false) + glide_.value, inner().w, style.header_height});
}

Event Accordion::toggle(const InputFrame &input, Feedback &feedback)
{
    const float x = bounds_.cx();
    const std::size_t at = static_cast<std::size_t>(focus_);
    if (sections_[at].disabled || content_height(focus_) <= 0.0f)
        return refuse(feedback, style, input, highlight_.refusal(), x);
    const bool opens = !open_[at];
    if (opens && style.single)
        open_.assign(open_.size(), false);
    open_[at] = opens;
    press_.trigger();
    retarget(false);
    play_cue(feedback, style, style.sounds.change, x, opens ? 1.04f : 0.96f);
    return Event::changed;
}

Event Accordion::handle(const InputFrame &input, Feedback &feedback)
{
    exit_ = Direction::none;
    if (sections_.empty())
        return Event::none;
    const float x = bounds_.cx();
    const int count = static_cast<int>(sections_.size());
    if (input.nav == Direction::up || input.nav == Direction::down)
    {
        int next = focus_ + (input.nav == Direction::down ? 1 : -1);
        if (next < 0 || next >= count)
        {
            // An edge that is an exit hands the focus over instead of wrapping.
            if (style.exits.allows(input.nav))
            {
                exit_ = input.nav;
                return Event::none;
            }
            if (!style.wrap || input.nav_repeat || count < 2)
                return refuse(feedback, style, input, highlight_.refusal(), x);
            next = next < 0 ? count - 1 : 0;
        }
        glide_.value += top(focus_, false) - top(next, false);
        focus_ = next;
        retarget(false);
        const float along =
            count > 1 ? static_cast<float>(focus_) / static_cast<float>(count - 1) : 0.0f;
        play_cue(feedback, style, style.sounds.move, x,
                 style.pitch_by_position ? tween::lerp(1.05f, 0.95f, along) : 1.0f);
        return Event::moved;
    }
    if (input.nav == Direction::left || input.nav == Direction::right)
    {
        // Right opens, left closes; with nothing to do, the edge may be an exit.
        const std::size_t at = static_cast<std::size_t>(focus_);
        const bool can = !sections_[at].disabled && content_height(focus_) > 0.0f;
        if (can && open_[at] != (input.nav == Direction::right))
            return toggle(input, feedback);
        if (style.exits.allows(input.nav))
        {
            exit_ = input.nav;
            return Event::none;
        }
        return refuse(feedback, style, input, highlight_.refusal(), x);
    }
    if (input.is_pressed(Action::confirm))
        return toggle(input, feedback);
    if (input.is_pressed(Action::back))
    {
        play_cue(feedback, style, style.sounds.cancel, x);
        return Event::cancelled;
    }
    return Event::none;
}

void Accordion::update(float dt)
{
    age_ += dt;
    const float omega = std::max(style.omega(), 12.0f);
    const float damping = std::max(style.damping(), 0.75f);
    for (std::size_t i = 0; i < amounts_.size(); ++i)
    {
        amounts_[i].target = open_[i] ? 1.0f : 0.0f;
        amounts_[i].update(dt, omega, damping);
    }
    glide_.target = 0.0f;
    glide_.update(dt, std::max(style.omega(), 18.0f), std::max(style.damping(), 0.78f));
    highlight_.update(dt, style);
    place_highlight();
    // A section that closed may have left the scroll past the new end.
    const float limit = std::max(total(true) - inner().h, 0.0f);
    scroll_.position.target = std::min(scroll_.position.target, limit);
    scroll_.update(dt, std::max(style.omega(), 14.0f));
    active_amount_.target = active_ ? 1.0f : 0.0f;
    active_amount_.update(dt, 18.0f);
    press_.update(dt, 10.0f);
}

void Accordion::draw(Canvas &canvas) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    if (style.panel)
        paint.panel(bounds_);
    if (sections_.empty())
        return;

    const Rect in = inner();
    const float scroll = scroll_.offset();
    const float height = total(false);
    const bool overflow = std::max(height, total(true)) > in.h + 0.5f;
    const float bleed = style.highlight.kind == HighlightKind::glow ? 30.0f : 12.0f;
    const float above = overflow ? std::min(bleed, 9.0f) : bleed;
    list.push_clip({in.x - bleed, in.y - above, in.w + 2.0f * bleed, in.h + 2.0f * above});

    const int count = static_cast<int>(sections_.size());
    const bool on_surface = style.cards || style.panel;
    const Color strong = on_surface ? theme.text : paint.page_text();
    const Color muted = on_surface ? theme.text_muted : paint.page_text_muted();
    const auto entrance = [&](int index)
    {
        if (style.entrance_step <= 0.0f || style.reduced_motion)
            return tween::cubic_out(age_ / 0.2f);
        return tween::stagger(age_, index, style.entrance_step, 0.32f);
    };
    // How visible a section is: 1 inside the view, fading once less than a
    // header's worth of it is left at a clip edge.
    const auto visibility = [&](const Rect &section)
    {
        if (!overflow || style.edge_fade <= 0.0f)
            return 1.0f;
        const float shown = std::min(section.y + section.h - in.y, in.y + in.h - section.y);
        return tween::clamp01(shown / (style.header_height * style.edge_fade));
    };
    const auto section_rect = [&](int index)
    {
        return Rect{in.x, in.y + top(index, false) - scroll, in.w,
                    style.header_height + amount(index) * content_height(index)};
    };
    const float radius = std::min(theme.radius, style.header_height * 0.5f);

    if (style.cards || style.dividers)
    {
        for (int i = 0; i < count; ++i)
        {
            const Rect box = section_rect(i);
            const float alpha = visibility(box) * entrance(i);
            if (alpha <= 0.0f || box.y > in.y + in.h || box.y + box.h < in.y)
                continue;
            list.push_opacity(alpha);
            if (style.cards)
                paint.surface(box, radius, theme.surface_high, theme.outline, 1.0f);
            else if (i + 1 < count)
                list.rounded_rect({box.x + style.padding, box.y + box.h + style.gap * 0.5f - 0.75f,
                                   box.w - 2.0f * style.padding, 1.5f},
                                  0.0f, muted.with_alpha(0.22f));
            list.pop_opacity();
        }
    }

    // The highlight is kept in content space; bring it to the screen here.
    list.push_transform(1.0f, 0.0f, 0.0f, in.x, in.y - scroll);
    {
        HighlightStyle look = style.highlight;
        look.grow -= 2.0f * press_.value; // a press pushes it in for a moment
        highlight_.draw(canvas, style, look,
                        (0.35f + 0.65f * active_amount_.value) * entrance(focus_));
    }
    list.pop_transform();

    for (int i = 0; i < count; ++i)
    {
        const AccordionSection &section = sections_[static_cast<std::size_t>(i)];
        const Rect box = section_rect(i);
        const float arrive = entrance(i);
        const float alpha = visibility(box) * arrive;
        if (alpha <= 0.0f || box.y > in.y + in.h || box.y + box.h < in.y)
            continue;
        list.push_opacity(alpha);
        list.push_transform(1.0f, 0.0f, 0.0f, 0.0f,
                            style.reduced_motion ? 0.0f : 18.0f * (1.0f - arrive));

        // ---- the header ----
        const Rect header{box.x, box.y, box.w, style.header_height};
        const float under = highlight_.coverage({0.0f, top(i, false), in.w, header.h});
        const float focus = under * active_amount_.value;
        Color ink = Highlight::text_color(style, style.highlight, focus, strong);
        // On a plate in the primary colour the quiet text follows the ink
        // all the way, also while the accordion is inactive and the plate
        // is faint: the muted colour would not read on either.
        Color quiet = style.highlight.kind == HighlightKind::fill
                          ? gfx::mix(muted, ink.with_alpha(0.8f), under)
                          : gfx::mix(muted, ink, focus * 0.6f);
        if (section.disabled)
        {
            ink = ink.with_alpha(0.42f);
            quiet = quiet.with_alpha(0.42f);
        }
        const float open = tween::clamp01(amount(i));
        float left = header.x + style.padding;
        float right = header.x + header.w - style.padding;
        if (style.chevron == AccordionChevron::leading)
        {
            draw_chevron(list, left + 8.0f, header.cy(), 8.0f, open * 1.5707963f, 2.5f, quiet);
            left += kChevronRoom;
        }
        else if (style.chevron == AccordionChevron::trailing)
        {
            // Down while closed; it turns half a circle to point up.
            draw_chevron(list, right - 8.0f, header.cy(), 8.0f, (0.5f + open) * 3.14159265f, 2.5f,
                         quiet);
            right -= kChevronRoom;
        }
        if (!section.value.empty())
        {
            right -= paint.label(section.value, right, header.cy() + style.value_size * 0.34f,
                                 style.value_size, quiet, gfx::Align::right);
            right -= 18.0f;
        }
        paint.label(
            fit_label(paint, section.title, style.title_size, std::max(right - left, 40.0f)), left,
            header.cy() + style.title_size * 0.34f, style.title_size, ink);

        // ---- the content: laid out at full height, revealed by a clip ----
        const float full = content_height(i);
        if (open > 0.01f && full > 0.0f)
        {
            const float reveal = tween::clamp01((open - 0.2f) / 0.6f);
            if (style.cards)
                list.rounded_rect({header.x + style.padding, header.y + header.h - 0.75f,
                                   header.w - 2.0f * style.padding, 1.5f},
                                  0.0f, theme.text_muted.with_alpha(0.2f * open));
            list.push_clip({box.x, header.y + header.h, box.w, amount(i) * full});
            list.push_opacity(reveal);
            const Rect area{box.x + style.padding, header.y + header.h + style.body_padding,
                            box.w - 2.0f * style.padding,
                            std::max(full - 2.0f * style.body_padding, 0.0f)};
            if (content && section.body.empty())
            {
                content(canvas, area, section, i, reveal * alpha);
            }
            else
            {
                const std::vector<std::string> lines =
                    wrap_body(paint, section.body, style.body_size, area.w, style.body_lines);
                float baseline = area.y + style.body_size * 0.95f;
                for (const std::string &line : lines)
                {
                    paint.body(line, area.x, baseline, style.body_size, strong.with_alpha(0.86f));
                    baseline += style.body_size * style.body_line;
                }
            }
            list.pop_opacity();
            list.pop_clip();
        }
        list.pop_transform();
        list.pop_opacity();
    }
    list.pop_clip();

    if (style.scroll_thumb && overflow)
    {
        const float span = std::max(height, in.h + 1.0f);
        const float track = in.h - 16.0f;
        const float size = std::max(track * in.h / span, 36.0f);
        const float at = scroll / std::max(span - in.h, 1.0f);
        const float x = in.x + in.w + 8.0f;
        list.rounded_rect({x, in.y + 8.0f, 4.0f, track}, 2.0f, muted.with_alpha(0.16f));
        list.rounded_rect({x, in.y + 8.0f + (track - size) * tween::clamp01(at), 4.0f, size}, 2.0f,
                          muted.with_alpha(0.7f));
    }
}

} // namespace hui::ui
