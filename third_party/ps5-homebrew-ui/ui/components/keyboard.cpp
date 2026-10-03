// ps5-homebrew-ui - Component: Keyboard.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/keyboard.hpp"

#include "ui/components/action_glyph.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace hui::ui
{

using gfx::Color;
using gfx::Rect;

namespace
{

// The key size preferred_width() and preferred_height() assume when the style
// lets the bounds decide.
constexpr float kDefaultKeyWidth = 72.0f;
constexpr float kDefaultKeyHeight = 64.0f;
constexpr float kLayoutFade = 0.18f; // seconds the keys of a new layout take to appear
constexpr float kPartGap = 10.0f;    // between the glyph, the symbol and the word of a wide key

int characters(std::string_view text)
{
    int count = 0;
    for (char c : text)
        count += (static_cast<unsigned char>(c) & 0xc0) == 0x80 ? 0 : 1;
    return count;
}

KeyboardKey wide(KeyKind kind, int span)
{
    KeyboardKey key;
    key.kind = kind;
    key.span = span;
    if (kind == KeyKind::space)
        key.text = " ";
    return key;
}

KeyboardKey text_key(const char *text, int span = 1)
{
    KeyboardKey key;
    key.text = text;
    key.span = span;
    return key;
}

// Painter::label sets a theme's labels in capitals where the theme asks for
// them. A key cap must show the case it types, so caps are set in the label
// face with the same sizing rules and no case change.
float cap_text(Canvas &canvas, Painter &paint, std::string_view value, float x, float baseline,
               float size, Color color, gfx::Align align)
{
    const Theme &theme = paint.theme();
    if (theme.label == FontRole::pixel)
    {
        if (canvas.fonts.pixel.font == nullptr)
            return paint.label(value, x, baseline, size, color, align);
        return text(canvas.list, canvas.fonts.pixel, value, x, baseline - size * 0.04f,
                    Painter::pixel_em(size * 0.62f), color, align);
    }
    const float scale = theme.label == FontRole::hand ? 1.18f : 1.0f;
    return text(canvas.list, paint.font(theme.label), value, x, baseline, size * scale, color,
                align);
}

float cap_width(const Canvas &canvas, const Painter &paint, std::string_view value, float size)
{
    const Theme &theme = paint.theme();
    if (theme.label == FontRole::pixel)
    {
        if (canvas.fonts.pixel.font == nullptr)
            return paint.label_width(value, size);
        return canvas.fonts.pixel.measure(value, Painter::pixel_em(size * 0.62f));
    }
    const float scale = theme.label == FontRole::hand ? 1.18f : 1.0f;
    return paint.font(theme.label).measure(value, size * scale);
}

struct KeyPaint
{
    Color fill;
    Color edge;
    Color ink;
    float border = -1.0f;
};

// The recipe of Painter::button: its primary kind for the Done key and its
// secondary kind for every other, so a key is a button of the theme.
KeyPaint key_paint(const Theme &theme, bool primary)
{
    const bool framed = theme.style == SurfaceStyle::hard || theme.style == SurfaceStyle::pixel ||
                        theme.style == SurfaceStyle::bevel;
    const float line = theme.button_border < 0.0f ? theme.border : theme.button_border;
    if (primary)
    {
        // On lit consoles the edge carries the colour, not the fill.
        if (theme.style == SurfaceStyle::glow)
            return {theme.surface, theme.primary, theme.primary, -1.0f};
        const bool sketch = theme.style == SurfaceStyle::sketch;
        return {theme.primary, framed ? theme.outline : (sketch ? theme.on_primary : theme.primary),
                theme.on_primary, framed || sketch ? theme.border : line};
    }
    const bool hollow = theme.secondary.a <= 0.01f;
    return {theme.secondary, hollow ? theme.on_secondary : theme.outline, theme.on_secondary,
            hollow ? std::max(theme.border, 1.5f) : (framed ? theme.border : line)};
}

// How far Painter::surface moves a surface's content when it is pressed. The
// caps are drawn in a later pass than the surfaces (the highlight lies
// between them), so the offset is worked out again here.
void pressed_offset(const Theme &theme, float press, float *dx, float *dy)
{
    *dx = 0.0f;
    *dy = 0.0f;
    switch (theme.style)
    {
    case SurfaceStyle::hard:
        *dx = *dy = theme.shadow_offset * press;
        break;
    case SurfaceStyle::bevel:
        *dx = *dy = press > 0.5f ? 2.0f : 0.0f;
        break;
    case SurfaceStyle::sketch:
        *dy = 2.0f * press;
        break;
    default:
        break;
    }
}

// The symbols of the wide keys, drawn from strokes so every face has them.
void draw_icon(gfx::DrawList &list, KeyKind kind, float cx, float cy, float s, Color ink,
               float armed, float locked)
{
    const float pen = std::max(2.5f, s * 0.11f);
    switch (kind)
    {
    case KeyKind::shift:
    {
        // Outline: off. Filled: the next letter. Filled with a bar: locked.
        const Rect head{cx - s * 0.42f, cy - s * 0.5f, s * 0.84f, s * 0.54f};
        list.triangle(head, ink, pen);
        list.triangle(head, ink.with_alpha(armed));
        list.line(cx, cy + s * 0.08f, cx, cy + s * 0.26f, s * 0.2f,
                  ink.with_alpha(0.4f + 0.6f * armed));
        list.line(cx - s * 0.3f, cy + s * 0.5f, cx + s * 0.3f, cy + s * 0.5f, pen,
                  ink.with_alpha(locked));
        break;
    }
    case KeyKind::backspace:
        list.line(cx - s * 0.5f, cy, cx + s * 0.5f, cy, pen, ink);
        list.line(cx - s * 0.5f, cy, cx - s * 0.16f, cy - s * 0.32f, pen, ink);
        list.line(cx - s * 0.5f, cy, cx - s * 0.16f, cy + s * 0.32f, pen, ink);
        break;
    case KeyKind::space:
        list.line(cx - s * 0.55f, cy + s * 0.22f, cx + s * 0.55f, cy + s * 0.22f, pen, ink);
        list.line(cx - s * 0.55f, cy + s * 0.22f, cx - s * 0.55f, cy - s * 0.04f, pen, ink);
        list.line(cx + s * 0.55f, cy + s * 0.22f, cx + s * 0.55f, cy - s * 0.04f, pen, ink);
        break;
    case KeyKind::done:
        list.line(cx - s * 0.42f, cy + s * 0.02f, cx - s * 0.12f, cy + s * 0.3f, pen, ink);
        list.line(cx - s * 0.12f, cy + s * 0.3f, cx + s * 0.44f, cy - s * 0.3f, pen, ink);
        break;
    default:
        break;
    }
}

} // namespace

// ---- layouts ----------------------------------------------------------------

KeyboardLayout &KeyboardLayout::add_row(std::string_view characters)
{
    std::vector<KeyboardKey> row;
    for (char c : characters)
    {
        KeyboardKey key;
        key.text = std::string(1, c);
        if (c >= 'a' && c <= 'z')
            key.shifted = std::string(1, static_cast<char>(c - 'a' + 'A'));
        row.push_back(std::move(key));
    }
    rows.push_back(std::move(row));
    return *this;
}

KeyboardLayout &KeyboardLayout::add_row(std::vector<KeyboardKey> keys)
{
    rows.push_back(std::move(keys));
    return *this;
}

KeyboardLayout KeyboardLayout::letters()
{
    KeyboardLayout layout;
    layout.name = "abc";
    layout.columns = 10;
    layout.add_row("1234567890").add_row("qwertyuiop").add_row("asdfghjkl'").add_row("zxcvbnm,.?");
    // Punctuation doubles up under shift, as on a physical keyboard.
    layout.rows[2][9].shifted = "\"";
    layout.rows[3][7].shifted = ";";
    layout.rows[3][8].shifted = ":";
    layout.rows[3][9].shifted = "!";
    layout.add_row({wide(KeyKind::shift, 2), wide(KeyKind::layout, 1), wide(KeyKind::space, 3),
                    wide(KeyKind::backspace, 2), wide(KeyKind::done, 2)});
    return layout;
}

KeyboardLayout KeyboardLayout::symbols()
{
    KeyboardLayout layout;
    layout.name = "#+=";
    layout.columns = 10;
    layout.add_row("1234567890").add_row("!@#$%^&*()").add_row("-_=+[]{}<>");
    layout.add_row("/\\|:;\"'~`?");
    layout.add_row({wide(KeyKind::layout, 3), wide(KeyKind::space, 3), wide(KeyKind::backspace, 2),
                    wide(KeyKind::done, 2)});
    return layout;
}

KeyboardLayout KeyboardLayout::numeric()
{
    KeyboardLayout layout;
    layout.name = "123";
    layout.columns = 3;
    layout.add_row("123").add_row("456").add_row("789");
    layout.add_row({wide(KeyKind::backspace, 1), text_key("0"), wide(KeyKind::done, 1)});
    return layout;
}

KeyboardLayout KeyboardLayout::email()
{
    KeyboardLayout layout;
    layout.name = "abc";
    layout.columns = 10;
    layout.add_row("1234567890").add_row("qwertyuiop").add_row("asdfghjkl@").add_row("zxcvbnm._-");
    layout.add_row({wide(KeyKind::shift, 2), wide(KeyKind::layout, 1), wide(KeyKind::space, 2),
                    text_key(".com"), wide(KeyKind::backspace, 2), wide(KeyKind::done, 2)});
    return layout;
}

// ---- the component ----------------------------------------------------------

Keyboard::Keyboard()
{
    set_layouts({KeyboardLayout::letters(), KeyboardLayout::symbols()});
}

void Keyboard::set_layouts(std::vector<KeyboardLayout> layouts)
{
    layouts_ = std::move(layouts);
    for (KeyboardLayout &layout : layouts_)
    {
        // A row without keys has nothing to focus: drop it here once instead
        // of guarding every loop.
        std::erase_if(layout.rows, [](const std::vector<KeyboardKey> &row) { return row.empty(); });
        layout.columns = std::max(layout.columns, 1);
    }
    std::erase_if(layouts_, [](const KeyboardLayout &layout) { return layout.rows.empty(); });
    layout_ = std::clamp(layout_, 0, std::max(static_cast<int>(layouts_.size()) - 1, 0));
    layout_age_ = 0.0f;
    relocate();
    retarget(true);
}

void Keyboard::set_layout(int index)
{
    if (layouts_.empty())
        return;
    const int next = std::clamp(index, 0, static_cast<int>(layouts_.size()) - 1);
    if (next == layout_)
        return;
    layout_ = next;
    layout_age_ = 0.0f;
    relocate();
    retarget(false);
}

void Keyboard::set_length(int length)
{
    length_ = length;
}

const KeyboardLayout &Keyboard::current() const
{
    return layouts_[static_cast<std::size_t>(layout_)];
}

bool Keyboard::empty() const
{
    return layouts_.empty();
}

const KeyboardKey &Keyboard::key_at(int row, int key) const
{
    return current().rows[static_cast<std::size_t>(row)][static_cast<std::size_t>(key)];
}

const KeyboardKey &Keyboard::focused() const
{
    static const KeyboardKey kNone;
    return empty() ? kNone : key_at(row_, key_);
}

void Keyboard::set_bounds(const Rect &bounds)
{
    // A parent may place the keyboard every frame: only a real change snaps
    // the highlight, or it would never glide.
    const bool changed = bounds.x != bounds_.x || bounds.y != bounds_.y || bounds.w != bounds_.w ||
                         bounds.h != bounds_.h;
    bounds_ = bounds;
    if (changed)
        retarget(true);
}

float Keyboard::preferred_width() const
{
    const float columns = empty() ? 10.0f : static_cast<float>(current().columns);
    const float key = style.key_width > 0.0f ? style.key_width : kDefaultKeyWidth;
    return columns * key + (columns - 1.0f) * style.gap +
           (style.panel ? 2.0f * style.panel_padding : 0.0f);
}

float Keyboard::preferred_height() const
{
    const float rows = empty() ? 5.0f : static_cast<float>(current().rows.size());
    const float key = style.key_height > 0.0f ? style.key_height : kDefaultKeyHeight;
    return rows * key + (rows - 1.0f) * style.gap +
           (style.panel ? 2.0f * style.panel_padding : 0.0f);
}

void Keyboard::enter()
{
    age_ = 0.0f;
}

Keyboard::Metrics Keyboard::metrics() const
{
    Metrics m;
    const Rect inner = style.panel ? bounds_.inset(style.panel_padding) : bounds_;
    const float columns = empty() ? 1.0f : static_cast<float>(current().columns);
    const float rows = empty() ? 1.0f : static_cast<float>(current().rows.size());
    m.unit_w = style.key_width > 0.0f ? style.key_width
                                      : (inner.w - (columns - 1.0f) * style.gap) / columns;
    m.unit_h =
        style.key_height > 0.0f ? style.key_height : (inner.h - (rows - 1.0f) * style.gap) / rows;
    m.unit_w = std::max(m.unit_w, 8.0f);
    m.unit_h = std::max(m.unit_h, 8.0f);
    const float w = columns * m.unit_w + (columns - 1.0f) * style.gap;
    const float h = rows * m.unit_h + (rows - 1.0f) * style.gap;
    m.board = {inner.cx() - w * 0.5f, inner.cy() - h * 0.5f, w, h};
    return m;
}

float Keyboard::row_start(int row) const
{
    int used = 0;
    for (const KeyboardKey &key : current().rows[static_cast<std::size_t>(row)])
        used += std::max(key.span, 1);
    return std::max(static_cast<float>(current().columns - used) * 0.5f, 0.0f);
}

float Keyboard::key_start(int row, int key) const
{
    const std::vector<KeyboardKey> &keys = current().rows[static_cast<std::size_t>(row)];
    int before = 0;
    for (int i = 0; i < key; ++i)
        before += std::max(keys[static_cast<std::size_t>(i)].span, 1);
    return row_start(row) + static_cast<float>(before);
}

float Keyboard::key_centre(int row, int key) const
{
    return key_start(row, key) + static_cast<float>(std::max(key_at(row, key).span, 1)) * 0.5f;
}

// The key of a row that covers a column, or the nearest one.
int Keyboard::key_under(int row, float column) const
{
    const int count = static_cast<int>(current().rows[static_cast<std::size_t>(row)].size());
    int nearest = 0;
    float distance = 1.0e9f;
    for (int i = 0; i < count; ++i)
    {
        const float start = key_start(row, i);
        const float span = static_cast<float>(std::max(key_at(row, i).span, 1));
        if (column >= start && column < start + span)
            return i;
        const float away = std::fabs(start + span * 0.5f - column);
        if (away < distance)
        {
            distance = away;
            nearest = i;
        }
    }
    return nearest;
}

bool Keyboard::find_kind(KeyKind kind, int *row, int *key) const
{
    if (empty())
        return false;
    const auto &rows = current().rows;
    for (std::size_t r = 0; r < rows.size(); ++r)
    {
        for (std::size_t k = 0; k < rows[r].size(); ++k)
        {
            if (rows[r][k].kind == kind)
            {
                *row = static_cast<int>(r);
                *key = static_cast<int>(k);
                return true;
            }
        }
    }
    return false;
}

Rect Keyboard::key_rect(int row, int key) const
{
    if (empty())
        return bounds_;
    const Metrics m = metrics();
    const float pitch = m.unit_w + style.gap;
    const float span = static_cast<float>(std::max(key_at(row, key).span, 1));
    return {m.board.x + key_start(row, key) * pitch,
            m.board.y + static_cast<float>(row) * (m.unit_h + style.gap), span * pitch - style.gap,
            m.unit_h};
}

float Keyboard::key_radius(const Painter &paint, const Rect &r) const
{
    const float half = std::min(r.w, r.h) * 0.5f;
    switch (style.radius_source)
    {
    case RadiusSource::pill:
        return half;
    case RadiusSource::square:
        return 0.0f;
    case RadiusSource::custom:
        return std::min(style.radius, half);
    default:
        return paint.control_radius(r);
    }
}

// Keeps the focus on a key that exists after the layout changed.
void Keyboard::relocate()
{
    if (empty())
    {
        row_ = key_ = 0;
        return;
    }
    row_ = std::clamp(row_, 0, static_cast<int>(current().rows.size()) - 1);
    key_ = key_under(row_, column_);
}

void Keyboard::retarget(bool snap)
{
    if (empty())
        return;
    const Rect target = key_rect(row_, key_);
    highlight_.target(target);
    if (snap)
        highlight_.snap(target);
}

void Keyboard::set_focus(int row, int key, bool snap)
{
    if (empty())
        return;
    row_ = std::clamp(row, 0, static_cast<int>(current().rows.size()) - 1);
    const int count = static_cast<int>(current().rows[static_cast<std::size_t>(row_)].size());
    key_ = std::clamp(key, 0, count - 1);
    column_ = key_centre(row_, key_);
    retarget(snap);
}

void Keyboard::press(int row, int key)
{
    press_row_ = row;
    press_key_ = key;
    press_.trigger();
}

Event Keyboard::move(const InputFrame &input, Feedback &feedback)
{
    const auto &rows = current().rows;
    const Rect from = key_rect(row_, key_);
    if (input.nav == Direction::left || input.nav == Direction::right)
    {
        const bool right = input.nav == Direction::right;
        const int count = static_cast<int>(rows[static_cast<std::size_t>(row_)].size());
        int next = key_ + (right ? 1 : -1);
        bool wrapped = false;
        if (next < 0 || next >= count)
        {
            if (style.exits.allows(input.nav))
            {
                exit_ = input.nav;
                return Event::none;
            }
            // A held direction stops at the edge instead of circling forever.
            if (!style.wrap || count < 2 || input.nav_repeat)
                return refuse(feedback, style, input, highlight_.refusal(), from.cx());
            next = right ? 0 : count - 1;
            wrapped = true;
        }
        key_ = next;
        column_ = key_centre(row_, key_);
        if (wrapped && !style.reduced_motion)
        {
            // The old highlight slides out through the edge it was pushed
            // against while the real one enters from behind the other.
            const float way = right ? 1.0f : -1.0f;
            const float margin = style.gap + style.panel_padding;
            ghost_ = highlight_;
            ghost_.target({from.x + way * (from.w + margin), from.y, from.w, from.h});
            const Rect to = key_rect(row_, key_);
            highlight_.snap({to.x - way * (to.w + margin), to.y, to.w, to.h});
            highlight_.target(to);
            wrap_.trigger();
        }
        else
        {
            retarget(false);
        }
    }
    else
    {
        const int next = row_ + (input.nav == Direction::down ? 1 : -1);
        if (next < 0 || next >= static_cast<int>(rows.size()))
        {
            if (style.exits.allows(input.nav))
            {
                exit_ = input.nav;
                return Event::none;
            }
            return refuse(feedback, style, input, highlight_.refusal(), from.cx());
        }
        // column_ is where the player really is: a wide key covers several
        // columns, and going back up returns to the one they left.
        const float column = style.column_memory ? column_ : key_centre(row_, key_);
        row_ = next;
        key_ = key_under(row_, column);
        if (!style.column_memory)
            column_ = key_centre(row_, key_);
        retarget(false);
    }
    // Rows sound like steps: lower rows, lower pitch.
    play_cue(feedback, style, style.sounds.move, key_rect(row_, key_).cx(),
             1.1f - 0.035f * static_cast<float>(row_));
    return Event::moved;
}

Event Keyboard::emit(std::string_view utf8, int row, int key, bool capital, Feedback &feedback)
{
    const float x = row >= 0 ? key_rect(row, key).cx() : bounds_.cx();
    if (style.max_length > 0 && length_ >= style.max_length)
        return refuse(feedback, style, InputFrame{}, highlight_.refusal(), x);
    if (on_text)
        on_text(utf8);
    typed_.append(utf8);
    if (length_ >= 0)
        length_ += characters(utf8);
    float pitch = 1.0f;
    if (style.pitch_by_column && row >= 0 && current().columns > 1)
        pitch = tween::lerp(
            0.96f, 1.05f,
            tween::clamp01(key_centre(row, key) / static_cast<float>(current().columns)));
    // Capitals sound a touch higher.
    play_cue(feedback, style, style.type, x, capital ? pitch * 1.06f : pitch);
    return Event::changed;
}

// repeat: this deletion comes from a held button, not from a press.
Event Keyboard::erase(bool repeat, Feedback &feedback)
{
    int row = 0;
    int key = 0;
    const bool has_key = find_kind(KeyKind::backspace, &row, &key);
    const float x = has_key ? key_rect(row, key).cx() : bounds_.cx();
    if (length_ == 0)
    {
        InputFrame quiet;
        quiet.nav_repeat = repeat;
        return refuse(feedback, style, quiet, highlight_.refusal(), x);
    }
    if (has_key)
        press(row, key);
    if (on_backspace)
        on_backspace();
    ++erased_;
    if (length_ > 0)
        --length_;
    play_cue(feedback, style, style.erase, x);
    return Event::changed;
}

Event Keyboard::backspace(Feedback &feedback)
{
    return erase(false, feedback);
}

Event Keyboard::space(Feedback &feedback)
{
    int row = -1;
    int key = -1;
    if (find_kind(KeyKind::space, &row, &key))
        press(row, key);
    return emit(" ", row, key, false, feedback);
}

Event Keyboard::done(Feedback &feedback)
{
    int row = 0;
    int key = 0;
    const bool has_key = find_kind(KeyKind::done, &row, &key);
    if (has_key)
        press(row, key);
    play_cue(feedback, style, style.sounds.activate, has_key ? key_rect(row, key).cx() : -1.0f);
    if (style.sounds.rumble > 0.0f)
        feedback.rumble(0.3f * style.sounds.rumble, 0.04f);
    if (on_done)
        on_done();
    return Event::activated;
}

void Keyboard::toggle_shift(Feedback &feedback)
{
    int row = 0;
    int key = 0;
    const bool has_key = find_kind(KeyKind::shift, &row, &key);
    if (has_key)
        press(row, key);
    const float x = has_key ? key_rect(row, key).cx() : -1.0f;
    switch (shift_)
    {
    case KeyboardShift::off:
        shift_ = KeyboardShift::once;
        play_cue(feedback, style, style.sounds.change, x, 1.0f);
        break;
    case KeyboardShift::once:
        shift_ = style.shift_lock ? KeyboardShift::lock : KeyboardShift::off;
        play_cue(feedback, style, style.sounds.change, x, style.shift_lock ? 1.12f : 0.9f);
        break;
    case KeyboardShift::lock:
        shift_ = KeyboardShift::off;
        play_cue(feedback, style, style.sounds.change, x, 0.9f);
        break;
    }
}

void Keyboard::cycle_layout(Feedback &feedback)
{
    if (empty())
        return;
    if (layouts_.size() < 2)
    {
        refuse(feedback, style, InputFrame{}, highlight_.refusal(), bounds_.cx());
        return;
    }
    const bool on_key = focused().kind == KeyKind::layout;
    set_layout((layout_ + 1) % static_cast<int>(layouts_.size()));
    shift_ = KeyboardShift::off;
    int row = 0;
    int key = 0;
    if (find_kind(KeyKind::layout, &row, &key))
    {
        // The player who pressed the switch key finds it under the focus
        // again, wherever the new layout keeps it.
        if (on_key)
            set_focus(row, key, false);
        press(row, key);
    }
    play_cue(feedback, style, style.sounds.page, bounds_.cx());
}

Event Keyboard::activate(int row, int key, Feedback &feedback)
{
    const KeyboardKey &pressed = key_at(row, key);
    press(row, key);
    switch (pressed.kind)
    {
    case KeyKind::character:
    {
        const bool has_shift = !pressed.shifted.empty() && pressed.shifted != pressed.text;
        const bool capital = has_shift && shifted();
        const Event event =
            emit(capital ? pressed.shifted : pressed.text, row, key, capital, feedback);
        // A shift for one letter is spent by a letter, not by a digit.
        if (event == Event::changed && has_shift && shift_ == KeyboardShift::once)
            shift_ = KeyboardShift::off;
        return event;
    }
    case KeyKind::space:
        return emit(" ", row, key, false, feedback);
    case KeyKind::shift:
        toggle_shift(feedback);
        return Event::none;
    case KeyKind::backspace:
        return erase(false, feedback);
    case KeyKind::layout:
        cycle_layout(feedback);
        return Event::none;
    case KeyKind::done:
        return done(feedback);
    }
    return Event::none;
}

Event Keyboard::tap(std::string_view utf8, Feedback &feedback)
{
    typed_.clear();
    erased_ = 0;
    exit_ = Direction::none;
    if (empty())
        return Event::none;

    int row = 0;
    int key = 0;
    if (utf8 == "\b" || utf8 == "\n" || utf8 == " ")
    {
        const KeyKind kind = utf8 == "\b"   ? KeyKind::backspace
                             : utf8 == "\n" ? KeyKind::done
                                            : KeyKind::space;
        if (find_kind(kind, &row, &key))
        {
            set_focus(row, key, false);
            return activate(row, key, feedback);
        }
        if (kind == KeyKind::backspace)
            return erase(false, feedback);
        return kind == KeyKind::done ? done(feedback) : emit(" ", -1, -1, false, feedback);
    }

    // The current layout first, as typed before as shifted; then the others.
    const int count = static_cast<int>(layouts_.size());
    for (int step = 0; step < count; ++step)
    {
        const int index = (layout_ + step) % count;
        const auto &rows = layouts_[static_cast<std::size_t>(index)].rows;
        for (int want_shift = 0; want_shift < 2; ++want_shift)
        {
            for (std::size_t r = 0; r < rows.size(); ++r)
            {
                for (std::size_t k = 0; k < rows[r].size(); ++k)
                {
                    const KeyboardKey &candidate = rows[r][k];
                    if (candidate.kind != KeyKind::character)
                        continue;
                    const bool has_shift =
                        !candidate.shifted.empty() && candidate.shifted != candidate.text;
                    if (want_shift == 0 ? candidate.text != utf8
                                        : (!has_shift || candidate.shifted != utf8))
                        continue;
                    set_layout(index);
                    if (want_shift != 0 && shift_ == KeyboardShift::off)
                        shift_ = KeyboardShift::once;
                    else if (want_shift == 0 && has_shift)
                        shift_ = KeyboardShift::off;
                    set_focus(static_cast<int>(r), static_cast<int>(k), false);
                    return activate(row_, key_, feedback);
                }
            }
        }
    }
    // No key types it: it goes out all the same.
    return emit(utf8, -1, -1, false, feedback);
}

Event Keyboard::handle(const InputFrame &input, Feedback &feedback)
{
    typed_.clear();
    erased_ = 0;
    exit_ = Direction::none;
    if (empty())
        return Event::none;

    const KeyboardBindings &bound = style.bindings;
    const auto pressed = [&](Action action)
    { return action != Action::count && input.is_pressed(action); };
    const auto held = [&](Action action)
    { return action != Action::count && input.is_held(action); };

    if (input.is_pressed(Action::back))
    {
        play_cue(feedback, style, style.sounds.cancel, bounds_.cx());
        return Event::cancelled;
    }

    Event result = Event::none;
    if (input.nav != Direction::none)
        result = move(input, feedback);

    // The shortcuts press their key on screen too, so the player learns
    // which key each button stands for.
    if (pressed(bound.shift))
        toggle_shift(feedback);
    if (pressed(bound.layout))
        cycle_layout(feedback);
    if (pressed(bound.space))
        result = space(feedback);

    // Backspace repeats while it is held, from its shortcut or from confirm
    // on its key. The input model repeats directions only, so the time is
    // counted in update() and spent here.
    const bool confirm = input.is_pressed(Action::confirm);
    const bool on_erase = focused().kind == KeyKind::backspace;
    if (pressed(bound.backspace) || (on_erase && confirm))
    {
        result = erase(false, feedback);
        hold_ = 0.0f;
        holding_ = true;
    }
    else if (held(bound.backspace) || (on_erase && input.is_held(Action::confirm)))
    {
        const float interval = std::max(style.repeat_interval, 0.02f);
        while (hold_ >= style.repeat_delay)
        {
            hold_ -= interval;
            result = erase(true, feedback);
        }
        holding_ = true;
    }
    if (confirm && !on_erase)
        result = activate(row_, key_, feedback);
    if (pressed(bound.done))
        result = done(feedback);
    return result;
}

void Keyboard::update(float dt)
{
    age_ += dt;
    layout_age_ += dt;
    hold_ = holding_ ? hold_ + dt : 0.0f;
    holding_ = false;

    // A text that starts gets a capital without the player asking, once: if
    // they turn shift off again it stays off.
    if (length_ != 0)
    {
        armed_for_empty_ = false;
    }
    else if (style.auto_capital && !armed_for_empty_)
    {
        if (shift_ == KeyboardShift::off)
            shift_ = KeyboardShift::once;
        armed_for_empty_ = true;
    }

    // The knobs may have changed since the last frame: follow them.
    retarget(false);
    highlight_.update(dt, style);
    ghost_.update(dt, style);
    wrap_.update(dt, 13.0f);
    press_.update(dt, 14.0f);
    shift_blend_.target = shifted() ? 1.0f : 0.0f;
    shift_blend_.update(dt, 22.0f);
    lock_blend_.target = shift_ == KeyboardShift::lock ? 1.0f : 0.0f;
    lock_blend_.update(dt, 22.0f);
    active_amount_.target = active_ ? 1.0f : 0.0f;
    active_amount_.update(dt, 18.0f);
}

void Keyboard::draw_cap(Canvas &canvas, Painter &paint, const KeyboardKey &key, const Rect &rect,
                        Color ink) const
{
    gfx::DrawList &list = canvas.list;
    if (key.kind == KeyKind::character)
    {
        const auto cap = [&](std::string_view value, float alpha)
        {
            if (alpha <= 0.01f || value.empty())
                return;
            // A key that types a word (".com") sets it as large as fits.
            const float wide = cap_width(canvas, paint, value, style.label_size);
            const float room = std::max(rect.w - 12.0f, 8.0f);
            const float size = wide > room ? style.label_size * room / wide : style.label_size;
            cap_text(canvas, paint, value, rect.cx(), rect.cy() + size * 0.35f, size,
                     ink.with_alpha(alpha), gfx::Align::center);
        };
        if (!key.label.empty())
        {
            cap(key.label, 1.0f);
        }
        else if (!key.shifted.empty() && key.shifted != key.text)
        {
            // Both cases are drawn; shift cross-fades between them.
            const float upper = tween::clamp01(shift_blend_.value);
            cap(key.text, 1.0f - upper);
            cap(key.shifted, upper);
        }
        else
        {
            cap(key.text, 1.0f);
        }
        return;
    }

    // A wide key: [controller glyph] [symbol] [word], centred as one group.
    // What does not fit goes in that order: the word, then the glyph.
    Action bound = Action::count;
    std::string word;
    bool symbol = true;
    switch (key.kind)
    {
    case KeyKind::space:
        bound = style.bindings.space;
        word = style.space_label;
        break;
    case KeyKind::shift:
        bound = style.bindings.shift;
        break;
    case KeyKind::backspace:
        bound = style.bindings.backspace;
        break;
    case KeyKind::done:
        bound = style.bindings.done;
        word = style.done_label;
        break;
    default:
        bound = style.bindings.layout;
        // It names where it leads.
        word = layouts_[static_cast<std::size_t>((layout_ + 1) % static_cast<int>(layouts_.size()))]
                   .name;
        symbol = false;
        break;
    }
    if (!key.label.empty())
        word = key.label;
    if (symbol && !style.wide_labels && key.label.empty())
        word.clear();

    const float room = std::max(rect.w - 16.0f, 8.0f);
    float glyph_w = style.binding_glyphs && bound != Action::count
                        ? action_glyph_width(bound, style.glyph_size)
                        : 0.0f;
    const float symbol_w = symbol ? style.icon_size * 1.2f : 0.0f;
    float word_size = style.wide_label_size;
    float word_w = word.empty() ? 0.0f : paint.label_width(word, word_size);
    const auto total = [&]()
    {
        float sum = 0.0f;
        int parts = 0;
        for (float part : {glyph_w, symbol_w, word_w})
        {
            if (part > 0.0f)
            {
                sum += part;
                ++parts;
            }
        }
        return sum + kPartGap * static_cast<float>(std::max(parts - 1, 0));
    };
    if (symbol && total() > room)
        word_w = 0.0f;
    if (total() > room)
        glyph_w = 0.0f;
    if (!symbol && word_w > room)
    {
        word_size *= room / word_w;
        word_w = paint.label_width(word, word_size);
    }

    float x = rect.cx() - total() * 0.5f;
    if (glyph_w > 0.0f)
    {
        draw_action_glyph(canvas, style.theme, bound, x, rect.cy(), style.glyph_size);
        x += glyph_w + kPartGap;
    }
    if (symbol)
    {
        draw_icon(list, key.kind, x + symbol_w * 0.5f, rect.cy(), style.icon_size, ink,
                  tween::clamp01(shift_blend_.value), tween::clamp01(lock_blend_.value));
        x += symbol_w + kPartGap;
    }
    if (word_w > 0.0f)
        paint.label(word, x, rect.cy() + word_size * 0.35f, word_size, ink);
}

void Keyboard::draw(Canvas &canvas) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    if (style.panel)
        paint.panel(bounds_);
    if (empty())
        return;

    const auto &rows = current().rows;
    const bool still = style.reduced_motion;
    const float swap = tween::cubic_out(layout_age_ / kLayoutFade);
    const auto entrance = [&](int row)
    {
        if (style.entrance_step <= 0.0f || still)
            return tween::cubic_out(age_ / 0.2f) * swap;
        return tween::stagger(age_, row, style.entrance_step, 0.34f) * swap;
    };
    // A key arrives from a little below and dips around its centre when it
    // is pressed. Returns how far it is pressed.
    const auto push_key = [&](int row, int key, const Rect &rect)
    {
        const float in = entrance(row);
        const float press = row == press_row_ && key == press_key_ ? press_.value : 0.0f;
        list.push_opacity(in);
        list.push_transform(still ? 1.0f : 1.0f - style.press_dip * press, rect.cx(), rect.cy(),
                            0.0f, still ? 0.0f : 14.0f * (1.0f - in));
        return press;
    };
    const auto pop_key = [&]()
    {
        list.pop_transform();
        list.pop_opacity();
    };

    // 1. The keys themselves.
    const float armed = tween::clamp01(shift_blend_.value);
    for (int r = 0; r < static_cast<int>(rows.size()); ++r)
    {
        const int count = static_cast<int>(rows[static_cast<std::size_t>(r)].size());
        for (int k = 0; k < count; ++k)
        {
            const KeyboardKey &key = key_at(r, k);
            const Rect rect = key_rect(r, k);
            const float radius = key_radius(paint, rect);
            const float press = push_key(r, k, rect);
            if (style.surfaces)
            {
                const KeyPaint look = key_paint(theme, key.kind == KeyKind::done);
                paint.surface(rect, radius, look.fill, look.edge, 1.0f - press, look.border);
                if (key.kind == KeyKind::shift && armed > 0.01f)
                    paint.stroke(rect, radius, std::max(theme.border, 2.5f),
                                 theme.accent.with_alpha(armed));
            }
            else if (key.kind == KeyKind::shift && armed > 0.01f)
            {
                paint.fill(rect, radius, theme.accent.with_alpha(0.2f * armed));
            }
            pop_key();
        }
    }

    // 2. The highlight. While it wraps it is clipped to the board, so the
    //    move reads as "out through this edge, in through that one".
    HighlightStyle look = style.highlight;
    const Rect focus_rect = key_rect(row_, key_);
    if (look.radius < 0.0f)
        look.radius = key_radius(paint, focus_rect);
    if (press_row_ == row_ && press_key_ == key_)
        look.grow -= 2.0f * press_.value;
    const float amount = (0.35f + 0.65f * active_amount_.value) * entrance(row_);
    const bool wrapping = wrap_.value > 0.01f;
    if (wrapping)
    {
        list.push_clip(style.panel ? bounds_ : metrics().board.inset(-style.gap));
        ghost_.draw(canvas, style, look, amount * tween::clamp01(wrap_.value));
    }
    highlight_.draw(canvas, style, look, amount);
    int done_row = 0;
    int done_key = 0;
    if (look.kind == HighlightKind::fill && style.surfaces && look.color.a <= 0.0f &&
        find_kind(KeyKind::done, &done_row, &done_key))
    {
        // The Done key already has the plate's colour: on it the focus would
        // vanish, so there the theme's ring joins the plate.
        const float cover = highlight_.coverage(key_rect(done_row, done_key));
        paint.focus_ring(highlight_.rect(canvas.time), look.radius, cover * amount);
    }
    if (wrapping)
        list.pop_clip();

    // 3. The caps, coloured by the highlight above them.
    const Color flat = style.panel ? theme.text : paint.page_text();
    Color flat_done = theme.primary;
    if (theme.style == SurfaceStyle::bevel || theme.style == SurfaceStyle::pixel)
        flat_done = flat;
    else if (theme.style == SurfaceStyle::sketch)
        flat_done = theme.on_primary;
    for (int r = 0; r < static_cast<int>(rows.size()); ++r)
    {
        const int count = static_cast<int>(rows[static_cast<std::size_t>(r)].size());
        for (int k = 0; k < count; ++k)
        {
            const KeyboardKey &key = key_at(r, k);
            Rect rect = key_rect(r, k);
            const bool is_done = key.kind == KeyKind::done;
            const Color resting =
                style.surfaces ? key_paint(theme, is_done).ink : (is_done ? flat_done : flat);
            const float cover = highlight_.coverage(rect) * active_amount_.value;
            const Color ink = Highlight::text_color(style, look, cover, resting);
            const float press = push_key(r, k, rect);
            if (style.surfaces)
            {
                float dx = 0.0f;
                float dy = 0.0f;
                pressed_offset(theme, tween::clamp01(press), &dx, &dy);
                rect.x += dx;
                rect.y += dy;
            }
            draw_cap(canvas, paint, key, rect, ink);
            pop_key();
        }
    }
}

} // namespace hui::ui
