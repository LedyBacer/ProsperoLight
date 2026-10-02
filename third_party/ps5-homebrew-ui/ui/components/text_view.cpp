// ps5-homebrew-ui - Component: TextView.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/text_view.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <string_view>
#include <utility>

namespace hui::ui
{

using gfx::Color;
using gfx::Rect;

namespace
{

constexpr float kThumbRoom = 18.0f; // kept free on the right of the text for the thumb
constexpr float kCodePadding = 16.0f;
constexpr float kBumpPixels = 10.0f;

bool round_theme(const Theme &theme)
{
    return theme.corner == Corner::round && theme.radius >= 2.0f;
}

// The one colour of a theme that is always opaque enough for a small mark.
Color mark_color(const Theme &theme)
{
    return theme.focus.a > 0.6f ? theme.focus : theme.primary;
}

// Removes whole UTF-8 characters from the end until the line and "..." fit.
template <typename Measure> std::string cut_to(std::string line, float width, Measure measure)
{
    if (measure(line) <= width)
        return line;
    while (!line.empty() && measure(line + "...") > width)
    {
        do
            line.pop_back();
        while (!line.empty() && (static_cast<unsigned char>(line.back()) & 0xc0) == 0x80);
        while (!line.empty() && line.back() == ' ')
            line.pop_back();
    }
    return line + "...";
}

// Greedy word wrap. '\n' forces a break; keep_indent keeps the spaces a line
// starts with (code). A word wider than the column is cut, not split.
template <typename Measure>
void wrap_into(std::vector<std::string> &out, std::string_view text, float width, bool keep_indent,
               Measure measure)
{
    std::size_t at = 0;
    while (at <= text.size())
    {
        std::size_t end = text.find('\n', at);
        if (end == std::string_view::npos)
            end = text.size();
        const std::string_view paragraph = text.substr(at, end - at);
        std::string line;
        std::size_t p = 0;
        if (keep_indent)
        {
            while (p < paragraph.size() && paragraph[p] == ' ')
                ++p;
            line.assign(p, ' ');
        }
        bool has_word = false;
        while (p < paragraph.size())
        {
            if (paragraph[p] == ' ')
            {
                ++p;
                continue;
            }
            std::size_t stop = paragraph.find(' ', p);
            if (stop == std::string_view::npos)
                stop = paragraph.size();
            const std::string word(paragraph.substr(p, stop - p));
            const std::string candidate = has_word ? line + " " + word : line + word;
            if (!has_word || measure(candidate) <= width)
            {
                line = candidate;
            }
            else
            {
                out.push_back(cut_to(line, width, measure));
                line = word;
            }
            has_word = true;
            p = stop;
        }
        out.push_back(cut_to(line, width, measure));
        at = end + 1;
    }
}

bool is_item(TextBlockKind kind)
{
    return kind == TextBlockKind::bullet || kind == TextBlockKind::numbered ||
           kind == TextBlockKind::key_value;
}

// How visible something between two screen heights is. At the top a line
// fades only as the edge cuts it, so a heading the view has just jumped to
// stands at the top in full; at the bottom the text also thins out over a
// band, since that is where the reader is going. An edge where the article
// really ends fades nothing.
struct Fade
{
    float top = 0.0f;
    float bottom = 0.0f;
    float band = 0.0f;  // the bottom band, in pixels; 0 for no fading at all
    bool above = false; // there is more text above the view
    bool below = false;

    float at(float y0, float y1) const
    {
        if (band <= 0.0f)
            return 1.0f;
        const float height = std::max(y1 - y0, 1.0f);
        float alpha = 1.0f;
        if (above)
            alpha = std::min(alpha, (y1 - top) / height);
        if (below)
            alpha = std::min({alpha, (bottom - y0) / height, (bottom - y0) / band});
        return tween::clamp01(alpha);
    }
    float at(float y) const
    {
        return at(y - 12.0f, y + 12.0f);
    }
};

} // namespace

TextBlock TextBlock::heading(std::string text, int level)
{
    TextBlock block;
    block.kind = TextBlockKind::heading;
    block.text = std::move(text);
    block.level = std::clamp(level, 1, 3);
    return block;
}

TextBlock TextBlock::paragraph(std::string text)
{
    TextBlock block;
    block.text = std::move(text);
    return block;
}

TextBlock TextBlock::bullet(std::string text)
{
    TextBlock block;
    block.kind = TextBlockKind::bullet;
    block.text = std::move(text);
    return block;
}

TextBlock TextBlock::numbered(std::string text, std::string marker)
{
    TextBlock block;
    block.kind = TextBlockKind::numbered;
    block.text = std::move(text);
    block.value = std::move(marker);
    return block;
}

TextBlock TextBlock::quote(std::string text)
{
    TextBlock block;
    block.kind = TextBlockKind::quote;
    block.text = std::move(text);
    return block;
}

TextBlock TextBlock::code(std::string text)
{
    TextBlock block;
    block.kind = TextBlockKind::code;
    block.text = std::move(text);
    return block;
}

TextBlock TextBlock::divider()
{
    TextBlock block;
    block.kind = TextBlockKind::divider;
    return block;
}

TextBlock TextBlock::key_value(std::string key, std::string value)
{
    TextBlock block;
    block.kind = TextBlockKind::key_value;
    block.text = std::move(key);
    block.value = std::move(value);
    return block;
}

TextBlock TextBlock::image(std::string caption, float height, int tag)
{
    TextBlock block;
    block.kind = TextBlockKind::image;
    block.text = std::move(caption);
    block.height = height;
    block.tag = tag;
    return block;
}

bool TextView::Key::operator==(const Key &other) const
{
    if (fonts != other.fonts || theme != other.theme)
        return false;
    return std::equal(std::begin(values), std::end(values), std::begin(other.values));
}

TextView::Key TextView::key() const
{
    const Theme &theme = style.theme;
    Key k;
    k.fonts = fonts_;
    k.theme = theme.id;
    const float values[] = {
        bounds_.w,
        bounds_.h,
        style.padding,
        style.max_width,
        style.block_gap,
        style.item_gap,
        style.heading_gap,
        style.indent,
        style.image_height,
        style.footer ? style.footer_height : 0.0f,
        style.toc ? style.toc_width + style.toc_gap : 0.0f,
        style.body_size,
        style.line_height,
        style.heading_size,
        style.subheading_size,
        style.minor_size,
        style.code_size,
        style.caption_size,
        static_cast<float>(style.toc_levels) + (style.heading_rule ? 0.5f : 0.0f) +
            (style.scroll_thumb ? 0.25f : 0.0f),
        // The faces decide every width: a theme edited in place must re-wrap.
        static_cast<float>(static_cast<int>(theme.heading) * 16 + static_cast<int>(theme.label)) +
            (theme.caps ? 0.5f : 0.0f) + theme.tracking * 100.0f,
    };
    static_assert(sizeof(values) == sizeof(k.values));
    std::copy(std::begin(values), std::end(values), std::begin(k.values));
    return k;
}

void TextView::set_content(std::vector<TextBlock> blocks)
{
    blocks_ = std::move(blocks);
    key_ = Key{}; // stale: the next layout wraps again
    lines_.clear();
    placed_.clear();
    headings_.clear();
    height_ = 0.0f;
    seen_serial_ = 0; // new words: there is no place to keep
    scroll_.snap(0.0f);
    toc_mark_set_ = false;
    ensure_layout();
}

void TextView::set_bounds(const Rect &bounds)
{
    bounds_ = bounds;
    ensure_layout();
}

void TextView::layout(const Fonts &fonts)
{
    fonts_ = &fonts;
    ensure_layout();
    sync_scroll();
}

Rect TextView::inner() const
{
    return bounds_.inset(style.padding);
}

Rect TextView::view() const
{
    Rect r = inner();
    if (style.footer)
        r.h -= style.footer_height;
    if (style.toc)
    {
        const float taken = style.toc_width + style.toc_gap;
        r.x += taken;
        r.w -= taken;
    }
    r.w = std::max(r.w, 40.0f);
    r.h = std::max(r.h, 1.0f);
    return r;
}

Rect TextView::column() const
{
    Rect r = view();
    if (style.scroll_thumb)
        r.w -= kThumbRoom;
    if (style.max_width > 0.0f)
        r.w = std::min(r.w, style.max_width);
    r.w = std::max(r.w, 40.0f);
    return r;
}

Rect TextView::toc_rect() const
{
    Rect r = inner();
    if (style.footer)
        r.h -= style.footer_height;
    r.w = style.toc_width;
    return r;
}

void TextView::ensure_layout() const
{
    if (fonts_ == nullptr)
        return;
    const Key now = key();
    if (now == key_)
        return;
    key_ = now;
    build(*fonts_);
}

void TextView::build(const Fonts &fonts) const
{
    lines_.clear();
    placed_.clear();
    headings_.clear();
    scratch_.clear();
    const Painter paint(scratch_, fonts, style.theme, 0);
    const float width = column().w;
    const float body = style.body_size;
    const float body_line = body * style.line_height;
    const auto body_measure = [&](std::string_view s) { return paint.body_width(s, body); };
    // The widest marker a list is likely to carry sets the indent of them all.
    const float number_indent = std::max(style.indent, paint.body_width("00.", body) + 12.0f);

    std::vector<std::string> wrapped;
    float y = 0.0f;
    int number = 0;
    for (std::size_t i = 0; i < blocks_.size(); ++i)
    {
        const TextBlock &block = blocks_[i];
        if (i > 0)
        {
            const TextBlock &before = blocks_[i - 1];
            if (block.kind == TextBlockKind::heading && block.level <= 2)
                y += style.heading_gap;
            else if (is_item(block.kind) && block.kind == before.kind)
                y += style.item_gap;
            else if (before.kind == TextBlockKind::heading)
                y += style.block_gap * 0.7f; // text stays close to its heading
            else
                y += style.block_gap;
        }
        number = block.kind == TextBlockKind::numbered ? number + 1 : 0;

        Placed place;
        place.top = y;
        place.first_line = lines_.size();
        place.number = number;
        wrapped.clear();
        // Lines of one size stacked from the block's top; returns their height.
        const auto stack = [&](float x, float size, float line, Face face, Ink ink, float from)
        {
            for (std::size_t n = 0; n < wrapped.size(); ++n)
            {
                Line out;
                out.text = wrapped[n];
                out.x = x;
                out.baseline = y + from + line * (static_cast<float>(n) + 0.5f) + size * 0.34f;
                out.size = size;
                out.face = face;
                out.ink = ink;
                lines_.push_back(std::move(out));
            }
            return line * static_cast<float>(wrapped.size());
        };

        switch (block.kind)
        {
        case TextBlockKind::heading:
        {
            if (block.level <= style.toc_levels)
                headings_.push_back(static_cast<int>(i));
            if (block.level >= 3)
            {
                const float size = style.minor_size;
                wrap_into(wrapped, block.text, width, false,
                          [&](std::string_view s) { return paint.label_width(s, size); });
                place.height = stack(0.0f, size, size * 1.4f, Face::label, Ink::muted, 0.0f);
                break;
            }
            const float size = block.level == 1 ? style.heading_size : style.subheading_size;
            wrap_into(wrapped, block.text, width, false,
                      [&](std::string_view s) { return paint.heading_width(s, size); });
            place.height = stack(0.0f, size, size * 1.28f, Face::heading, Ink::text, 0.0f);
            if (block.level == 1 && style.heading_rule)
                place.height += 14.0f;
            break;
        }
        case TextBlockKind::paragraph:
            wrap_into(wrapped, block.text, width, false, body_measure);
            place.height = stack(0.0f, body, body_line, Face::body, Ink::text, 0.0f);
            break;
        case TextBlockKind::bullet:
            wrap_into(wrapped, block.text, width - style.indent, false, body_measure);
            place.height = stack(style.indent, body, body_line, Face::body, Ink::text, 0.0f);
            break;
        case TextBlockKind::numbered:
        {
            wrap_into(wrapped, block.text, width - number_indent, false, body_measure);
            place.height = stack(number_indent, body, body_line, Face::body, Ink::text, 0.0f);
            Line marker;
            char counted[16];
            std::snprintf(counted, sizeof(counted), "%d.", number);
            marker.text = block.value.empty() ? std::string(counted) : block.value;
            marker.x = number_indent - 12.0f;
            marker.baseline = y + body_line * 0.5f + body * 0.34f;
            marker.size = body;
            marker.ink = Ink::muted;
            marker.align = gfx::Align::right;
            lines_.push_back(std::move(marker));
            break;
        }
        case TextBlockKind::quote:
            wrap_into(wrapped, block.text, width - style.indent, false, body_measure);
            place.height = stack(style.indent, body, body_line, Face::body, Ink::muted, 0.0f);
            break;
        case TextBlockKind::code:
        {
            const float size = style.code_size;
            wrap_into(wrapped, block.text, width - 2.0f * kCodePadding, true,
                      [&](std::string_view s) { return fonts.mono.measure(s, size); });
            place.height =
                stack(kCodePadding, size, size * 1.4f, Face::mono, Ink::code, kCodePadding) +
                2.0f * kCodePadding;
            break;
        }
        case TextBlockKind::divider:
            place.height = 2.0f;
            break;
        case TextBlockKind::key_value:
        {
            // The value keeps its words; the key gives way.
            const std::string value = cut_to(block.value, width * 0.62f, [&](std::string_view s)
                                             { return paint.label_width(s, body); });
            const float taken = paint.label_width(value, body);
            wrapped.push_back(
                cut_to(block.text, std::max(width - taken - 24.0f, 40.0f), body_measure));
            place.height = stack(0.0f, body, body_line, Face::body, Ink::muted, 0.0f);
            Line right;
            right.text = value;
            right.x = width;
            right.baseline = y + body_line * 0.5f + body * 0.34f;
            right.size = body;
            right.face = Face::label;
            right.align = gfx::Align::right;
            lines_.push_back(std::move(right));
            break;
        }
        case TextBlockKind::image:
        {
            const float picture = block.height > 0.0f ? block.height : style.image_height;
            place.height = picture;
            if (!block.text.empty())
            {
                const float size = style.caption_size;
                wrapped.push_back(cut_to(block.text, width, [&](std::string_view s)
                                         { return paint.body_width(s, size); }));
                place.height +=
                    stack(0.0f, size, size * 1.5f, Face::body, Ink::muted, picture + 6.0f) + 6.0f;
            }
            break;
        }
        }
        place.line_count = lines_.size() - place.first_line;
        y += place.height;
        placed_.push_back(place);
    }
    ++serial_;
    // A little air under the last line, so it does not sit on the edge.
    height_ = blocks_.empty() ? 0.0f : y + 8.0f;
}

float TextView::scroll_limit() const
{
    return std::max(0.0f, height_ - view().h);
}

float TextView::progress() const
{
    const float limit = scroll_limit();
    return limit <= 0.0f ? 1.0f : tween::clamp01(scroll_.value / limit);
}

float TextView::block_offset(int index) const
{
    if (index < 0 || index >= static_cast<int>(placed_.size()))
        return 0.0f;
    return placed_[static_cast<std::size_t>(index)].top;
}

// Remembers what is at the top of the view as a block and a share of it.
void TextView::remember_place()
{
    anchor_block_ = 0;
    anchor_share_ = 0.0f;
    for (std::size_t i = 0; i < placed_.size(); ++i)
    {
        if (placed_[i].top > scroll_.target)
            break;
        anchor_block_ = static_cast<int>(i);
        anchor_share_ = (scroll_.target - placed_[i].top) / std::max(placed_[i].height, 1.0f);
    }
}

// A re-wrap (another theme, another width) changes every height. The reader's
// place is the block that was at the top of the view, not a number of pixels:
// after a re-wrap the same block is put back there.
void TextView::sync_scroll()
{
    ensure_layout();
    const float limit = scroll_limit();
    if (seen_serial_ != serial_)
    {
        if (seen_serial_ != 0 && anchor_block_ < static_cast<int>(placed_.size()))
        {
            const Placed &place = placed_[static_cast<std::size_t>(anchor_block_)];
            // A share past the block's end was in the gap under it.
            scroll_.snap(place.top + std::min(anchor_share_, 1.0f) * place.height);
        }
        seen_serial_ = serial_;
    }
    scroll_.target = std::clamp(scroll_.target, 0.0f, limit);
    scroll_.value = std::clamp(scroll_.value, 0.0f, limit);
    remember_place();
}

int TextView::current_heading() const
{
    if (headings_.empty() || placed_.empty())
        return -1;
    const float limit = scroll_limit();
    const float at = std::clamp(scroll_.value, 0.0f, limit);
    // At the very end the last sections may never reach the top of the view.
    const float probe = limit > 0.0f && at >= limit - 1.0f ? height_ : at + view().h * 0.3f;
    int found = -1;
    for (const int index : headings_)
    {
        if (placed_[static_cast<std::size_t>(index)].top <= probe)
            found = index;
    }
    return found;
}

void TextView::scroll_to(float offset, bool snap)
{
    sync_scroll();
    scroll_.target = std::clamp(offset, 0.0f, scroll_limit());
    if (snap)
        scroll_.snap(scroll_.target);
    remember_place();
}

void TextView::scroll_to_block(int index, bool snap)
{
    ensure_layout();
    if (index < 0 || index >= static_cast<int>(placed_.size()))
        return;
    scroll_to(placed_[static_cast<std::size_t>(index)].top, snap);
}

Event TextView::scroll_by(float delta, const InputFrame *input, Feedback &feedback, audio::Cue cue,
                          Direction direction)
{
    const float limit = scroll_limit();
    const bool at_end = delta > 0.0f ? scroll_.target >= limit - 0.5f : scroll_.target <= 0.5f;
    if (at_end)
    {
        if (direction != Direction::none && style.exits.allows(direction))
        {
            exit_ = direction;
            return Event::none;
        }
        bump_sign_ = delta > 0.0f ? 1.0f : -1.0f;
        const InputFrame fresh;
        return refuse(feedback, style, input != nullptr ? *input : fresh, bump_, bounds_.cx());
    }
    scroll_.target = std::clamp(scroll_.target + delta, 0.0f, limit);
    remember_place();
    const float along = limit > 0.0f ? scroll_.target / limit : 0.0f;
    play_cue(feedback, style, cue, bounds_.cx(),
             style.pitch_by_position ? tween::lerp(1.05f, 0.95f, along) : 1.0f, 0.7f);
    return Event::moved;
}

Event TextView::page(int direction, Feedback &feedback)
{
    exit_ = Direction::none;
    sync_scroll();
    const float delta = view().h * style.page_share * (direction < 0 ? -1.0f : 1.0f);
    return scroll_by(delta, nullptr, feedback, style.sounds.page, Direction::none);
}

Event TextView::next_heading(int direction, Feedback &feedback)
{
    exit_ = Direction::none;
    sync_scroll();
    const float at = scroll_.target;
    float destination = at;
    if (direction >= 0)
    {
        destination = scroll_limit();
        for (const int index : headings_)
        {
            const float top = placed_[static_cast<std::size_t>(index)].top;
            if (top > at + 4.0f)
            {
                destination = std::min(top, destination);
                break;
            }
        }
    }
    else
    {
        destination = 0.0f;
        for (const int index : headings_)
        {
            const float top = placed_[static_cast<std::size_t>(index)].top;
            if (top < at - 4.0f)
                destination = top;
        }
    }
    // The same refusal as a step: at an end there is nowhere to jump to.
    const float delta = destination - at;
    if (std::fabs(delta) < 0.5f)
        return scroll_by(direction >= 0 ? 1.0f : -1.0f, nullptr, feedback, style.sounds.page,
                         Direction::none);
    return scroll_by(delta, nullptr, feedback, style.sounds.page, Direction::none);
}

Event TextView::handle(const InputFrame &input, Feedback &feedback)
{
    exit_ = Direction::none;
    sync_scroll();
    analog_ = input.stick2_y;
    if (input.nav == Direction::up || input.nav == Direction::down)
    {
        const float delta = input.nav == Direction::down ? style.step : -style.step;
        return scroll_by(delta, &input, feedback, style.sounds.move, input.nav);
    }
    if (input.is_pressed(Action::back))
    {
        play_cue(feedback, style, style.sounds.cancel, bounds_.cx());
        return Event::cancelled;
    }
    return Event::none;
}

float TextView::toc_scroll() const
{
    const float total = style.toc_row * static_cast<float>(headings_.size());
    const float room = toc_rect().h;
    if (total <= room)
        return 0.0f;
    // The marker's own glide scrolls the column, so the two never disagree.
    const Rect mark = toc_mark_.rect(0.0f);
    return std::clamp(mark.y + mark.h * 0.5f - room * 0.5f, 0.0f, total - room);
}

void TextView::update(float dt)
{
    sync_scroll();
    if (std::fabs(analog_) > 0.01f)
    {
        // Squared response: fine control near the centre, speed at the rim,
        // and more of it the longer the stick stays pushed.
        hold_ += dt;
        const float boost = 1.0f + (style.stick_boost - 1.0f) *
                                       tween::smoothstep(hold_ / std::max(style.stick_ramp, 0.01f));
        const float delta = analog_ * std::fabs(analog_) * style.stick_speed * boost * dt;
        const float limit = scroll_limit();
        const float target = std::clamp(scroll_.target + delta, 0.0f, limit);
        // The page follows the stick directly; the spring only smooths steps.
        scroll_.value = std::clamp(scroll_.value + (target - scroll_.target), 0.0f, limit);
        scroll_.target = target;
        remember_place();
    }
    else
    {
        hold_ = 0.0f;
    }
    analog_ = 0.0f;
    scroll_.update(dt, std::max(style.omega(), 14.0f));
    active_amount_.target = active_ ? 1.0f : 0.0f;
    active_amount_.update(dt, 18.0f);
    bump_.update(dt, 9.0f);

    const int current = current_heading();
    int row = -1;
    for (std::size_t i = 0; i < headings_.size(); ++i)
    {
        if (headings_[i] == current)
            row = static_cast<int>(i);
    }
    if (row >= 0)
    {
        const Rect target{0.0f, style.toc_row * static_cast<float>(row), style.toc_width,
                          style.toc_row};
        toc_mark_.target(target);
        if (!toc_mark_set_)
            toc_mark_.snap(target);
        toc_mark_set_ = true;
    }
    toc_mark_.update(dt, style);
}

void TextView::draw_toc(Canvas &canvas, Painter &paint) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    const Rect box = toc_rect();
    const float scroll = toc_scroll();
    const Color ink = style.panel ? theme.text : paint.page_text();
    const Color quiet = style.panel ? theme.text_muted : paint.page_text_muted();
    const int current = current_heading();

    list.push_clip({box.x - 4.0f, box.y, box.w + 8.0f, box.h});
    if (toc_mark_set_ && current >= 0)
    {
        HighlightStyle look;
        look.kind = HighlightKind::bar;
        look.breathe = false;
        list.push_transform(1.0f, 0.0f, 0.0f, box.x, box.y - scroll);
        toc_mark_.draw(canvas, style, look, 0.5f + 0.5f * active_amount_.value);
        list.pop_transform();
    }
    const float size = style.caption_size;
    for (std::size_t i = 0; i < headings_.size(); ++i)
    {
        const int index = headings_[i];
        const TextBlock &block = blocks_[static_cast<std::size_t>(index)];
        const float top = box.y + style.toc_row * static_cast<float>(i) - scroll;
        if (top + style.toc_row < box.y || top > box.y + box.h)
            continue;
        const float left = box.x + 16.0f + (block.level > 1 ? 16.0f : 0.0f);
        const float room = std::max(box.x + box.w - 8.0f - left, 20.0f);
        paint.label(fit_label(paint, block.text, size, room), left,
                    top + style.toc_row * 0.5f + size * 0.34f, size,
                    index == current ? ink : quiet);
    }
    list.pop_clip();
    list.rounded_rect({box.x + box.w + style.toc_gap * 0.5f - 0.75f, box.y, 1.5f, box.h}, 0.0f,
                      quiet.with_alpha(0.25f));
}

void TextView::draw_footer(Canvas &canvas, Painter &paint) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    const Rect in = inner();
    const Rect foot{in.x, in.y + in.h - style.footer_height, in.w, style.footer_height};
    const Color ink = style.panel ? theme.text : paint.page_text();
    const Color quiet = style.panel ? theme.text_muted : paint.page_text_muted();

    // A hairline that fills as the reader goes: progress without a number.
    const float line_y = foot.y + 10.0f;
    list.rounded_rect({foot.x, line_y, foot.w, 2.0f}, 0.0f, quiet.with_alpha(0.25f));
    const float read = progress();
    if (read > 0.001f)
        list.rounded_rect({foot.x, line_y - 0.5f, foot.w * read, 3.0f}, 0.0f, mark_color(theme));

    const float size = style.caption_size;
    const float baseline = foot.y + foot.h - 6.0f;
    char percent[8];
    std::snprintf(percent, sizeof(percent), "%d%%", static_cast<int>(std::lround(read * 100.0f)));
    const float taken =
        paint.label(percent, foot.x + foot.w, baseline, size, ink, gfx::Align::right);
    const int current = current_heading();
    if (current >= 0)
        paint.label(fit_label(paint, blocks_[static_cast<std::size_t>(current)].text, size,
                              std::max(foot.w - taken - 24.0f, 40.0f)),
                    foot.x, baseline, size, quiet);
}

void TextView::draw(Canvas &canvas) const
{
    fonts_ = &canvas.fonts;
    ensure_layout();
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);

    const float corner = style.panel
                             ? std::min(theme.radius_card, std::min(bounds_.w, bounds_.h) * 0.5f)
                             : paint.control_radius(bounds_);
    if (style.panel)
        paint.panel(bounds_);
    if (style.focus_ring)
        paint.focus_ring(bounds_, corner, active_amount_.value);
    if (blocks_.empty())
        return;

    const Rect v = view();
    const Rect col = column();
    const float limit = scroll_limit();
    const float offset = std::clamp(scroll_.value, 0.0f, limit);
    const float origin = v.y - offset - bump_sign_ * kBumpPixels * bump_.value;
    const Color ink = style.panel ? theme.text : paint.page_text();
    const Color quiet = style.panel ? theme.text_muted : paint.page_text_muted();
    // Fields are white in the two languages that draw them so; code follows.
    const bool white = theme.style == SurfaceStyle::bevel || theme.style == SurfaceStyle::hard;
    const Color plate = white ? theme.light : theme.surface_high;
    const Color code_ink = white ? Painter::on(plate) : theme.text;

    Fade fade;
    fade.top = v.y;
    fade.bottom = v.y + v.h;
    fade.band = style.edge_fade;
    fade.above = offset > 0.5f;
    fade.below = offset < limit - 0.5f;

    list.push_clip({v.x - 6.0f, v.y, v.w + 12.0f, v.h});
    for (std::size_t i = 0; i < blocks_.size() && i < placed_.size(); ++i)
    {
        const TextBlock &block = blocks_[i];
        const Placed &place = placed_[i];
        const float top = origin + place.top;
        if (top > v.y + v.h)
            break;
        if (top + place.height + style.block_gap < v.y)
            continue;
        const float body_line = style.body_size * style.line_height;

        switch (block.kind)
        {
        case TextBlockKind::heading:
            if (block.level == 1 && style.heading_rule)
            {
                const float y = top + place.height - 5.0f;
                paint.fill({col.x, y, 56.0f, 4.0f}, round_theme(theme) ? 2.0f : 0.0f,
                           mark_color(theme).with_alpha(fade.at(y)));
            }
            break;
        case TextBlockKind::bullet:
        {
            const float cy = top + body_line * 0.5f;
            const Color mark = quiet.with_alpha(fade.at(cy));
            const float cx = col.x + style.indent * 0.45f;
            if (round_theme(theme))
                list.circle(cx, cy, 4.5f, mark);
            else
                list.rounded_rect({cx - 4.0f, cy - 4.0f, 8.0f, 8.0f}, 0.0f, mark);
            break;
        }
        case TextBlockKind::quote:
            paint.fill({col.x + 8.0f, top + 3.0f, 4.0f, std::max(place.height - 6.0f, 4.0f)},
                       round_theme(theme) ? 2.0f : 0.0f,
                       mark_color(theme).with_alpha(0.85f * fade.at(top + place.height * 0.5f)));
            break;
        case TextBlockKind::code:
            paint.well({col.x, top, col.w, place.height}, std::min(theme.radius, 12.0f), plate);
            break;
        case TextBlockKind::divider:
            list.rounded_rect({col.x, top, col.w, 2.0f}, 0.0f,
                              quiet.with_alpha(0.3f * fade.at(top)));
            break;
        case TextBlockKind::key_value:
            if (i + 1 < blocks_.size() && blocks_[i + 1].kind == TextBlockKind::key_value)
            {
                const float y = top + place.height + style.item_gap * 0.5f - 0.75f;
                list.rounded_rect({col.x, y, col.w, 1.5f}, 0.0f,
                                  quiet.with_alpha(0.22f * fade.at(y)));
            }
            break;
        case TextBlockKind::image:
        {
            const float picture = block.height > 0.0f ? block.height : style.image_height;
            const Rect box{col.x, top, col.w, picture};
            if (image)
            {
                image(canvas, box, block, static_cast<int>(i));
            }
            else
            {
                // A placeholder that still reads as "a picture goes here".
                paint.well(box, std::min(theme.radius, 12.0f), theme.surface_high);
                const float s = std::min(box.h * 0.36f, 64.0f);
                const Color mark = theme.text_muted.with_alpha(0.55f);
                list.triangle({box.cx() - s * 1.1f, box.cy() - s * 0.2f, s * 1.3f, s * 0.8f}, mark);
                list.triangle({box.cx() - s * 0.1f, box.cy() - s * 0.5f, s * 1.3f, s * 1.1f}, mark);
                list.circle(box.cx() - s * 0.8f, box.cy() - s * 0.45f, s * 0.17f, mark);
            }
            break;
        }
        default:
            break;
        }

        for (std::size_t n = place.first_line; n < place.first_line + place.line_count; ++n)
        {
            const Line &line = lines_[n];
            const float baseline = origin + line.baseline;
            const float alpha = fade.at(baseline - line.size * 0.9f, baseline + line.size * 0.3f);
            if (alpha <= 0.0f)
                continue;
            Color color = line.ink == Ink::muted ? quiet : (line.ink == Ink::code ? code_ink : ink);
            color = color.with_alpha(alpha);
            const float x = col.x + line.x;
            switch (line.face)
            {
            case Face::heading:
                paint.heading(line.text, x, baseline, line.size, color, line.align);
                break;
            case Face::label:
                paint.label(line.text, x, baseline, line.size, color, line.align);
                break;
            case Face::mono:
                text(list, canvas.fonts.mono, line.text, x, baseline, line.size, color, line.align);
                break;
            case Face::body:
                paint.body(line.text, x, baseline, line.size, color, line.align);
                break;
            }
        }
    }
    list.pop_clip();

    if (style.scroll_thumb && limit > 0.5f)
    {
        const float track = v.h - 16.0f;
        const float size = std::max(track * v.h / height_, 36.0f);
        // Beside the text, wherever a narrow measure ends it.
        const float x = col.x + col.w + kThumbRoom - 5.0f;
        list.rounded_rect({x, v.y + 8.0f, 4.0f, track}, 2.0f, quiet.with_alpha(0.18f));
        list.rounded_rect({x, v.y + 8.0f + (track - size) * (offset / limit), 4.0f, size}, 2.0f,
                          quiet.with_alpha(0.45f + 0.4f * active_amount_.value));
    }
    if (style.toc && !headings_.empty())
        draw_toc(canvas, paint);
    if (style.footer)
        draw_footer(canvas, paint);
}

} // namespace hui::ui
