// ps5-homebrew-ui - Component: MediaControls.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/media_controls.hpp"

#include "ui/components/overlay.hpp"
#include "ui/components/progress.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string_view>
#include <utility>

namespace hui::ui
{

using gfx::Color;
using gfx::Rect;

namespace
{

constexpr float kQuarter = 1.5707963f;
// The white thumb every web slider has (Painter::slider draws the same one).
const Color kThumb{1.0f, 1.0f, 1.0f, 1.0f};

bool round_theme(const Theme &theme)
{
    return theme.corner == Corner::round && theme.radius >= 2.0f;
}

// Themes whose wells have a thick frame: a bar needs room inside it.
bool framed(const Theme &theme)
{
    return theme.style == SurfaceStyle::hard || theme.style == SurfaceStyle::bevel ||
           theme.style == SurfaceStyle::pixel;
}

Color opaque(Color c)
{
    return {c.r, c.g, c.b, 1.0f};
}

void format_time(char *out, std::size_t size, float seconds, bool hours)
{
    const int whole = std::max(0, static_cast<int>(seconds));
    if (hours)
        std::snprintf(out, size, "%d:%02d:%02d", whole / 3600, (whole / 60) % 60, whole % 60);
    else
        std::snprintf(out, size, "%d:%02d", whole / 60, whole % 60);
}

// ---- tabular digits ----
// A running clock must not jitter as its digits change width, and the theme's
// label face is rarely monospaced: every digit is set in a cell as wide as the
// widest one, so the text keeps the theme's face and still stands still.

float digit_cell(const Painter &paint, float size)
{
    float widest = 0.0f;
    for (char c = '0'; c <= '9'; ++c)
        widest = std::max(widest, paint.label_width(std::string_view(&c, 1), size));
    return widest;
}

float tabular_width(const Painter &paint, std::string_view text, float size)
{
    const float cell = digit_cell(paint, size);
    float width = 0.0f;
    for (const char &c : text)
        width += c >= '0' && c <= '9' ? cell : paint.label_width(std::string_view(&c, 1), size);
    return width;
}

float draw_tabular(Painter &paint, std::string_view text, float x, float baseline, float size,
                   Color color, gfx::Align align = gfx::Align::left)
{
    const float width = tabular_width(paint, text, size);
    const float cell = digit_cell(paint, size);
    float at = align == gfx::Align::right ? x - width
                                          : (align == gfx::Align::center ? x - width * 0.5f : x);
    for (const char &c : text)
    {
        const std::string_view one(&c, 1);
        if (c >= '0' && c <= '9')
        {
            paint.label(one, at + cell * 0.5f, baseline, size, color, gfx::Align::center);
            at += cell;
        }
        else
        {
            at += paint.label(one, at, baseline, size, color);
        }
    }
    return width;
}

// ---- icons, drawn from shapes ----
// k scales an icon designed for a 56 pixel button. Triangles are single
// shapes, so they can fade; the stroked icons are drawn in opaque ink because
// overlapping strokes would show through each other.

void icon_triangle(gfx::DrawList &list, float cx, float cy, float size, float dir, Color c)
{
    list.triangle({cx - size * 0.5f, cy - size * 0.5f, size, size}, c, 0.0f, kQuarter * dir);
}

void icon_pause(gfx::DrawList &list, float cx, float cy, float k, bool round, Color c)
{
    const float corner = round ? 2.5f * k : 0.0f;
    list.rounded_rect({cx - 9.5f * k, cy - 11.0f * k, 7.0f * k, 22.0f * k}, corner, c);
    list.rounded_rect({cx + 2.5f * k, cy - 11.0f * k, 7.0f * k, 22.0f * k}, corner, c);
}

void icon_track(gfx::DrawList &list, float cx, float cy, float k, float dir, bool round, Color c)
{
    icon_triangle(list, cx - dir * 3.0f * k, cy, 18.0f * k, dir, c);
    list.rounded_rect({cx + dir * 8.0f * k - 2.0f * k, cy - 9.0f * k, 4.0f * k, 18.0f * k},
                      round ? 2.0f * k : 0.0f, c);
}

void icon_skip(gfx::DrawList &list, float cx, float cy, float k, float dir, Color c)
{
    icon_triangle(list, cx - dir * 6.5f * k, cy, 15.0f * k, dir, c);
    icon_triangle(list, cx + dir * 6.5f * k, cy, 15.0f * k, dir, c);
}

void icon_shuffle(gfx::DrawList &list, float cx, float cy, float k, Color c)
{
    // Two paths that cross, each ending in an arrow head on the right.
    const float t = 3.2f * k;
    for (int i = 0; i < 2; ++i)
    {
        const float s = i == 0 ? 1.0f : -1.0f;
        const float y0 = cy - 7.0f * k * s;
        const float y1 = cy + 7.0f * k * s;
        list.line(cx - 13.0f * k, y0, cx - 6.0f * k, y0, t, c);
        list.line(cx - 6.0f * k, y0, cx + 4.0f * k, y1, t, c);
        list.line(cx + 4.0f * k, y1, cx + 12.0f * k, y1, t, c);
        list.line(cx + 12.0f * k, y1, cx + 8.0f * k, y1 - 4.5f * k, t, c);
        list.line(cx + 12.0f * k, y1, cx + 8.0f * k, y1 + 4.5f * k, t, c);
    }
}

void icon_repeat(gfx::DrawList &list, float cx, float cy, float k, bool one, Color c)
{
    // Two arcs chasing each other round a circle, each ending in an arrow
    // head laid along the tangent at its tip.
    const float t = 3.2f * k;
    const float radius = 11.0f * k; // to the middle of the stroke
    const float barb = 5.0f * k;
    constexpr float kSweep = 2.25f;
    for (int i = 0; i < 2; ++i)
    {
        const float start = -0.95f + 3.1415927f * static_cast<float>(i);
        list.arc(cx, cy, radius + t * 0.5f, t, start, kSweep, c);
        const float a = start + kSweep;
        // Angles run clockwise from 12 o'clock: n points outward, d along the arc.
        const float nx = std::sin(a);
        const float ny = -std::cos(a);
        const float dx = std::cos(a);
        const float dy = std::sin(a);
        const float px = cx + nx * radius + dx * 2.0f * k;
        const float py = cy + ny * radius + dy * 2.0f * k;
        list.line(px, py, px - (dx + nx) * barb, py - (dy + ny) * barb, t, c);
        list.line(px, py, px - (dx - nx) * barb, py - (dy - ny) * barb, t, c);
    }
    if (one)
    {
        // "Repeat one": a 1 in the middle of the loop.
        list.line(cx + 0.5f * k, cy - 4.5f * k, cx + 0.5f * k, cy + 5.0f * k, 2.6f * k, c);
        list.line(cx - 2.5f * k, cy - 2.5f * k, cx + 0.5f * k, cy - 4.5f * k, 2.6f * k, c);
    }
}

void icon_volume(gfx::DrawList &list, float cx, float cy, float k, float level, bool muted,
                 bool round, Color c)
{
    // The speaker: a box and a cone opening to the right.
    list.rounded_rect({cx - 13.0f * k, cy - 4.5f * k, 7.0f * k, 9.0f * k}, round ? 1.5f * k : 0.0f,
                      c);
    icon_triangle(list, cx - 6.0f * k, cy, 18.0f * k, -1.0f, c);
    const float t = 2.8f * k;
    if (muted || level <= 0.001f)
    {
        list.line(cx + 5.0f * k, cy - 4.5f * k, cx + 13.0f * k, cy + 4.5f * k, t, c);
        list.line(cx + 13.0f * k, cy - 4.5f * k, cx + 5.0f * k, cy + 4.5f * k, t, c);
        return;
    }
    // One wave for a low level, two for a high one.
    list.arc(cx + 1.0f * k, cy, 7.0f * k, t, 0.75f, 1.64f, c);
    if (level > 0.5f)
        list.arc(cx + 1.0f * k, cy, 12.5f * k, t, 0.75f, 1.64f, c);
}

} // namespace

// ---- state ------------------------------------------------------------------

void MediaControls::set_bounds(const Rect &bounds)
{
    bounds_ = bounds;
    ring_set_ = false; // the ring is placed afresh, not flown in from the old layout
}

void MediaControls::set_duration(float seconds)
{
    duration_ = std::max(seconds, 0.0f);
    position_ = std::min(position_, duration_);
    buffered_ = std::min(buffered_, duration_);
}

void MediaControls::set_position(float seconds, bool snap)
{
    position_ = std::clamp(seconds, 0.0f, duration_);
    if (snap)
        shown_.snap(duration_ > 0.0f ? position_ / duration_ : 0.0f);
}

void MediaControls::set_buffered(float seconds)
{
    buffered_ = std::clamp(seconds, 0.0f, duration_);
}

void MediaControls::set_chapters(std::vector<MediaChapter> chapters)
{
    chapters_ = std::move(chapters);
}

void MediaControls::set_volume(float level, bool muted)
{
    volume_ = tween::clamp01(level);
    muted_ = muted;
}

int MediaControls::chapter_at(float seconds) const
{
    int found = -1;
    for (std::size_t i = 0; i < chapters_.size(); ++i)
    {
        if (chapters_[i].time <= seconds + 0.001f)
            found = static_cast<int>(i);
    }
    return found;
}

void MediaControls::set_focus(MediaControl control)
{
    focus_ = shown(control) ? control : MediaControl::play;
    if (focus_ != MediaControl::scrubber)
        row_focus_ = focus_;
    adjusting_ = false;
}

bool MediaControls::hidden() const
{
    return style.auto_hide > 0.0f && idle_ >= style.auto_hide;
}

bool MediaControls::shown(MediaControl control) const
{
    switch (control)
    {
    case MediaControl::shuffle:
        return style.show_shuffle;
    case MediaControl::repeat:
        return style.show_repeat;
    case MediaControl::previous:
    case MediaControl::next:
        return style.show_tracks;
    case MediaControl::rewind:
    case MediaControl::forward:
        return style.show_skip;
    case MediaControl::volume:
        return style.show_volume;
    case MediaControl::play:
    case MediaControl::scrubber:
        break;
    }
    return true;
}

// The buttons from left to right, as the layout places them.
int MediaControls::stops(MediaControl *out) const
{
    constexpr MediaControl kFull[kButtons] = {
        MediaControl::shuffle, MediaControl::previous, MediaControl::rewind, MediaControl::play,
        MediaControl::forward, MediaControl::next,     MediaControl::repeat, MediaControl::volume,
    };
    constexpr MediaControl kCompact[kButtons] = {
        MediaControl::previous, MediaControl::rewind,  MediaControl::play,   MediaControl::forward,
        MediaControl::next,     MediaControl::shuffle, MediaControl::repeat, MediaControl::volume,
    };
    const MediaControl *order = style.layout == MediaLayout::compact ? kCompact : kFull;
    int count = 0;
    for (int i = 0; i < kButtons; ++i)
    {
        if (shown(order[i]))
            out[count++] = order[i];
    }
    return count;
}

float MediaControls::track_thickness() const
{
    return framed(style.theme) ? style.track_height + 2.0f * style.theme.border
                               : style.track_height;
}

float MediaControls::preferred_height() const
{
    const float row = std::max(style.button_size, style.play_size);
    if (style.layout == MediaLayout::compact)
        return style.padding * 1.2f + style.thumb_size + 6.0f + row;
    return 2.0f * style.padding + style.title_size * 1.2f + style.artist_size * 1.35f + 18.0f +
           std::max(style.thumb_size, style.time_size * 1.3f) + 18.0f + row;
}

MediaControls::Geometry MediaControls::geometry() const
{
    Geometry g;
    const float size = style.button_size;
    const float play = style.play_size;
    const float gap = style.button_gap;
    const float row = std::max(size, play);
    const float thick = track_thickness();
    const auto width_of = [&](MediaControl control)
    { return control == MediaControl::play ? play : size; };
    // Lays `count` buttons out from x, centred on cy; returns where they end.
    const auto place = [&](const MediaControl *order, int count, float x, float cy)
    {
        for (int i = 0; i < count; ++i)
        {
            const float w = width_of(order[i]);
            g.button[static_cast<int>(order[i])] = {x, cy - w * 0.5f, w, w};
            x += w + gap;
        }
        return count > 0 ? x - gap : x;
    };
    const auto span = [&](const MediaControl *order, int count)
    {
        float total = 0.0f;
        for (int i = 0; i < count; ++i)
            total += width_of(order[i]) + (i > 0 ? gap : 0.0f);
        return total;
    };

    MediaControl order[kButtons];
    const int count = stops(order);
    // The volume button always closes the row; the bar beside it is its own.
    const bool volume = style.show_volume;
    const int transport = volume ? count - 1 : count;

    if (style.layout == MediaLayout::compact)
    {
        g.inner = {bounds_.x + style.padding, bounds_.y + style.padding * 0.6f,
                   bounds_.w - 2.0f * style.padding, bounds_.h - style.padding * 1.2f};
        const Rect &in = g.inner;
        g.track = {in.x, in.y + style.thumb_size * 0.5f - thick * 0.5f, in.w, thick};
        const float top = in.y + style.thumb_size + 6.0f;
        g.row_cy = top + (in.y + in.h - top) * 0.5f;
        // Transport on the left, the toggles and the volume on the right.
        int left = 0;
        while (left < transport && order[left] != MediaControl::shuffle &&
               order[left] != MediaControl::repeat)
            ++left;
        const float after = place(order, left, in.x, g.row_cy);
        float right = in.x + in.w;
        if (volume)
        {
            g.volume_track = {right - style.volume_width, g.row_cy - 3.0f, style.volume_width,
                              6.0f};
            right -= style.volume_width + 10.0f;
        }
        const float tail = span(order + left, count - left);
        place(order + left, count - left, right - tail, g.row_cy);
        g.time_left = after + 20.0f;
        g.time_baseline = g.row_cy + style.time_size * 0.35f;
        const float times = style.time_size * (hours() ? 10.6f : 7.6f);
        const float text_x = g.time_left + times + 16.0f;
        g.text = {text_x, top, std::max(right - tail - 20.0f - text_x, 0.0f), in.y + in.h - top};
        g.time_right = g.time_left + times;
        return g;
    }

    g.inner = bounds_.inset(style.padding);
    const Rect &in = g.inner;
    float x0 = in.x;
    if (style.show_art)
    {
        const float side = std::min(in.h, in.w * 0.3f);
        g.art = {in.x, in.y, side, side};
        x0 += side + style.art_gap;
    }
    const float right = in.x + in.w;
    const float width = right - x0;
    const float info = style.title_size * 1.2f + style.artist_size * 1.35f;
    g.text = {x0, in.y, width, info};

    const float row_top = in.y + in.h - row;
    g.row_cy = row_top + row * 0.5f;
    float limit = right;
    if (volume)
    {
        g.volume_track = {right - style.volume_width, g.row_cy - 3.0f, style.volume_width, 6.0f};
        const float bx = right - style.volume_width - 10.0f - size;
        g.button[static_cast<int>(MediaControl::volume)] = {bx, g.row_cy - size * 0.5f, size, size};
        limit = bx - 16.0f;
    }
    // The transport is centred on the row unless the volume is in its way.
    const float group = span(order, transport);
    const float gx = std::max(x0, std::min(x0 + (width - group) * 0.5f, limit - group));
    place(order, transport, gx, g.row_cy);

    const float mid = (in.y + info + row_top) * 0.5f;
    const float room = style.time_size * (hours() ? 5.4f : 3.9f);
    g.track = {x0 + room, mid - thick * 0.5f, std::max(width - 2.0f * room, 40.0f), thick};
    g.time_left = x0;
    g.time_right = right;
    g.time_baseline = mid + style.time_size * 0.35f;
    return g;
}

Rect MediaControls::thumb_rect(const Geometry &g) const
{
    const float size = style.thumb_size * (0.7f + 0.3f * scrub_focus_.value);
    const float cx = g.track.x + g.track.w * tween::clamp01(shown_.value);
    return {cx - size * 0.5f, g.track.cy() - size * 0.5f, size, size};
}

Rect MediaControls::focus_rect(const Geometry &g) const
{
    if (adjusting_)
    {
        const float cx = g.volume_track.x + g.volume_track.w * tween::clamp01(level_.value);
        return {cx - 9.0f, g.volume_track.cy() - 9.0f, 18.0f, 18.0f};
    }
    if (focus_ == MediaControl::scrubber)
        return thumb_rect(g);
    return g.button[static_cast<int>(focus_)];
}

Rect MediaControls::control_rect(MediaControl control) const
{
    const Geometry g = geometry();
    if (control == MediaControl::scrubber)
        return thumb_rect(g);
    return g.button[static_cast<int>(control)];
}

// ---- input ------------------------------------------------------------------

Event MediaControls::seek(int direction, const InputFrame &input, Feedback &feedback)
{
    const Geometry g = geometry();
    if (duration_ <= 0.0f)
        return refuse(feedback, style, input, ring_.refusal(), g.track.cx());
    // Holding the direction covers ground: every repeat seeks further.
    streak_ = input.nav_repeat ? streak_ + 1 : 0;
    const float step = std::min(
        style.seek_step * std::pow(std::max(style.seek_growth, 1.0f), static_cast<float>(streak_)),
        std::max(style.seek_max, style.seek_step));
    const float target =
        std::clamp(position_ + step * static_cast<float>(direction), 0.0f, duration_);
    if (std::fabs(target - position_) < 0.01f)
        return refuse(feedback, style, input, ring_.refusal(), thumb_rect(g).cx());
    position_ = target;
    seek_position_ = target;
    command_ = MediaCommand::seek;
    linger_ = 1.0f;
    const float along = target / duration_;
    play_cue(feedback, style, style.sounds.step, g.track.x + g.track.w * along,
             tween::lerp(0.9f, 1.2f, along));
    return Event::changed;
}

Event MediaControls::adjust_volume(int direction, const InputFrame &input, Feedback &feedback)
{
    const Geometry g = geometry();
    const float next = tween::clamp01(volume_ + style.volume_step * static_cast<float>(direction));
    const bool unmute = muted_ && direction > 0;
    if (std::fabs(next - volume_) < 1e-4f && !unmute)
        return refuse(feedback, style, input, ring_.refusal(), g.volume_track.cx());
    volume_ = next;
    if (unmute)
        muted_ = false;
    command_ = MediaCommand::volume;
    play_cue(feedback, style, style.sounds.step, g.volume_track.cx(),
             tween::lerp(0.85f, 1.25f, volume_));
    return Event::changed;
}

Event MediaControls::toggle_mute(Feedback &feedback)
{
    muted_ = !muted_;
    idle_ = 0.0f;
    command_ = MediaCommand::mute;
    play_cue(feedback, style, style.sounds.change, geometry().volume_track.cx(),
             muted_ ? 0.94f : 1.06f);
    return Event::changed;
}

Event MediaControls::activate(MediaControl control, const InputFrame &input, Feedback &feedback)
{
    const Geometry g = geometry();
    const float x = control == MediaControl::scrubber ? thumb_rect(g).cx()
                                                      : g.button[static_cast<int>(control)].cx();
    // Confirm on the scrubber is play / pause: the thumb is where the eyes are.
    if (control == MediaControl::scrubber)
        control = MediaControl::play;
    pressed_ = control;
    press_.trigger();
    switch (control)
    {
    case MediaControl::play:
        command_ = playing_ ? MediaCommand::pause : MediaCommand::play;
        play_cue(feedback, style, style.sounds.activate, x);
        if (style.sounds.rumble > 0.0f)
            feedback.rumble(0.3f * style.sounds.rumble, 0.04f);
        return Event::activated;
    case MediaControl::previous:
    case MediaControl::next:
        command_ = control == MediaControl::next ? MediaCommand::next : MediaCommand::previous;
        play_cue(feedback, style, style.sounds.page, x,
                 control == MediaControl::next ? 1.04f : 0.96f);
        return Event::activated;
    case MediaControl::rewind:
    case MediaControl::forward:
    {
        const float sign = control == MediaControl::forward ? 1.0f : -1.0f;
        const float target = std::clamp(position_ + sign * style.skip_seconds, 0.0f, duration_);
        if (duration_ <= 0.0f || std::fabs(target - position_) < 0.01f)
            return refuse(feedback, style, input, ring_.refusal(), x);
        position_ = target;
        seek_position_ = target;
        linger_ = 1.0f;
        command_ = sign > 0.0f ? MediaCommand::forward : MediaCommand::rewind;
        play_cue(feedback, style, style.sounds.step, x,
                 tween::lerp(0.9f, 1.2f, target / duration_));
        return Event::activated;
    }
    case MediaControl::shuffle:
        shuffle_ = !shuffle_;
        command_ = MediaCommand::shuffle;
        play_cue(feedback, style, style.sounds.change, x, shuffle_ ? 1.06f : 0.94f);
        return Event::changed;
    case MediaControl::repeat:
        repeat_ = repeat_ == MediaRepeat::off
                      ? MediaRepeat::all
                      : (repeat_ == MediaRepeat::all ? MediaRepeat::one : MediaRepeat::off);
        command_ = MediaCommand::repeat;
        play_cue(feedback, style, style.sounds.change, x,
                 repeat_ == MediaRepeat::off ? 0.94f
                                             : (repeat_ == MediaRepeat::all ? 1.0f : 1.06f));
        return Event::changed;
    case MediaControl::volume:
        // Left and right are the row's: confirm hands them to the volume
        // until confirm or back gives them back.
        adjusting_ = true;
        play_cue(feedback, style, style.sounds.activate, x);
        return Event::none;
    case MediaControl::scrubber:
        break;
    }
    return Event::none;
}

Event MediaControls::handle(const InputFrame &input, Feedback &feedback)
{
    exit_ = Direction::none;
    command_ = MediaCommand::none;
    if (input.nav != Direction::none || input.pressed != 0)
    {
        // The first press on hidden controls only brings them back.
        const bool was_hidden = hidden();
        idle_ = 0.0f;
        if (was_hidden)
            return Event::none;
    }
    if (!shown(focus_))
        focus_ = MediaControl::play;
    const Geometry g = geometry();

    if (adjusting_)
    {
        if (input.nav == Direction::left || input.nav == Direction::down)
            return adjust_volume(-1, input, feedback);
        if (input.nav == Direction::right || input.nav == Direction::up)
            return adjust_volume(1, input, feedback);
        if (input.is_pressed(Action::confirm) || input.is_pressed(Action::back))
        {
            adjusting_ = false;
            play_cue(feedback, style,
                     input.is_pressed(Action::back) ? style.sounds.cancel : style.sounds.activate,
                     g.volume_track.cx());
        }
        return Event::none;
    }

    const float x = focus_rect(g).cx();
    const auto leave = [&](Direction edge)
    {
        if (style.exits.allows(edge))
        {
            exit_ = edge;
            return Event::none;
        }
        return refuse(feedback, style, input, ring_.refusal(), x);
    };

    if (focus_ == MediaControl::scrubber)
    {
        if (input.nav == Direction::left || input.nav == Direction::right)
            return seek(input.nav == Direction::right ? 1 : -1, input, feedback);
        if (input.nav == Direction::up)
            return leave(Direction::up);
        if (input.nav == Direction::down)
        {
            focus_ = shown(row_focus_) ? row_focus_ : MediaControl::play;
            play_cue(feedback, style, style.sounds.move, focus_rect(g).cx());
            return Event::moved;
        }
    }
    else if (input.nav == Direction::left || input.nav == Direction::right)
    {
        MediaControl order[kButtons];
        const int count = stops(order);
        int at = 0;
        for (int i = 0; i < count; ++i)
        {
            if (order[i] == focus_)
                at = i;
        }
        const int next = at + (input.nav == Direction::right ? 1 : -1);
        if (next < 0 || next >= count)
            return leave(input.nav);
        focus_ = row_focus_ = order[next];
        play_cue(feedback, style, style.sounds.move, focus_rect(g).cx());
        return Event::moved;
    }
    else if (input.nav == Direction::up)
    {
        row_focus_ = focus_;
        focus_ = MediaControl::scrubber;
        play_cue(feedback, style, style.sounds.move, thumb_rect(g).cx(), 1.05f);
        return Event::moved;
    }
    else if (input.nav == Direction::down)
    {
        return leave(Direction::down);
    }

    if (input.is_pressed(Action::confirm))
        return activate(focus_, input, feedback);
    if (input.is_pressed(Action::back))
    {
        play_cue(feedback, style, style.sounds.cancel, x);
        return Event::cancelled;
    }
    return Event::none;
}

void MediaControls::update(float dt)
{
    const bool counting = style.auto_hide > 0.0f && (playing_ || style.hide_paused) && !adjusting_;
    idle_ = counting ? idle_ + dt : 0.0f;
    visible_.target = hidden() ? 0.0f : 1.0f;
    // Leaving is unhurried; coming back is immediate.
    visible_.update(dt, hidden() ? 7.0f : 18.0f);

    shown_.target = duration_ > 0.0f ? tween::clamp01(position_ / duration_) : 0.0f;
    shown_.update(dt, std::max(style.omega(), 16.0f));
    morph_.target = playing_ ? 1.0f : 0.0f;
    morph_.update(dt, std::max(style.omega(), 16.0f));
    const bool scrubbing = focus_ == MediaControl::scrubber && active_ && !adjusting_;
    scrub_focus_.target = scrubbing ? 1.0f : 0.0f;
    scrub_focus_.update(dt, 16.0f);
    linger_ = std::max(0.0f, linger_ - dt);
    bubble_.target = style.bubble && (scrubbing || linger_ > 0.0f) ? 1.0f : 0.0f;
    bubble_.update(dt, 16.0f);
    level_.target = muted_ ? 0.0f : volume_;
    level_.update(dt, 20.0f);
    adjust_.target = adjusting_ ? 1.0f : 0.0f;
    adjust_.update(dt, 18.0f);
    shuffle_on_.target = shuffle_ ? 1.0f : 0.0f;
    shuffle_on_.update(dt, 16.0f);
    repeat_on_.target = repeat_ != MediaRepeat::off ? 1.0f : 0.0f;
    repeat_on_.update(dt, 16.0f);
    active_amount_.target = active_ ? 1.0f : 0.0f;
    active_amount_.update(dt, 18.0f);
    press_.update(dt, 10.0f);

    const Rect target = focus_rect(geometry());
    ring_.target(target);
    if (!ring_set_)
        ring_.snap(target);
    ring_set_ = true;
    ring_.update(dt, style);
}

// ---- drawing ----------------------------------------------------------------

void MediaControls::draw_art(Canvas &canvas, Painter &paint, const Rect &box) const
{
    const Theme &theme = style.theme;
    const float corner = theme.corner == Corner::round ? std::min(theme.radius_card, 16.0f) : 0.0f;
    if (art)
    {
        art(canvas, box, corner);
        return;
    }
    // No artwork: a note on a sunken plate.
    paint.well(box, corner, theme.surface_high);
    gfx::DrawList &list = canvas.list;
    const float s = box.w * 0.2f;
    const Color ink = opaque(theme.text_muted);
    const float cx = box.cx() - s * 0.4f;
    const float cy = box.cy() + s * 0.2f;
    list.circle(cx - s * 0.45f, cy + s * 0.8f, s * 0.42f, ink);
    list.circle(cx + s * 1.05f, cy + s * 0.5f, s * 0.42f, ink);
    list.line(cx - s * 0.12f, cy + s * 0.8f, cx - s * 0.12f, cy - s * 0.9f, s * 0.18f, ink);
    list.line(cx + s * 1.38f, cy + s * 0.5f, cx + s * 1.38f, cy - s * 1.2f, s * 0.18f, ink);
    list.line(cx - s * 0.12f, cy - s * 0.9f, cx + s * 1.38f, cy - s * 1.2f, s * 0.32f, ink);
}

void MediaControls::draw_button(Canvas &canvas, Painter &paint, MediaControl control,
                                const Rect &r) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    const float press = pressed_ == control ? tween::clamp01(press_.value) : 0.0f;
    const bool toggle = control == MediaControl::shuffle || control == MediaControl::repeat ||
                        control == MediaControl::volume;
    const bool primary = control == MediaControl::play;
    const bool surfaced = style.buttons == MediaButtons::surface && !toggle;
    const bool round = round_theme(theme);
    const Color resting = style.panel ? theme.text : paint.page_text();
    const Color quiet = style.panel ? theme.text_muted : paint.page_text_muted();

    float cx = r.cx();
    float cy = r.cy();
    Color ink = opaque(resting);
    if (surfaced)
    {
        Look look;
        look.press = press;
        paint.button(r, "", primary ? ButtonKind::primary : ButtonKind::secondary, look);
        if (primary)
            ink = theme.style == SurfaceStyle::glow ? theme.primary : theme.on_primary;
        else
            ink = theme.on_secondary;
        ink = opaque(ink);
        // The icon rides on the body the way the painter's label does.
        if (theme.style == SurfaceStyle::hard)
        {
            cx += theme.shadow_offset * press;
            cy += theme.shadow_offset * press;
        }
        else if (theme.style == SurfaceStyle::bevel && press > 0.5f)
        {
            cx += 2.0f;
            cy += 2.0f;
        }
        else if (theme.style == SurfaceStyle::sketch)
        {
            cy += 2.0f * press;
        }
    }

    float on = 0.0f;
    if (control == MediaControl::shuffle)
        on = shuffle_on_.value;
    else if (control == MediaControl::repeat)
        on = repeat_on_.value;
    else if (control == MediaControl::volume)
        on = adjust_.value;
    if (toggle)
    {
        // On is said three ways, since colour alone is not enough: a tinted
        // plate, the accent ink and a dot under the icon.
        const Color accent = opaque(theme.accent);
        ink = gfx::mix(opaque(quiet), accent, tween::clamp01(on));
        if (control == MediaControl::volume)
            ink = gfx::mix(opaque(resting), accent, tween::clamp01(on));
        if (on > 0.01f)
        {
            const Rect plate = r.inset(4.0f);
            paint.fill(plate, paint.control_radius(plate), accent.with_alpha(0.16f * on));
            if (control != MediaControl::volume)
            {
                const float dot = 3.0f * tween::clamp01(on);
                if (round)
                    list.circle(cx, r.y + r.h - 7.0f, dot, accent);
                else
                    list.rounded_rect({cx - dot, r.y + r.h - 7.0f - dot, dot * 2.0f, dot * 2.0f},
                                      0.0f, accent);
            }
        }
    }

    const float k = r.w / 56.0f * (surfaced ? 1.0f : 1.12f);
    list.push_transform(1.0f - 0.08f * press, cx, cy, 0.0f, 0.0f);
    switch (control)
    {
    case MediaControl::play:
    {
        // Play and pause trade places, each shrinking as it fades.
        const float m = tween::clamp01(morph_.value);
        if (m < 0.99f)
            icon_triangle(list, cx + 2.0f * k, cy, 24.0f * k * (0.6f + 0.4f * (1.0f - m)), 1.0f,
                          ink.with_alpha(1.0f - m));
        if (m > 0.01f)
            icon_pause(list, cx, cy, k * (0.6f + 0.4f * m), round, ink.with_alpha(m));
        break;
    }
    case MediaControl::previous:
        icon_track(list, cx, cy, k, -1.0f, round, ink);
        break;
    case MediaControl::next:
        icon_track(list, cx, cy, k, 1.0f, round, ink);
        break;
    case MediaControl::rewind:
        icon_skip(list, cx, cy, k, -1.0f, ink);
        break;
    case MediaControl::forward:
        icon_skip(list, cx, cy, k, 1.0f, ink);
        break;
    case MediaControl::shuffle:
        icon_shuffle(list, cx, cy, k, ink);
        break;
    case MediaControl::repeat:
        icon_repeat(list, cx, cy, k, repeat_ == MediaRepeat::one, ink);
        break;
    case MediaControl::volume:
        icon_volume(list, cx, cy, k, volume_, muted_, round, ink);
        break;
    case MediaControl::scrubber:
        break;
    }
    list.pop_transform();
}

void MediaControls::draw_scrubber(Canvas &canvas, Painter &paint, const Geometry &g) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    const Color quiet = style.panel ? theme.text_muted : paint.page_text_muted();

    // The track is the library's own buffered bar: it already knows how every
    // theme builds a well and a fill.
    ProgressBar bar;
    bar.style.theme = theme;
    bar.style.reduced_motion = style.reduced_motion;
    bar.style.mode = ProgressMode::buffered;
    bar.style.height = g.track.h;
    bar.style.placement = LabelPlacement::none;
    bar.style.percent = false;
    bar.style.finish_flash = false;
    bar.set_bounds(g.track);
    bar.set_value(shown_.value, true);
    bar.set_buffer(duration_ > 0.0f ? buffered_ / duration_ : 0.0f, true);
    bar.draw(canvas);

    if (style.chapter_marks && duration_ > 0.0f)
    {
        for (const MediaChapter &chapter : chapters_)
        {
            if (chapter.time <= 0.0f || chapter.time >= duration_)
                continue;
            const float x = g.track.x + g.track.w * chapter.time / duration_;
            list.rounded_rect({x - 1.5f, g.track.y - 8.0f, 3.0f, 5.0f}, 0.0f,
                              quiet.with_alpha(0.8f));
        }
    }

    const Rect thumb = thumb_rect(g);
    const float corner = std::min(theme.radius, thumb.w * 0.5f);
    switch (theme.style)
    {
    case SurfaceStyle::outline:
    case SurfaceStyle::glow:
        paint.fill(thumb, corner, theme.accent);
        break;
    case SurfaceStyle::hard:
    case SurfaceStyle::pixel:
    case SurfaceStyle::sketch:
        paint.fill(thumb, corner, kThumb);
        paint.stroke(thumb, corner, std::min(theme.border, 4.0f), theme.outline);
        break;
    case SurfaceStyle::neumorphic:
    case SurfaceStyle::bevel:
    case SurfaceStyle::gloss:
        paint.surface(thumb, corner, theme.surface, theme.outline, 1.0f);
        break;
    default:
        list.shadow({thumb.x, thumb.y + 3.0f, thumb.w, thumb.h}, corner, 7.0f,
                    theme.shadow.a > 0.0f ? theme.shadow : quiet.with_alpha(0.4f));
        paint.fill(thumb, corner, kThumb);
        paint.stroke(thumb, corner, 3.0f, theme.accent);
        break;
    }
}

// Where the time bubble sits: above the thumb, kept inside the content.
Rect MediaControls::bubble_plate(const Painter &paint, const Geometry &g) const
{
    const float amount = tween::clamp01(bubble_.value);
    if (amount <= 0.01f || duration_ <= 0.0f)
        return {};
    char text[16];
    format_time(text, sizeof(text), position_, hours());
    const float size = style.time_size + 1.0f;
    const int chapter = chapter_at(position_);
    float name = 0.0f;
    if (chapter >= 0)
        name = std::min(paint.body_width(chapters_[static_cast<std::size_t>(chapter)].title, 20.0f),
                        260.0f);
    const float width = std::max(tabular_width(paint, text, size), name) + 30.0f;
    const float height = size + 18.0f + (name > 0.0f ? 27.0f : 0.0f);
    const Rect thumb = thumb_rect(g);
    const float lo = g.inner.x + width * 0.5f;
    const float hi = g.inner.x + g.inner.w - width * 0.5f;
    const float cx = lo < hi ? std::clamp(thumb.cx(), lo, hi) : g.inner.cx();
    const float rise = style.reduced_motion ? 0.0f : 8.0f * (1.0f - amount);
    return {cx - width * 0.5f, thumb.y - 14.0f - height + rise, width, height};
}

void MediaControls::draw_bubble(Canvas &canvas, Painter &paint, const Geometry &g) const
{
    const Rect plate = bubble_plate(paint, g);
    if (plate.w <= 0.0f)
        return;
    const float amount = tween::clamp01(bubble_.value);
    const Theme &theme = style.theme;
    char text[16];
    format_time(text, sizeof(text), position_, hours());
    const float size = style.time_size + 1.0f;
    const int chapter = chapter_at(position_);
    std::string name;
    if (chapter >= 0)
        name = fit_body(paint, chapters_[static_cast<std::size_t>(chapter)].title, 20.0f, 260.0f);

    canvas.list.push_opacity(amount);
    draw_overlay_panel(canvas, theme, plate, false, 0.55f, std::min(theme.radius, 12.0f));
    draw_tabular(paint, text, plate.cx(), plate.y + 9.0f + size * 0.84f, size, theme.text,
                 gfx::Align::center);
    if (!name.empty())
        paint.body(name, plate.cx(), plate.y + plate.h - 12.0f, 20.0f, theme.text_muted,
                   gfx::Align::center);
    canvas.list.pop_opacity();
}

void MediaControls::draw(Canvas &canvas) const
{
    const float visible = visibility();
    if (visible <= 0.01f)
        return;
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const Geometry g = geometry();
    const Color ink = style.panel ? theme.text : paint.page_text();
    const Color quiet = style.panel ? theme.text_muted : paint.page_text_muted();

    list.push_opacity(visible);
    list.push_transform(1.0f, 0.0f, 0.0f, 0.0f,
                        style.reduced_motion ? 0.0f : 18.0f * (1.0f - visible));
    if (style.panel)
        paint.panel(bounds_);

    char elapsed[16];
    char other[20];
    format_time(elapsed, sizeof(elapsed), position_, hours());
    const float size = style.time_size;
    if (style.layout == MediaLayout::compact)
    {
        // "0:42 / 3:54", then the title and the artist on the same line.
        char total[16];
        format_time(total, sizeof(total), duration_, hours());
        std::snprintf(other, sizeof(other), " / %s", total);
        const float taken = draw_tabular(paint, elapsed, g.time_left, g.time_baseline, size, ink);
        draw_tabular(paint, other, g.time_left + taken, g.time_baseline, size, quiet);
        if (g.text.w > 60.0f && !title.empty())
        {
            const float baseline = g.row_cy + style.title_size * 0.35f;
            const float used = paint.bounded_label(title, g.text.x, baseline, style.title_size, ink, g.text.w);
            const float room = g.text.w - used - 18.0f;
            if (!artist.empty() && room > 90.0f)
                paint.bounded_body(artist, g.text.x + used + 18.0f, baseline, style.artist_size, quiet, room);
        }
    }
    else
    {
        if (g.art.w > 0.0f)
            draw_art(canvas, paint, g.art);
        const float first = g.text.y + style.title_size * 0.92f;
        const std::string name = fit_label(paint, title, style.title_size, g.text.w);
        const std::string by = fit_body(paint, artist, style.artist_size, g.text.w);
        // Near the start of a track the time bubble comes down over these
        // words: they step back for it instead of showing through its edges.
        float covered = 0.0f;
        const Rect plate = bubble_plate(paint, g);
        if (plate.w > 0.0f && plate.y < first + style.artist_size * 1.4f + 8.0f)
        {
            const float end = g.text.x + std::max(paint.label_width(name, style.title_size),
                                                  paint.body_width(by, style.artist_size));
            covered =
                tween::clamp01(bubble_.value) * tween::clamp01((end + 28.0f - plate.x) / 20.0f);
        }
        list.push_opacity(1.0f - 0.88f * covered);
        paint.label(name, g.text.x, first, style.title_size, ink);
        paint.body(by, g.text.x, first + style.artist_size * 1.4f, style.artist_size, quiet);
        list.pop_opacity();
        draw_tabular(paint, elapsed, g.time_left, g.time_baseline, size, ink);
        if (style.remaining)
        {
            char left[16];
            format_time(left, sizeof(left), std::max(duration_ - position_, 0.0f), hours());
            std::snprintf(other, sizeof(other), "-%s", left);
        }
        else
        {
            format_time(other, sizeof(other), duration_, hours());
        }
        draw_tabular(paint, other, g.time_right, g.time_baseline, size, quiet, gfx::Align::right);
    }

    draw_scrubber(canvas, paint, g);
    for (int i = 0; i < kButtons; ++i)
    {
        if (g.button[i].w > 0.0f)
            draw_button(canvas, paint, static_cast<MediaControl>(i), g.button[i]);
    }
    if (style.show_volume)
    {
        Rect track = g.volume_track;
        const float thick = framed(theme) ? track.h + 2.0f * theme.border : track.h;
        track = {track.x, track.cy() - thick * 0.5f, track.w, thick};
        ProgressBar level;
        level.style.theme = theme;
        level.style.height = thick;
        level.style.placement = LabelPlacement::none;
        level.style.percent = false;
        level.style.finish_flash = false;
        level.set_bounds(track);
        level.set_value(level_.value, true);
        level.draw(canvas);
        const float shown = tween::clamp01(adjust_.value);
        if (shown > 0.01f)
        {
            const float half = 8.0f * shown;
            const float cx = track.x + track.w * tween::clamp01(level_.value);
            paint.fill({cx - half, track.cy() - half, 2.0f * half, 2.0f * half},
                       std::min(theme.radius, half), opaque(theme.accent));
        }
    }

    // One ring for the whole component. On the scrubber it locks onto the
    // thumb, so the two never drift apart while the clock moves it.
    if (active_amount_.value > 0.01f && ring_set_)
    {
        const float lock = adjusting_ ? 0.0f : tween::clamp01(scrub_focus_.value);
        const Rect glide = ring_.rect(canvas.time);
        const Rect thumb = thumb_rect(g);
        const Rect r{tween::lerp(glide.x, thumb.x, lock), tween::lerp(glide.y, thumb.y, lock),
                     tween::lerp(glide.w, thumb.w, lock), tween::lerp(glide.h, thumb.h, lock)};
        paint.focus_ring(r, paint.control_radius(r), active_amount_.value);
    }
    draw_bubble(canvas, paint, g);
    list.pop_transform();
    list.pop_opacity();
}

} // namespace hui::ui
