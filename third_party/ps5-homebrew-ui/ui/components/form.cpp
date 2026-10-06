// ps5-homebrew-ui - Component: Form.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/form.hpp"

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

const std::string kNoText;

// Where a slider's value sits between its limits, 0..1.
float fraction(const FormRow &row)
{
    return tween::inverse_lerp(row.minimum, row.maximum, row.value);
}

} // namespace

// ---- building ---------------------------------------------------------------

FormRow &Form::add(FormRowKind kind, int id, std::string label)
{
    rows_.emplace_back();
    FormRow &row = rows_.back();
    row.kind = kind;
    row.id = id;
    row.label = std::move(label);
    const int index = static_cast<int>(rows_.size()) - 1;
    if (focus_ < 0 && focusable(index))
        focus_ = index;
    // The caller still sets the row's optional parts (a description changes
    // the layout), so the next update() settles everything without a glide.
    settle_ = true;
    retarget(true);
    return row;
}

FormRow &Form::add_header(std::string label)
{
    return add(FormRowKind::header, 0, std::move(label));
}

FormRow &Form::add_toggle(int id, std::string label, bool on)
{
    FormRow &row = add(FormRowKind::toggle, id, std::move(label));
    row.on = on;
    row.on_amount.snap(on ? 1.0f : 0.0f);
    return row;
}

FormRow &Form::add_choice(int id, std::string label, std::vector<std::string> options, int index)
{
    FormRow &row = add(FormRowKind::choice, id, std::move(label));
    row.choice.set_options(std::move(options));
    row.choice.set_index(index);
    return row;
}

FormRow &Form::add_slider(int id, std::string label, float value, float minimum, float maximum,
                          float step)
{
    FormRow &row = add(FormRowKind::slider, id, std::move(label));
    row.minimum = std::min(minimum, maximum);
    row.maximum = std::max(minimum, maximum);
    row.step = std::max(step, 0.0f);
    row.value = std::clamp(value, row.minimum, row.maximum);
    row.shown.snap(fraction(row));
    return row;
}

FormRow &Form::add_stepper(int id, std::string label, int value, int minimum, int maximum, int step)
{
    FormRow &row = add(FormRowKind::stepper, id, std::move(label));
    row.stepper.set_range(minimum, maximum, step);
    row.stepper.set_value(value);
    return row;
}

FormRow &Form::add_action(int id, std::string label)
{
    return add(FormRowKind::action, id, std::move(label));
}

FormRow &Form::add_value(int id, std::string label, std::string text)
{
    FormRow &row = add(FormRowKind::value, id, std::move(label));
    row.text = std::move(text);
    return row;
}

void Form::clear()
{
    rows_.clear();
    tops_.clear();
    focus_ = -1;
    held_ = 0;
    scroll_.position.snap(0.0f);
}

FormRow *Form::row(int id)
{
    for (FormRow &row : rows_)
    {
        if (row.kind != FormRowKind::header && row.id == id)
            return &row;
    }
    return nullptr;
}

const FormRow *Form::row(int id) const
{
    for (const FormRow &row : rows_)
    {
        if (row.kind != FormRowKind::header && row.id == id)
            return &row;
    }
    return nullptr;
}

// ---- values -----------------------------------------------------------------

bool Form::toggle_value(int id) const
{
    const FormRow *found = row(id);
    return found != nullptr && found->on;
}

int Form::choice_index(int id) const
{
    const FormRow *found = row(id);
    return found != nullptr ? found->choice.index() : 0;
}

const std::string &Form::choice_text(int id) const
{
    const FormRow *found = row(id);
    return found != nullptr ? found->choice.value() : kNoText;
}

float Form::slider_value(int id) const
{
    const FormRow *found = row(id);
    return found != nullptr ? found->value : 0.0f;
}

int Form::stepper_value(int id) const
{
    const FormRow *found = row(id);
    return found != nullptr ? found->stepper.value() : 0;
}

const std::string &Form::value_text(int id) const
{
    const FormRow *found = row(id);
    return found != nullptr ? found->text : kNoText;
}

void Form::set_toggle(int id, bool on)
{
    if (FormRow *found = row(id))
        found->on = on;
}

void Form::set_choice(int id, int index)
{
    if (FormRow *found = row(id))
        found->choice.set_index(index);
}

void Form::set_slider(int id, float value)
{
    if (FormRow *found = row(id))
        found->value = std::clamp(value, found->minimum, found->maximum);
}

void Form::set_stepper(int id, int value)
{
    if (FormRow *found = row(id))
        found->stepper.set_value(value);
}

void Form::set_value_text(int id, std::string text)
{
    if (FormRow *found = row(id))
        found->text = std::move(text);
}

void Form::set_disabled(int id, bool disabled)
{
    if (FormRow *found = row(id))
        found->disabled = disabled;
}

std::string Form::slider_text(const FormRow &row) const
{
    if (row.format)
        return row.format(row.value);
    char number[32];
    std::snprintf(number, sizeof(number), "%.*f", std::clamp(row.decimals, 0, 6),
                  static_cast<double>(row.value));
    return std::string(number) + row.unit;
}

// ---- layout -----------------------------------------------------------------

void Form::set_bounds(const Rect &bounds)
{
    // The same place again (a screen that lays out every frame) must not
    // cut a glide short.
    if (bounds.x == bounds_.x && bounds.y == bounds_.y && bounds.w == bounds_.w &&
        bounds.h == bounds_.h)
        return;
    bounds_ = bounds;
    retarget(true);
}

Rect Form::inner() const
{
    return style.panel ? bounds_.inset(style.panel_padding) : bounds_;
}

float Form::scale() const
{
    return style.compact ? 0.8f : 1.0f;
}

// Compact type shrinks less than compact rows, and never below what reads
// from a sofa.
float Form::type(float size) const
{
    return style.compact ? std::max(size * 0.9f, std::min(size, 20.0f)) : size;
}

float Form::base_size(int index) const
{
    const bool header = rows_[static_cast<std::size_t>(index)].kind == FormRowKind::header;
    return (header ? style.header_height : style.row_height) * scale();
}

// The extra height a row takes while it shows its description.
float Form::fold_size(int index) const
{
    const FormRow &row = rows_[static_cast<std::size_t>(index)];
    if (!style.description_inline || row.description.empty() || row.kind == FormRowKind::header)
        return 0.0f;
    return type(style.description_size) * 1.5f;
}

// The layout once every fold has settled: only the focused row is unfolded.
float Form::target_top(int index) const
{
    float y = 0.0f;
    for (int i = 0; i < index; ++i)
        y += base_size(i) + style.gap * scale();
    if (focus_ >= 0 && focus_ < index)
        y += fold_size(focus_);
    return y;
}

float Form::target_height() const
{
    const int count = static_cast<int>(rows_.size());
    if (count == 0)
        return 0.0f;
    return target_top(count - 1) + base_size(count - 1) +
           (focus_ == count - 1 ? fold_size(focus_) : 0.0f);
}

// The layout right now, folds in motion included.
void Form::layout()
{
    const int count = static_cast<int>(rows_.size());
    tops_.resize(rows_.size());
    float y = 0.0f;
    for (int i = 0; i < count; ++i)
    {
        tops_[static_cast<std::size_t>(i)] = y;
        y += base_size(i) + fold_size(i) * rows_[static_cast<std::size_t>(i)].open.value +
             style.gap * scale();
    }
}

Rect Form::row_rect(int index) const
{
    const Rect in = inner();
    const std::size_t at = static_cast<std::size_t>(index);
    return {in.x, in.y + tops_[at] - scroll_.offset(), in.w,
            base_size(index) + fold_size(index) * rows_[at].open.value};
}

bool Form::focusable(int index) const
{
    const FormRowKind kind = rows_[static_cast<std::size_t>(index)].kind;
    return kind != FormRowKind::header && (kind != FormRowKind::value || style.focus_values);
}

// The next focusable row in a direction, or -1 at the end.
int Form::next(int from, int direction) const
{
    const int count = static_cast<int>(rows_.size());
    for (int i = from + direction; i >= 0 && i < count; i += direction)
    {
        if (focusable(i))
            return i;
    }
    return -1;
}

void Form::retarget(bool snap)
{
    const int count = static_cast<int>(rows_.size());
    for (int i = 0; i < count; ++i)
    {
        FormRow &row = rows_[static_cast<std::size_t>(i)];
        row.open.target = i == focus_ ? 1.0f : 0.0f;
        if (snap)
            row.open.snap(row.open.target);
    }
    if (focus_ >= 0 && focus_ < count)
    {
        const Rect in = inner();
        const float top = target_top(focus_);
        const float size = base_size(focus_) + fold_size(focus_);
        // Reveal the section header above the first row of a section too.
        const bool under_header =
            focus_ > 0 && rows_[static_cast<std::size_t>(focus_ - 1)].kind == FormRowKind::header;
        scroll_.reveal(under_header ? target_top(focus_ - 1) : top, top + size, in.h,
                       style.row_height * scale() * 0.5f, target_height());
        // The highlight lives in content space: scrolling must not make it lag.
        const Rect target{0.0f, top, in.w, size};
        highlight_.target(target);
        if (snap)
        {
            scroll_.position.snap(scroll_.position.target);
            highlight_.snap(target);
        }
    }
    layout();
}

void Form::set_focus(int index, bool snap)
{
    const int count = static_cast<int>(rows_.size());
    if (count == 0)
        return;
    index = std::clamp(index, 0, count - 1);
    if (!focusable(index))
    {
        const int after = next(index, 1);
        index = after >= 0 ? after : next(index, -1);
    }
    if (index < 0)
        return;
    focus_ = index;
    held_ = 0;
    retarget(snap);
}

void Form::focus_row(int id, bool snap)
{
    const int count = static_cast<int>(rows_.size());
    for (int i = 0; i < count; ++i)
    {
        const FormRow &candidate = rows_[static_cast<std::size_t>(i)];
        if (candidate.kind != FormRowKind::header && candidate.id == id)
        {
            set_focus(i, snap);
            return;
        }
    }
}

int Form::focus_id() const
{
    return focus_ >= 0 ? rows_[static_cast<std::size_t>(focus_)].id : -1;
}

bool Form::uses_horizontal() const
{
    if (focus_ < 0)
        return false;
    const FormRowKind kind = rows_[static_cast<std::size_t>(focus_)].kind;
    return kind == FormRowKind::toggle || kind == FormRowKind::choice ||
           kind == FormRowKind::slider || kind == FormRowKind::stepper;
}

const std::string &Form::help_text() const
{
    return focus_ >= 0 ? rows_[static_cast<std::size_t>(focus_)].description : kNoText;
}

void Form::enter()
{
    age_ = 0.0f;
}

// ---- the styles of the embedded components ------------------------------------

ChoiceStyle Form::choice_style() const
{
    ChoiceStyle look;
    static_cast<ComponentStyle &>(look) = style;
    look.value_size = type(style.value_size);
    look.boxed = false;
    look.focus_ring = false;
    look.wrap = style.wrap_choices;
    look.arrow_inset = 10.0f;
    look.idle_arrows = 0.3f;
    return look;
}

StepperStyle Form::stepper_style() const
{
    StepperStyle look;
    static_cast<ComponentStyle &>(look) = style;
    look.value_size = type(style.value_size);
    look.buttons = style.stepper_buttons;
    look.button_size = 44.0f * (style.compact ? 0.86f : 1.0f);
    look.focus_ring = false;
    look.fast_after = style.fast_after;
    look.fast_factor = style.fast_factor;
    return look;
}

// ---- input ------------------------------------------------------------------

Event Form::flip(FormRow &row, bool on, Feedback &feedback, float x)
{
    row.on = on;
    changed_id_ = row.id;
    // Up for on, down for off: the ear knows the state without looking.
    play_cue(feedback, style, style.sounds.change, x, on ? 1.06f : 0.94f);
    return Event::changed;
}

Event Form::edit(FormRow &row, int direction, const InputFrame &input, Feedback &feedback)
{
    // Cues come from where the controls are: the right part of the form.
    const Rect in = inner();
    const float x = in.x + in.w * (style.values_right ? 0.8f : 0.6f);
    Event event = Event::none;
    switch (row.kind)
    {
    case FormRowKind::toggle:
        // Spatial, like the switch itself: right is on, left is off.
        if (row.on == (direction > 0))
            return refuse(feedback, style, input, highlight_.refusal(), x);
        return flip(row, direction > 0, feedback, x);
    case FormRowKind::choice:
        event = row.choice.pick(direction, style.wrap_choices, input, feedback, choice_style(), x,
                                highlight_.refusal());
        break;
    case FormRowKind::stepper:
        event = row.stepper.step(input, feedback, stepper_style(), x, highlight_.refusal());
        break;
    case FormRowKind::slider:
    {
        const float span = row.maximum - row.minimum;
        const float unit =
            row.step > 0.0f ? row.step : span / static_cast<float>(std::max(style.slider_steps, 1));
        held_ = input.nav_repeat ? held_ + 1 : 0;
        const int strides =
            style.fast_after > 0 && held_ >= style.fast_after ? std::max(style.fast_factor, 1) : 1;
        // Count in whole steps from the minimum: adding a float step over and
        // over would drift off the grid.
        const float at = unit > 0.0f ? std::round((row.value - row.minimum) / unit) : 0.0f;
        const float wanted = row.minimum + (at + static_cast<float>(direction * strides)) * unit;
        const float value = std::clamp(wanted, row.minimum, row.maximum);
        if (std::fabs(value - row.value) <= unit * 1e-3f)
            return refuse(feedback, style, input, highlight_.refusal(), x);
        row.value = value;
        row.press.trigger();
        play_cue(feedback, style, style.sounds.step, x, tween::lerp(0.9f, 1.2f, fraction(row)));
        event = Event::changed;
        break;
    }
    default:
        break;
    }
    if (event == Event::changed)
        changed_id_ = row.id;
    return event;
}

Event Form::handle(const InputFrame &input, Feedback &feedback)
{
    if (focus_ < 0)
        return Event::none;
    const float x = bounds_.cx();
    if (input.nav == Direction::up || input.nav == Direction::down)
    {
        const int direction = input.nav == Direction::down ? 1 : -1;
        int to = next(focus_, direction);
        if (to < 0 && style.wrap && !input.nav_repeat)
            to = next(direction > 0 ? -1 : static_cast<int>(rows_.size()), direction);
        if (to < 0 || to == focus_)
            return refuse(feedback, style, input, highlight_.refusal(), x);
        focus_ = to;
        held_ = 0;
        retarget(false);
        const float along = rows_.size() > 1
                                ? static_cast<float>(focus_) / static_cast<float>(rows_.size() - 1)
                                : 0.0f;
        play_cue(feedback, style, style.sounds.move, x,
                 style.pitch_by_position ? tween::lerp(1.05f, 0.95f, along) : 1.0f);
        return Event::moved;
    }

    FormRow &row = rows_[static_cast<std::size_t>(focus_)];
    if (input.nav == Direction::left || input.nav == Direction::right)
    {
        if (!uses_horizontal())
            return Event::none;
        if (row.disabled)
            return refuse(feedback, style, input, highlight_.refusal(), x);
        return edit(row, input.nav == Direction::right ? 1 : -1, input, feedback);
    }
    if (input.is_pressed(Action::confirm))
    {
        if (row.disabled)
            return refuse(feedback, style, input, highlight_.refusal(), x);
        switch (row.kind)
        {
        case FormRowKind::toggle:
            return flip(row, !row.on, feedback, x);
        case FormRowKind::choice:
        {
            // Confirm always finds a next option: it goes round at the end.
            const Event event =
                row.choice.pick(1, true, input, feedback, choice_style(), x, highlight_.refusal());
            if (event == Event::changed)
                changed_id_ = row.id;
            return event;
        }
        case FormRowKind::action:
            row.press.trigger();
            changed_id_ = row.id;
            play_cue(feedback, style, style.sounds.activate, x);
            if (style.sounds.rumble > 0.0f)
                feedback.rumble(0.3f * style.sounds.rumble, 0.04f);
            return Event::activated;
        case FormRowKind::value:
            return refuse(feedback, style, input, highlight_.refusal(), x);
        default:
            // A slider and a stepper have nothing to confirm.
            return Event::none;
        }
    }
    if (input.is_pressed(Action::back))
    {
        play_cue(feedback, style, style.sounds.cancel, x);
        return Event::cancelled;
    }
    return Event::none;
}

// ---- animation --------------------------------------------------------------

void Form::update(float dt)
{
    age_ += dt;
    // Every frame, so a style change (compact, a new row height) re-flows the
    // form by itself, with the highlight and the scroll gliding to their new
    // places.
    retarget(settle_);
    settle_ = false;

    const float omega = std::max(style.omega(), 18.0f);
    for (FormRow &row : rows_)
    {
        row.on_amount.target = row.on ? 1.0f : 0.0f;
        row.on_amount.update(dt, std::max(style.omega(), 14.0f), std::max(style.damping(), 0.62f));
        row.shown.target = fraction(row);
        row.shown.update(dt, 26.0f);
        row.open.update(dt, omega);
        row.press.update(dt, 10.0f);
        if (row.kind == FormRowKind::choice)
            row.choice.update(dt, style);
        else if (row.kind == FormRowKind::stepper)
            row.stepper.update(dt, style);
    }
    layout();
    highlight_.update(dt, style);
    scroll_.update(dt, std::max(style.omega(), 14.0f));
    active_amount_.target = active_ ? 1.0f : 0.0f;
    active_amount_.update(dt, 18.0f);
}

// ---- drawing ----------------------------------------------------------------

// A control's place on a row's main line: at the row's right edge, or where
// the label column ends.
Rect Form::control_rect(const Rect &line, float width) const
{
    const float left = line.x + style.padding;
    const float right = line.x + line.w - style.padding;
    width = std::min(width, std::max(right - left, 0.0f));
    if (style.values_right)
        return {right - width, line.y, width, line.h};
    const float column =
        style.label_width > 0.0f ? style.label_width : (right - left) * style.label_ratio;
    return {std::min(left + column, right - width), line.y, width, line.h};
}

void Form::draw_row(Canvas &canvas, Painter &paint, const FormRow &row, const Rect &rect,
                    float focus, ChoiceStyle &choice_look, StepperStyle &stepper_look) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    const float zoom = style.compact ? 0.86f : 1.0f; // controls shrink less than rows
    const bool on_surface = style.panel || !style.on_page;
    const Color text = on_surface ? theme.text : paint.page_text();
    const Color muted = on_surface ? theme.text_muted : paint.page_text_muted();

    if (row.kind == FormRowKind::header)
    {
        const float size = style.header_size;
        const float baseline = rect.y + rect.h - 16.0f * scale();
        const float left = rect.x + style.padding;
        const float width = paint.label(upper(row.label), left, baseline, size, muted);
        if (style.header_rule)
            list.rounded_rect({left + width + 16.0f, baseline - size * 0.36f,
                               std::max(rect.w - 2.0f * style.padding - width - 16.0f, 0.0f), 1.5f},
                              0.0f, muted.with_alpha(0.25f));
        return;
    }

    // The colour of text under the highlight: a filled plate turns it, and a
    // glow plate is a surface whatever is behind the form.
    Color ink = text;
    if (style.highlight.kind == HighlightKind::fill)
        ink = gfx::mix(text, Highlight::text_color(style, style.highlight, 1.0f), focus);
    else if (style.highlight.kind == HighlightKind::glow)
        ink = gfx::mix(text, theme.text, focus);
    const Color quiet = gfx::mix(muted, ink, focus * 0.6f);

    const float base = style.row_height * scale();
    const Rect line{rect.x, rect.y, rect.w, base};
    const float cy = line.cy();
    const float label_size = type(style.label_size);
    const float value_size = type(style.value_size);
    const float shift = style.reduced_motion ? 0.0f : style.focus_shift * focus;
    const float left = rect.x + style.padding + shift;
    const float right = rect.x + rect.w - style.padding;
    // Where the label must stop. Each kind moves it to its control's left edge.
    float label_end = right;

    // Disabled rows are dimmed, and so are read-only ones where they can be
    // focused: the highlight must not promise an edit.
    const bool read_only = row.kind == FormRowKind::value && style.focus_values;
    list.push_opacity(row.disabled ? 0.42f : (read_only ? 0.62f : 1.0f));
    Color label_ink = ink;
    switch (row.kind)
    {
    case FormRowKind::toggle:
    {
        const Rect at = control_rect(line, style.toggle_width * zoom);
        const float height = style.toggle_height * zoom;
        const Rect body{at.x, cy - height * 0.5f, at.w, height};
        Look look;
        look.disabled = row.disabled;
        paint.toggle(body, row.on_amount.value, look);
        label_end = body.x;
        if (style.toggle_text)
        {
            const char *state = row.on ? style.on_text : style.off_text;
            const float baseline = cy + value_size * 0.35f;
            const Color color = row.on ? ink : quiet;
            if (style.values_right)
            {
                const float room = std::max(paint.label_width(style.on_text, value_size),
                                            paint.label_width(style.off_text, value_size));
                paint.label(state, body.x - 16.0f, baseline, value_size, color, gfx::Align::right);
                label_end = body.x - 16.0f - room;
            }
            else
            {
                paint.label(state, body.x + body.w + 16.0f, baseline, value_size, color);
            }
        }
        break;
    }
    case FormRowKind::choice:
    {
        const Rect at = control_rect(line, style.control_width * zoom);
        choice_look.ink = ink;
        row.choice.draw(canvas, choice_look, at, focus);
        label_end = at.x;
        break;
    }
    case FormRowKind::slider:
    {
        const Rect at = control_rect(line, style.control_width * zoom);
        const float height = style.slider_height * zoom;
        const float number = std::min(style.number_width * zoom, at.w * 0.4f);
        Look look;
        look.press = row.press.value;
        look.disabled = row.disabled;
        paint.slider({at.x, cy - height * 0.5f, at.w - number, height}, row.shown.value, look);
        paint.bounded_label(slider_text(row), at.x + at.w, cy + value_size * 0.35f, value_size, ink, number - 8.0f, gfx::Align::right);
        label_end = at.x;
        break;
    }
    case FormRowKind::stepper:
    {
        const Rect at = control_rect(line, style.stepper_width * zoom);
        const float height = std::min(stepper_look.button_size, base - 8.0f);
        stepper_look.ink = ink;
        row.stepper.draw(canvas, stepper_look, {at.x, cy - height * 0.5f, at.w, height}, focus);
        label_end = at.x;
        break;
    }
    case FormRowKind::action:
    case FormRowKind::value:
    {
        if (row.kind == FormRowKind::action && row.danger)
            label_ink = style.highlight.kind == HighlightKind::fill
                            ? gfx::mix(theme.danger, ink, focus)
                            : theme.danger;
        if (trailing)
        {
            const Rect at = control_rect(line, style.control_width * zoom);
            trailing(canvas, at, row, focus);
            label_end = at.x;
            break;
        }
        float end = right;
        if (row.kind == FormRowKind::action && row.chevron)
        {
            const float x = end - 7.0f + (style.reduced_motion ? 0.0f : 4.0f * focus);
            const Color color = row.danger ? label_ink : quiet;
            list.line(x - 5.0f, cy - 9.0f, x + 4.0f, cy, 2.5f, color);
            list.line(x + 4.0f, cy, x - 5.0f, cy + 9.0f, 2.5f, color);
            end -= 30.0f;
        }
        if (!row.text.empty())
        {
            const float baseline = cy + value_size * 0.35f;
            const float column = control_rect(line, 0.0f).x;
            if (style.values_right)
            {
                // A value may take up to its column; the label gets the rest.
                const float room = end - left - (right - left) * style.label_ratio;
                const float width =
                    paint.bounded_label(row.text, end, baseline, value_size, quiet, std::max(room, 80.0f), gfx::Align::right);
                label_end = end - width;
            }
            else
            {
                paint.bounded_label(row.text, column, baseline, value_size, quiet, std::max(end - column, 80.0f));
                label_end = column;
            }
        }
        else
        {
            label_end = end;
        }
        break;
    }
    case FormRowKind::header:
        break;
    }

    if (!style.values_right)
        label_end = std::min(label_end, control_rect(line, 0.0f).x);
    paint.bounded_label(row.label, left, cy + label_size * 0.35f, label_size, label_ink, std::max(label_end - 18.0f - left, 40.0f));
    list.pop_opacity();

    // The description is not dimmed with a disabled row: it is what explains it.
    const float unfolded = row.open.value;
    if (style.description_inline && !row.description.empty() && unfolded > 0.01f)
    {
        const float size = type(style.description_size);
        list.push_opacity(tween::clamp01(unfolded));
        paint.bounded_body(row.description, left, rect.y + base - 9.0f * scale() + size * 0.8f, size, quiet, rect.w - 2.0f * style.padding);
        list.pop_opacity();
    }
}

void Form::draw(Canvas &canvas) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    if (style.panel)
        paint.panel(bounds_);
    const int count = static_cast<int>(rows_.size());
    if (count == 0 || tops_.size() != rows_.size())
        return;

    const Rect in = inner();
    const float scroll = scroll_.offset();
    const float content = target_height();
    const bool overflow = content > in.h + 0.5f;
    // The clip is a little larger than the rows so a ring or a glow around
    // the first and last row is not cut, but only at an end the form is
    // scrolled to: anywhere else the margin would show a slice of the row
    // that is leaving.
    const bool fades = style.edge_fade > 0.0f;
    float bleed = 10.0f;
    if (style.highlight.kind == HighlightKind::glow)
        bleed = 30.0f;
    else if (style.highlight.kind == HighlightKind::ring)
        // The hard look's ring also wraps the offset shadow a control has.
        bleed = std::max({bleed, theme.focus_gap + theme.focus_width + 2.0f,
                          theme.border + theme.shadow_offset + 6.0f});
    const float limit = std::max(content - in.h, 0.0f);
    const float above = std::clamp(bleed - scroll, 0.0f, bleed);
    const float below = std::clamp(bleed - (limit - scroll), 0.0f, bleed);
    list.push_clip({in.x - bleed, in.y - above, in.w + 2.0f * bleed, in.h + above + below});

    // How visible a row is: 1 inside the view, fading as it is cut.
    const auto visibility = [&](const Rect &row)
    {
        if (!overflow || !fades)
            return 1.0f;
        // A row's text sits on its centre line: a row that shows less than
        // half of itself would only show cut letters, so it is gone by then.
        const float shown =
            std::min(row.y + row.h - in.y, in.y + in.h - row.y) / std::max(row.h, 1.0f);
        const float t = tween::clamp01((shown - 0.5f) / std::max(style.edge_fade - 0.5f, 0.01f));
        return t * t;
    };
    const auto entrance = [&](int index)
    {
        if (style.entrance_step <= 0.0f || style.reduced_motion)
            return tween::cubic_out(age_ / 0.2f);
        return tween::stagger(age_, index, style.entrance_step, 0.32f);
    };

    if (style.dividers)
    {
        for (int i = 0; i + 1 < count; ++i)
        {
            const Rect row = row_rect(i);
            const float alpha = visibility(row) * entrance(i);
            if (alpha <= 0.0f || rows_[static_cast<std::size_t>(i)].kind == FormRowKind::header ||
                rows_[static_cast<std::size_t>(i + 1)].kind == FormRowKind::header)
                continue;
            list.rounded_rect(
                {row.x + style.padding, row.y + row.h + style.gap * scale() * 0.5f - 0.75f,
                 row.w - 2.0f * style.padding, 1.5f},
                0.0f,
                (style.panel || !style.on_page ? theme.text_muted : paint.page_text_muted())
                    .with_alpha(0.22f * alpha));
        }
    }

    const float active = active_amount_.value;
    if (focus_ >= 0)
    {
        const FormRow &focused = rows_[static_cast<std::size_t>(focus_)];
        HighlightStyle look = style.highlight;
        look.grow -= 2.0f * focused.press.value; // a press pushes it in for a moment
        // Over a destructive action the highlight itself turns to the danger
        // colour, blended by how much of that row it covers while it glides.
        float danger = 0.0f;
        for (int i = 0; i < count; ++i)
        {
            const FormRow &row = rows_[static_cast<std::size_t>(i)];
            if (row.kind == FormRowKind::action && row.danger)
                danger += highlight_.coverage({0.0f, tops_[static_cast<std::size_t>(i)], in.w,
                                               base_size(i) + fold_size(i) * row.open.value});
        }
        if (danger > 0.01f && look.kind != HighlightKind::fill)
        {
            const Color own = look.color.a > 0.0f ? look.color : theme.focus;
            look.color = gfx::mix(own, theme.danger.with_alpha(std::max(own.a, 0.8f)),
                                  tween::clamp01(danger));
        }
        // The highlight is kept in content space; bring it to the screen here.
        list.push_transform(1.0f, 0.0f, 0.0f, in.x, in.y - scroll);
        highlight_.draw(canvas, style, look, (0.35f + 0.65f * active) * entrance(focus_));
        list.pop_transform();
    }

    ChoiceStyle choice_look = choice_style();
    StepperStyle stepper_look = stepper_style();
    for (int i = 0; i < count; ++i)
    {
        const FormRow &row = rows_[static_cast<std::size_t>(i)];
        const Rect rect = row_rect(i);
        const float arrived = entrance(i);
        const float alpha = visibility(rect) * arrived;
        if (alpha <= 0.0f || rect.y > in.y + in.h || rect.y + rect.h < in.y)
            continue;
        list.push_opacity(alpha);
        list.push_transform(1.0f, 0.0f, 0.0f, 0.0f,
                            style.reduced_motion ? 0.0f : 18.0f * (1.0f - arrived));
        const float focus =
            highlight_.coverage({0.0f, tops_[static_cast<std::size_t>(i)], in.w, rect.h}) * active;
        draw_row(canvas, paint, row, rect, focus, choice_look, stepper_look);
        list.pop_transform();
        list.pop_opacity();
    }
    list.pop_clip();

    if (style.scroll_thumb && overflow)
    {
        const float track = in.h - 16.0f;
        const float size = std::max(track * in.h / content, 36.0f);
        const float at = scroll / std::max(content - in.h, 1.0f);
        const float x = in.x + in.w + 8.0f;
        const Color ink =
            style.panel || !style.on_page ? theme.text_muted : paint.page_text_muted();
        list.rounded_rect({x, in.y + 8.0f, 4.0f, track}, 2.0f, ink.with_alpha(0.16f));
        list.rounded_rect({x, in.y + 8.0f + (track - size) * tween::clamp01(at), 4.0f, size}, 2.0f,
                          ink.with_alpha(0.7f));
    }
}

} // namespace hui::ui
