// ps5-homebrew-ui - Component: SearchField.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/search_field.hpp"

#include "ui/components/focus_frame.hpp"
#include "ui/components/overlay.hpp"

#include <algorithm>
#include <cmath>

namespace hui::ui
{

using gfx::Color;
using gfx::Rect;

namespace
{

constexpr float kClearSize = 30.0f;   // the clear button's plate
constexpr float kSpinnerSize = 26.0f; // the "searching" spinner
constexpr float kRowInset = 14.0f;    // between a row's edge and its content
constexpr float kIconRoom = 32.0f;    // a row's leading symbol and the space after it

bool continuation(char c)
{
    return (static_cast<unsigned char>(c) & 0xc0) == 0x80;
}

int characters(std::string_view text)
{
    int count = 0;
    for (char c : text)
        count += continuation(c) ? 0 : 1;
    return count;
}

void drop_last(std::string &text)
{
    while (!text.empty() && continuation(text.back()))
        text.pop_back();
    if (!text.empty())
        text.pop_back();
}

void drop_first(std::string &text)
{
    std::size_t next = 1;
    while (next < text.size() && continuation(text[next]))
        ++next;
    text.erase(0, next);
}

char lower(char c)
{
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

// Where `query` first occurs in `text`, ignoring ASCII case.
std::size_t find_match(std::string_view text, std::string_view query)
{
    if (query.empty() || query.size() > text.size())
        return std::string_view::npos;
    for (std::size_t at = 0; at + query.size() <= text.size(); ++at)
    {
        std::size_t same = 0;
        while (same < query.size() && lower(text[at + same]) == lower(query[same]))
            ++same;
        if (same == query.size())
            return at;
    }
    return std::string_view::npos;
}

// A magnifier from a ring and a stroke; r is the radius of the lens.
void draw_magnifier(gfx::DrawList &list, float cx, float cy, float r, Color ink)
{
    const float pen = std::max(2.0f, r * 0.3f);
    list.ring(cx - r * 0.25f, cy - r * 0.25f, r, pen, ink);
    list.line(cx + r * 0.55f, cy + r * 0.55f, cx + r * 1.25f, cy + r * 1.25f, pen * 1.15f, ink);
}

// A clock: "you searched for this before".
void draw_clock(gfx::DrawList &list, float cx, float cy, float r, Color ink)
{
    const float pen = std::max(2.0f, r * 0.24f);
    list.ring(cx, cy, r, pen, ink);
    list.line(cx, cy, cx, cy - r * 0.5f, pen, ink);
    list.line(cx, cy, cx + r * 0.36f, cy + r * 0.2f, pen, ink);
}

} // namespace

void SearchField::set_recent(std::vector<std::string> recent)
{
    recent_ = std::move(recent);
    if (text_.empty())
        ask();
}

void SearchField::add_recent(std::string_view query)
{
    if (query.empty())
        return;
    std::erase(recent_, std::string(query));
    recent_.insert(recent_.begin(), std::string(query));
    if (style.max_recent > 0 && recent_.size() > static_cast<std::size_t>(style.max_recent))
        recent_.resize(static_cast<std::size_t>(style.max_recent));
    if (text_.empty())
        ask();
}

void SearchField::set_text(std::string_view text)
{
    text_.clear();
    if (!insert(text))
        edited();
}

int SearchField::length() const
{
    return characters(text_);
}

bool SearchField::insert(char c)
{
    if (c < 0x20 || c == 0x7f || full())
        return false;
    text_.push_back(c);
    edited();
    return true;
}

bool SearchField::insert(std::string_view utf8)
{
    bool changed = false;
    std::size_t at = 0;
    while (at < utf8.size() && !full())
    {
        std::size_t next = at + 1;
        while (next < utf8.size() && continuation(utf8[next]))
            ++next;
        if (static_cast<unsigned char>(utf8[at]) >= 0x20 && utf8[at] != 0x7f)
        {
            text_.append(utf8.substr(at, next - at));
            changed = true;
        }
        at = next;
    }
    if (changed)
        edited();
    return changed;
}

bool SearchField::backspace()
{
    if (text_.empty())
        return false;
    drop_last(text_);
    edited();
    return true;
}

void SearchField::clear()
{
    text_.clear();
    edited();
}

Event SearchField::insert(char c, Feedback &feedback)
{
    if (!insert(c))
        return refuse(feedback, style, InputFrame{}, shake_, bounds_.cx());
    play_cue(feedback, style, style.type, bounds_.cx());
    return Event::changed;
}

Event SearchField::backspace(Feedback &feedback)
{
    if (!backspace())
        return refuse(feedback, style, InputFrame{}, shake_, bounds_.cx());
    play_cue(feedback, style, style.erase, bounds_.cx());
    return Event::changed;
}

// The text changed. An empty query shows the recent searches at once; any
// other waits for the player to stop typing before the provider is asked.
void SearchField::edited()
{
    blink_ = 0.0f;
    if (text_.empty() || style.debounce <= 0.0f)
    {
        ask();
    }
    else
    {
        pending_ = style.debounce;
        asked_ = false;
        // Answers to the previous query may stay until the new ones arrive;
        // recent searches may not: they answer nothing that was typed.
        if (showing_recent_)
        {
            rows_.clear();
            showing_recent_ = false;
            if (focus_ >= 0)
                focus_ = kFocusField;
        }
    }
    if (focus_ == kFocusClear && !has_clear())
        focus_ = kFocusField;
}

void SearchField::ask()
{
    pending_ = 0.0f;
    rows_.clear();
    showing_recent_ = text_.empty();
    if (showing_recent_)
    {
        for (const std::string &query : recent_)
            rows_.push_back({query, std::string(), 0});
    }
    else if (provider)
    {
        provider(text_, rows_);
    }
    asked_ = true;
    results_age_ = 0.0f;
    const int rows = shown_rows();
    if (focus_ >= rows)
        focus_ = rows > 0 ? rows - 1 : kFocusField;
    last_row_ = std::clamp(last_row_, 0, std::max(rows - 1, 0));
    retarget(true);
}

void SearchField::refresh()
{
    ask();
}

int SearchField::shown_rows() const
{
    return std::min(static_cast<int>(rows_.size()), std::max(style.max_rows, 0));
}

bool SearchField::has_header() const
{
    return showing_recent_ && !style.recent_title.empty() && shown_rows() > 0;
}

bool SearchField::has_clear() const
{
    return style.clear_button && !text_.empty();
}

float SearchField::list_height() const
{
    const int rows = shown_rows();
    if (rows > 0)
        return 2.0f * style.list_padding + (has_header() ? style.header_height : 0.0f) +
               static_cast<float>(rows) * style.row_height;
    // A query the provider had no answer for says so instead of closing.
    if (asked_ && !text_.empty() && provider && !style.empty_text.empty())
        return 2.0f * style.list_padding + style.row_height;
    return 0.0f;
}

void SearchField::set_bounds(const Rect &bounds)
{
    const bool changed = bounds.x != bounds_.x || bounds.y != bounds_.y || bounds.w != bounds_.w;
    bounds_ = bounds;
    if (changed)
        retarget(true);
}

float SearchField::preferred_height() const
{
    return style.field_height + style.gap + 2.0f * style.list_padding + style.header_height +
           static_cast<float>(std::max(style.max_rows, 0)) * style.row_height;
}

Rect SearchField::field_rect() const
{
    return {bounds_.x, bounds_.y, bounds_.w, style.field_height};
}

Rect SearchField::list_rect() const
{
    return {bounds_.x, bounds_.y + style.field_height + style.gap, bounds_.w, list_height()};
}

Rect SearchField::row_rect(int index) const
{
    const Rect list = list_rect();
    return {list.x + style.list_padding,
            list.y + style.list_padding + (has_header() ? style.header_height : 0.0f) +
                static_cast<float>(index) * style.row_height,
            list.w - 2.0f * style.list_padding, style.row_height};
}

void SearchField::retarget(bool snap)
{
    const Rect target = row_rect(focus_ >= 0 ? focus_ : last_row_);
    highlight_.target(target);
    if (snap)
        highlight_.snap(target);
}

void SearchField::set_active(bool active)
{
    if (active && !active_)
        blink_ = 0.0f;
    active_ = active;
}

// An edge: hand the focus on where the screen asked for it, refuse elsewhere.
Event SearchField::leave(const InputFrame &input, Feedback &feedback)
{
    if (style.exits.allows(input.nav))
    {
        exit_ = input.nav;
        return Event::none;
    }
    return refuse(feedback, style, input, focus_ >= 0 ? highlight_.refusal() : shake_,
                  bounds_.cx());
}

Event SearchField::handle(const InputFrame &input, Feedback &feedback)
{
    exit_ = Direction::none;
    has_pick_ = false;
    const float x = bounds_.cx();
    const int rows = shown_rows();
    if (focus_ >= rows)
        focus_ = rows > 0 ? rows - 1 : kFocusField;

    if (input.is_pressed(Action::back))
    {
        focus_ = kFocusField;
        play_cue(feedback, style, style.sounds.cancel, x);
        return Event::cancelled;
    }

    if (input.nav != Direction::none)
    {
        const int before = focus_;
        switch (input.nav)
        {
        case Direction::down:
            if (focus_ < 0 && rows > 0)
                focus_ = 0;
            else if (focus_ >= 0 && focus_ + 1 < rows)
                ++focus_;
            else
                return leave(input, feedback);
            break;
        case Direction::up:
            if (focus_ > 0)
                --focus_;
            else if (focus_ == 0)
                focus_ = kFocusField;
            else
                return leave(input, feedback);
            break;
        case Direction::left:
            if (focus_ != kFocusClear)
                return leave(input, feedback);
            focus_ = kFocusField;
            break;
        default:
            if (focus_ != kFocusField || !has_clear())
                return leave(input, feedback);
            focus_ = kFocusClear;
            break;
        }
        if (focus_ >= 0)
        {
            last_row_ = focus_;
            // Entering the list, the highlight appears on the row; inside it
            // glides.
            retarget(before < 0);
        }
        const float along = rows > 1 && focus_ >= 0
                                ? static_cast<float>(focus_) / static_cast<float>(rows - 1)
                                : 0.0f;
        play_cue(feedback, style, style.sounds.move, x, tween::lerp(1.05f, 0.95f, along));
        return Event::moved;
    }

    if (input.is_pressed(Action::confirm))
    {
        if (focus_ == kFocusClear)
        {
            clear();
            focus_ = kFocusField;
            play_cue(feedback, style, style.erase, x);
            return Event::changed;
        }
        press_.trigger();
        play_cue(feedback, style, style.sounds.activate, x);
        if (focus_ >= 0)
        {
            picked_ = rows_[static_cast<std::size_t>(focus_)];
            has_pick_ = true;
            if (style.sounds.rumble > 0.0f)
                feedback.rumble(0.3f * style.sounds.rumble, 0.04f);
            if (style.remember)
                add_recent(picked_.text);
            if (style.fill_on_pick)
            {
                // The query is answered: the list closes until the next edit.
                text_ = picked_.text;
                blink_ = 0.0f;
                pending_ = 0.0f;
                rows_.clear();
                showing_recent_ = false;
                asked_ = false;
            }
            focus_ = kFocusField;
        }
        return Event::activated;
    }
    return Event::none;
}

void SearchField::update(float dt)
{
    blink_ += dt;
    results_age_ += dt;
    if (pending_ > 0.0f)
    {
        pending_ -= dt;
        if (pending_ <= 0.0f)
            ask();
    }

    const Rect field = field_rect();
    const bool line = style.theme.underline_fields;
    float right = field.x + field.w - (line ? 4.0f : 16.0f);
    if (has_clear())
        right -= kClearSize + 10.0f;
    spinner_.style.theme = style.theme;
    spinner_.style.reduced_motion = style.reduced_motion;
    spinner_.set_bounds(
        {right - kSpinnerSize, field.cy() - kSpinnerSize * 0.5f, kSpinnerSize, kSpinnerSize});
    spinner_.set_spinning(searching());
    spinner_.update(dt);

    const float omega = std::max(style.omega(), 14.0f);
    field_focus_.target = active_ && focus_ == kFocusField ? 1.0f : 0.0f;
    field_focus_.update(dt, 18.0f);
    clear_focus_.target = active_ && focus_ == kFocusClear ? 1.0f : 0.0f;
    clear_focus_.update(dt, 18.0f);
    clear_shown_.target = has_clear() ? 1.0f : 0.0f;
    clear_shown_.update(dt, 18.0f);
    list_focus_.target = active_ && focus_ >= 0 ? 1.0f : 0.0f;
    list_focus_.update(dt, 18.0f);
    const float height = list_height();
    open_.target = (active_ || style.keep_open) && height > 0.0f ? 1.0f : 0.0f;
    open_.update(dt, omega);
    // A closing list keeps its height while it fades.
    if (height > 0.0f)
        height_.target = height;
    height_.update(dt, omega);
    retarget(false);
    highlight_.update(dt, style);
    shake_.update(dt, 7.0f);
    press_.update(dt, 10.0f);
}

// The query as the field shows it: cut from the front when it is longer than
// the field, so the caret stays in sight.
std::string SearchField::shown(const Painter &paint, float room) const
{
    std::string display = text_;
    while (!display.empty() && paint.body_width(display, style.text_size) > room)
        drop_first(display);
    return display;
}

void SearchField::draw_field(Canvas &canvas, Painter &paint) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    const Rect field = field_rect();
    const float focus = field_focus_.value;
    const bool line = theme.underline_fields;
    const bool white =
        !line && (theme.style == SurfaceStyle::bevel || theme.style == SurfaceStyle::hard);
    const Color face = white ? theme.light : theme.surface_high;
    const Color page_ink = style.on_page ? paint.page_text() : theme.text;
    const Color page_quiet = style.on_page ? paint.page_text_muted() : theme.text_muted;
    const Color ink = line ? page_ink : (white ? Painter::on(face) : theme.text);
    const Color quiet = line ? page_quiet : (white ? ink.with_alpha(0.55f) : theme.text_muted);
    const float radius = style.pill ? field.h * 0.5f : std::min(theme.radius, 12.0f);
    const float cy = field.cy();

    const bool under = !line && focus_frame_goes_under(theme);
    const float ring = style.focus_ring ? focus : 0.0f;
    if (under)
        focus_frame(canvas, theme, field, radius, ring, true);
    if (line)
    {
        const float thick = 2.0f + 2.0f * focus;
        list.rounded_rect({field.x, field.y + field.h - thick, field.w, thick}, 0.0f,
                          gfx::mix(page_quiet, theme.primary, focus));
    }
    else
    {
        paint.well(field, radius, face);
    }

    draw_magnifier(list, field.x + (line ? 12.0f : 30.0f), cy, 9.0f, gfx::mix(quiet, ink, focus));
    const float text_x = field.x + (line ? 38.0f : 56.0f);
    float right = field.x + field.w - (line ? 4.0f : 16.0f);

    // The clear button: a small plate that fills with the primary colour and
    // grows when the focus is on it.
    const float shown_clear = tween::clamp01(clear_shown_.value);
    if (shown_clear > 0.01f)
    {
        const float on = tween::clamp01(clear_focus_.value);
        const float cx = right - kClearSize * 0.5f;
        const float half = kClearSize * 0.5f * (1.0f + 0.14f * on);
        const Rect plate{cx - half, cy - half, 2.0f * half, 2.0f * half};
        list.push_opacity(shown_clear);
        paint.fill(plate, paint.control_radius(plate) < 3.0f ? 0.0f : half,
                   gfx::mix(quiet.with_alpha(0.22f), theme.primary, on));
        const Color cross = gfx::mix(ink, theme.on_primary, on);
        list.line(cx - 5.0f, cy - 5.0f, cx + 5.0f, cy + 5.0f, 2.6f, cross);
        list.line(cx + 5.0f, cy - 5.0f, cx - 5.0f, cy + 5.0f, 2.6f, cross);
        list.pop_opacity();
    }
    if (has_clear())
        right -= kClearSize + 10.0f;
    spinner_.draw(canvas);
    right -= kSpinnerSize + 10.0f;

    const float room = std::max(right - text_x, 20.0f);
    const float baseline = cy + style.text_size * 0.35f;
    float end = 0.0f;
    if (!text_.empty())
    {
        end = paint.body(shown(paint, room), text_x, baseline, style.text_size, ink);
    }
    else if (!placeholder_.empty())
    {
        // It steps aside for the caret instead of sitting under it.
        paint.bounded_body(placeholder_, text_x + 12.0f * focus, baseline, style.text_size, quiet, room - 12.0f);
    }
    const bool lit = style.caret_period <= 0.0f || style.reduced_motion ||
                     std::fmod(blink_, style.caret_period) < style.caret_period * 0.5f;
    if (active_ && focus_ == kFocusField && lit)
    {
        const Color caret =
            line ? theme.primary
                 : (white || theme.style == SurfaceStyle::pixel ? ink : theme.accent);
        list.rounded_rect(
            {text_x + end + 3.0f, cy - style.text_size * 0.62f, 2.5f, style.text_size * 1.24f},
            0.0f, caret);
    }
    if (!line && !under)
        focus_frame(canvas, theme, field, radius, ring, true);
}

void SearchField::draw_list(Canvas &canvas, Painter &paint) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    const float open = tween::clamp01(open_.value);
    Rect panel = list_rect();
    panel.h = height_.value;
    if (open <= 0.01f || panel.h < 4.0f)
        return;
    const bool still = style.reduced_motion;
    const bool on_page = !style.list_panel && style.on_page;
    const Color base_ink = on_page ? paint.page_text() : theme.text;
    const Color base_quiet = on_page ? paint.page_text_muted() : theme.text_muted;

    list.push_opacity(open);
    list.push_transform(1.0f, 0.0f, 0.0f, 0.0f, still ? 0.0f : -8.0f * (1.0f - open));
    if (style.list_panel)
        draw_overlay_panel(canvas, theme, panel, true, 0.62f, std::min(theme.radius_card, 18.0f));
    // The rows are cut by the panel while its height follows their number.
    list.push_clip(panel.inset(-8.0f));

    if (has_header())
        paint.label(style.recent_title, panel.x + style.list_padding + kRowInset,
                    panel.y + style.list_padding + style.header_height * 0.5f +
                        style.header_size * 0.35f,
                    style.header_size, base_quiet);

    const float in_list = tween::clamp01(list_focus_.value);
    highlight_.draw(canvas, style, style.highlight, in_list);

    const int rows = shown_rows();
    for (int i = 0; i < rows; ++i)
    {
        const Suggestion &row = rows_[static_cast<std::size_t>(i)];
        const Rect rect = row_rect(i);
        if (rect.y > panel.y + panel.h)
            break;
        const float in = still ? tween::cubic_out(results_age_ / 0.16f)
                               : tween::stagger(results_age_, i, 0.03f, 0.24f);
        const float focus = highlight_.coverage(rect) * in_list;
        const Color strong = Highlight::text_color(style, style.highlight, focus, base_ink);
        Color soft = Highlight::text_color(style, style.highlight, focus, base_quiet);
        if (style.highlight.kind == HighlightKind::fill)
            soft = gfx::mix(soft, strong.with_alpha(0.72f), focus);
        list.push_opacity(in);
        list.push_transform(1.0f, 0.0f, 0.0f, 0.0f, still ? 0.0f : 8.0f * (1.0f - in));

        float x = rect.x + kRowInset;
        float right = rect.x + rect.w - kRowInset;
        if (style.row_icons)
        {
            if (showing_recent_)
                draw_clock(list, x + 9.0f, rect.cy(), 8.5f, soft);
            else
                draw_magnifier(list, x + 9.0f, rect.cy(), 6.5f, soft);
            x += kIconRoom;
        }
        if (!row.detail.empty())
        {
            right -= paint.bounded_body(row.detail, right, rect.cy() + style.detail_size * 0.35f, style.detail_size, soft, rect.w * 0.4f, gfx::Align::right);
            right -= 16.0f;
        }

        const float baseline = rect.cy() + style.row_size * 0.35f;
        const std::string cut =
            fit_body(paint, row.text, style.row_size, std::max(right - x, 24.0f));
        const std::size_t at = showing_recent_ ? std::string_view::npos : find_match(cut, text_);
        if (at == std::string_view::npos)
        {
            paint.body(cut, x, baseline, style.row_size, strong);
        }
        else
        {
            // Before, match, after: the match is what the player typed, so
            // it is the part set at full strength.
            const std::string_view whole(cut);
            x += paint.body(whole.substr(0, at), x, baseline, style.row_size, soft);
            const float width =
                paint.body(whole.substr(at, text_.size()), x, baseline, style.row_size, strong);
            if (style.match_underline)
                list.rounded_rect(
                    {x, baseline + 5.0f, width, 2.5f}, 0.0f,
                    gfx::mix(theme.accent, strong,
                             style.highlight.kind == HighlightKind::fill ? focus : 0.0f));
            x += width;
            paint.body(whole.substr(at + text_.size()), x, baseline, style.row_size, soft);
        }
        list.pop_transform();
        list.pop_opacity();
    }

    if (rows == 0 && asked_ && !text_.empty())
    {
        const Rect rect = row_rect(0);
        list.push_opacity(tween::cubic_out(results_age_ / 0.2f));
        paint.bounded_body(style.empty_text, rect.x + kRowInset, rect.cy() + style.row_size * 0.35f, style.row_size, base_quiet, rect.w - 2.0f * kRowInset);
        list.pop_opacity();
    }

    list.pop_clip();
    list.pop_transform();
    list.pop_opacity();
}

void SearchField::draw(Canvas &canvas) const
{
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, style.theme, canvas.glass);
    const float nudge = style.reduced_motion ? 0.0f : shake(shake_.value, canvas.time, 9.0f);
    list.push_transform(1.0f, 0.0f, 0.0f, nudge, 0.0f);
    draw_field(canvas, paint);
    list.pop_transform();
    draw_list(canvas, paint);
}

} // namespace hui::ui
