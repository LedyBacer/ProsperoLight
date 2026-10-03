// ps5-homebrew-ui - Components: ButtonGroup and SplitButton.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/button_group.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace hui::ui
{

using gfx::Color;
using gfx::Rect;

namespace
{

constexpr float kIconGap = 10.0f;

float baseline_for(float cy, float size)
{
    return cy + size * 0.35f;
}

// Where square plates reach the group's own border instead of floating
// inside it: every language without a real corner radius.
bool square_joints(const Theme &theme)
{
    return theme.corner != Corner::round || theme.radius < 5.0f ||
           theme.style == SurfaceStyle::hard;
}

} // namespace

// ---- ButtonGroup -----------------------------------------------------------

void ButtonGroup::set_items(std::vector<GroupItem> items)
{
    items_ = std::move(items);
    chosen_.assign(items_.size(), tween::Spring{});
    for (std::size_t i = 0; i < items_.size(); ++i)
        chosen_[i].snap(items_[i].selected ? 1.0f : 0.0f);
    focus_ = std::clamp(focus_, 0, std::max(count() - 1, 0));
    pressed_ = -1;
    retarget(true);
}

void ButtonGroup::set_bounds(const Rect &bounds)
{
    // A screen may place its components every frame: the same bounds again
    // must not stop the ring's glide.
    if (bounds.x == bounds_.x && bounds.y == bounds_.y && bounds.w == bounds_.w &&
        bounds.h == bounds_.h)
        return;
    bounds_ = bounds;
    retarget(true);
}

void ButtonGroup::set_focus(int index, bool snap)
{
    if (items_.empty())
        return;
    focus_ = std::clamp(index, 0, count() - 1);
    retarget(snap);
}

void ButtonGroup::set_active(bool active)
{
    active_ = active;
}

int ButtonGroup::selected() const
{
    for (int i = 0; i < count(); ++i)
    {
        if (items_[static_cast<std::size_t>(i)].selected)
            return i;
    }
    return -1;
}

void ButtonGroup::set_selected(int index, bool snap)
{
    for (int i = 0; i < count(); ++i)
    {
        GroupItem &item = items_[static_cast<std::size_t>(i)];
        if (style.mode == GroupMode::multiple)
            item.selected = item.selected || i == index;
        else
            item.selected = i == index;
        if (snap)
            chosen_[static_cast<std::size_t>(i)].snap(item.selected ? 1.0f : 0.0f);
    }
}

bool ButtonGroup::is_selected(int index) const
{
    return index >= 0 && index < count() && items_[static_cast<std::size_t>(index)].selected;
}

Rect ButtonGroup::item_rect(int index) const
{
    const ButtonMetrics m =
        button_metrics(style.size, style.height, style.text_size, style.padding);
    const int n = std::max(count(), 1);
    const float gap = style.joined ? 0.0f : style.gap;
    const float at = static_cast<float>(index);
    if (style.layout == GroupLayout::row)
    {
        const float w = style.item_width > 0.0f
                            ? style.item_width
                            : (bounds_.w - gap * static_cast<float>(n - 1)) / static_cast<float>(n);
        return {bounds_.x + at * (w + gap), bounds_.cy() - m.height * 0.5f, w, m.height};
    }
    const float w = style.item_width > 0.0f ? std::min(style.item_width, bounds_.w) : bounds_.w;
    return {bounds_.x, bounds_.y + at * (m.height + gap), w, m.height};
}

Rect ButtonGroup::rect() const
{
    const Rect first = item_rect(0);
    const Rect last = item_rect(std::max(count() - 1, 0));
    return {first.x, first.y, last.x + last.w - first.x, last.y + last.h - first.y};
}

void ButtonGroup::retarget(bool snap)
{
    if (items_.empty())
        return;
    const Rect target = item_rect(focus_);
    highlight_.target(target);
    if (snap)
        highlight_.snap(target);
}

Event ButtonGroup::handle(const InputFrame &input, Feedback &feedback)
{
    exit_ = Direction::none;
    if (items_.empty())
        return Event::none;
    const bool row = style.layout == GroupLayout::row;
    const Direction before = row ? Direction::left : Direction::up;
    const Direction after = row ? Direction::right : Direction::down;
    const float x = item_rect(focus_).cx();

    if (input.nav == before || input.nav == after)
    {
        int next = focus_ + (input.nav == after ? 1 : -1);
        if (next < 0 || next >= count())
        {
            if (style.wrap && !input.nav_repeat && count() > 1)
            {
                next = next < 0 ? count() - 1 : 0;
            }
            else if (style.exits.allows(input.nav))
            {
                exit_ = input.nav;
                return Event::none;
            }
            else
            {
                return refuse(feedback, style, input, highlight_.refusal(), x);
            }
        }
        focus_ = next;
        retarget(false);
        const float along =
            count() > 1 ? static_cast<float>(focus_) / static_cast<float>(count() - 1) : 0.0f;
        play_cue(feedback, style, style.sounds.move, item_rect(focus_).cx(),
                 style.pitch_by_position ? tween::lerp(0.96f, 1.06f, along) : 1.0f);
        return Event::moved;
    }
    if (input.nav != Direction::none)
    {
        // Across the group there is nothing to move to: the screen decides.
        if (style.exits.allows(input.nav))
            exit_ = input.nav;
        return Event::none;
    }
    if (!input.is_pressed(Action::confirm))
        return Event::none;

    GroupItem &item = items_[static_cast<std::size_t>(focus_)];
    if (item.disabled)
        return refuse(feedback, style, input, highlight_.refusal(), x);
    pressed_ = focus_;
    press_.trigger();
    if (style.mode == GroupMode::actions)
    {
        play_cue(feedback, style, style.sounds.activate, x);
        if (style.sounds.rumble > 0.0f)
            feedback.rumble(0.3f * style.sounds.rumble, 0.04f);
        return Event::activated;
    }
    if (style.mode == GroupMode::exclusive)
    {
        if (item.selected && !style.allow_none)
            return Event::none; // already the choice: the press shows, nothing changes
        const bool on = !item.selected;
        for (GroupItem &other : items_)
            other.selected = false;
        item.selected = on;
        play_cue(feedback, style, style.sounds.change, x, on ? 1.04f : 0.94f);
        return Event::changed;
    }
    item.selected = !item.selected;
    play_cue(feedback, style, style.sounds.change, x, item.selected ? 1.06f : 0.94f);
    return Event::changed;
}

void ButtonGroup::update(float dt)
{
    // The knobs may have changed since the last frame: follow them.
    retarget(false);
    highlight_.update(dt, style);
    active_amount_.target = active_ ? 1.0f : 0.0f;
    active_amount_.update(dt, std::max(style.omega(), 18.0f));
    press_.update(dt, 9.0f);
    const float omega = std::clamp(style.omega(), 14.0f, 60.0f);
    for (std::size_t i = 0; i < items_.size() && i < chosen_.size(); ++i)
    {
        chosen_[i].target = items_[i].selected ? 1.0f : 0.0f;
        chosen_[i].update(dt, omega);
    }
}

void ButtonGroup::draw_content(Canvas &canvas, Painter &paint, const ButtonMetrics &m, int index,
                               const Rect &content, Color ink) const
{
    const GroupItem &item = items_[static_cast<std::size_t>(index)];
    const float focus = highlight_.coverage(item_rect(index)) * active_amount_.value;
    canvas.list.push_opacity(item.disabled ? 0.4f : 1.0f);
    const float lead = style.icon_width > 0.0f ? style.icon_width : 0.0f;
    const float lead_gap = lead > 0.0f && !item.label.empty() ? kIconGap : 0.0f;
    const float room = std::max(content.w - 2.0f * m.padding - lead - lead_gap, 0.0f);
    const std::string text = fit_label(paint, item.label, m.text_size, room);
    const float text_w = item.label.empty() ? 0.0f : paint.label_width(text, m.text_size);
    const float x = content.cx() - (lead + lead_gap + text_w) * 0.5f;
    if (lead > 0.0f && icon)
        icon(canvas, {x, content.cy() - lead * 0.5f, lead, lead}, item, index, ink, focus);
    if (!item.label.empty())
    {
        const float baseline = baseline_for(content.cy(), m.text_size);
        paint.label(text, x + lead + lead_gap, baseline, m.text_size, ink);
        if (style.role == ButtonRole::ghost && !style.joined && !is_selected(index))
            canvas.list.rounded_rect(
                {x + lead + lead_gap, baseline + m.text_size * 0.42f, text_w, 2.0f}, 0.0f,
                ink.with_alpha(0.45f));
    }
    canvas.list.pop_opacity();
}

void ButtonGroup::draw_separate(Canvas &canvas, const ButtonMetrics &m) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const auto state = [&](int i, Look &look, ButtonRole &role, float &scale)
    {
        const GroupItem &item = items_[static_cast<std::size_t>(i)];
        const float chosen = tween::clamp01(chosen_[static_cast<std::size_t>(i)].value);
        look.press = i == pressed_ ? tween::clamp01(press_.value) : 0.0f;
        look.disabled = item.disabled;
        role = chosen > 0.5f ? style.selected_role : style.role;
        scale = style.reduced_motion ? 1.0f : 1.0f - style.press_scale * look.press;
    };
    for (int i = 0; i < count(); ++i)
    {
        Look look;
        ButtonRole role = style.role;
        float scale = 1.0f;
        state(i, look, role, scale);
        const Rect r = item_rect(i);
        list.push_transform(scale, r.cx(), r.cy(), 0.0f, 0.0f);
        draw_button_face(canvas, theme, r, role, look, -1.0f, style.on_page);
        list.pop_transform();
    }
    // One ring for the whole group, under the words.
    {
        const bool on = tween::clamp01(chosen_[static_cast<std::size_t>(focus_)].value) > 0.5f;
        draw_button_ring(canvas, theme, highlight_.rect(canvas.time),
                         on ? style.selected_role : style.role, active_amount_.value, -1.0f,
                         style.on_page);
    }
    for (int i = 0; i < count(); ++i)
    {
        Look look;
        ButtonRole role = style.role;
        float scale = 1.0f;
        state(i, look, role, scale);
        const Rect r = item_rect(i);
        const ButtonFace face = button_face(theme, r, role, look, -1.0f, style.on_page);
        list.push_transform(scale, r.cx(), r.cy(), 0.0f, 0.0f);
        draw_content(canvas, paint, m, i, face.content, face.ink);
        list.pop_transform();
    }
}

void ButtonGroup::draw_joined(Canvas &canvas, Painter &paint, const ButtonMetrics &m) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    const bool row = style.layout == GroupLayout::row;
    const int n = count();
    const Rect all = rect();
    const ButtonPaint rest = button_paint(theme, style.role, style.on_page);
    const ButtonPaint chosen = button_paint(theme, style.selected_role, style.on_page);
    const float amount = tween::clamp01(active_amount_.value);
    const Rect glide = highlight_.rect(canvas.time);

    if (theme.style == SurfaceStyle::bevel)
    {
        // The classic toolbar: every button its own bevel, edge to edge; a
        // selected one stays pushed in on a lighter face.
        for (int i = 0; i < n; ++i)
        {
            const float on = tween::clamp01(chosen_[static_cast<std::size_t>(i)].value);
            const bool down = on > 0.5f || (i == pressed_ && press_.value > 0.5f);
            const Rect content =
                paint.surface(item_rect(i), 0.0f,
                              gfx::mix(rest.fill, theme.light,
                                       style.role == ButtonRole::ghost ? 0.0f : 0.55f * on),
                              rest.edge, down ? 0.0f : 1.0f);
            draw_content(canvas, paint, m, i, content, rest.ink);
        }
        draw_button_ring(canvas, theme, glide, style.role, amount, 0.0f, style.on_page);
        return;
    }

    const float radius = paint.control_radius(all);
    const bool square = square_joints(theme);
    const float line = std::max(rest.border, 0.0f);
    if (style.role != ButtonRole::ghost)
        paint.surface(all, radius, rest.fill, rest.edge, 1.0f, rest.border);

    // The plate of a selected button: it reaches the border in square
    // languages and floats inside the body in round ones.
    const float inset = std::max(line + 3.0f, 4.0f);
    const auto plate_of = [&](int i) -> Rect
    {
        const Rect seg = item_rect(i);
        if (!square)
            return seg.inset(inset);
        const float half = line * 0.5f;
        float x0 = seg.x + (row && i > 0 ? half : line);
        float x1 = seg.x + seg.w - (row && i + 1 < n ? half : line);
        float y0 = seg.y + (!row && i > 0 ? half : line);
        float y1 = seg.y + seg.h - (!row && i + 1 < n ? half : line);
        return {x0, y0, x1 - x0, y1 - y0};
    };
    const float plate_radius = square ? 0.0f : std::max(radius - inset, 2.0f);
    for (int i = 0; i < n; ++i)
    {
        const float on = tween::clamp01(chosen_[static_cast<std::size_t>(i)].value);
        const float press = i == pressed_ ? tween::clamp01(press_.value) : 0.0f;
        const Rect plate = plate_of(i);
        if (on > 0.01f)
        {
            list.push_opacity(on);
            if (theme.style == SurfaceStyle::glow)
            {
                // Lit consoles select with light, not with a block of colour.
                list.rounded_rect(plate, plate_radius, chosen.ink.with_alpha(0.18f));
                list.bordered_rect(plate, plate_radius, chosen.ink.with_alpha(0.0f), 2.0f,
                                   chosen.ink);
            }
            else if (square)
            {
                // Inside the body a plate is a plain block: the theme's
                // notches and cuts belong to the body's own corners.
                list.rounded_rect(plate, 0.0f, chosen.fill);
            }
            else
            {
                paint.fill(plate, plate_radius, chosen.fill);
            }
            list.pop_opacity();
        }
        if (press > 0.01f)
            list.rounded_rect(plate, plate_radius, rest.ink.with_alpha(0.14f * press));
    }

    // Dividers. Round languages shorten them and drop the ones beside a plate.
    const bool own_edge = style.role == ButtonRole::secondary && theme.secondary.a > 0.01f &&
                          rest.edge.a > 0.01f && rest.border > 0.0f;
    const Color rule = own_edge ? rest.edge : rest.ink.with_alpha(0.28f);
    const float rule_width = own_edge ? rest.border : 1.5f;
    for (int i = 0; i + 1 < n; ++i)
    {
        const Rect seg = item_rect(i);
        const float beside = std::max(chosen_[static_cast<std::size_t>(i)].value,
                                      chosen_[static_cast<std::size_t>(i + 1)].value);
        const float alpha = square ? 1.0f : 1.0f - tween::clamp01(beside);
        const float trim = square ? line : 12.0f;
        if (row)
            list.rounded_rect(
                {seg.x + seg.w - rule_width * 0.5f, seg.y + trim, rule_width, seg.h - 2.0f * trim},
                0.0f, rule.with_alpha(alpha));
        else
            list.rounded_rect(
                {seg.x + trim, seg.y + seg.h - rule_width * 0.5f, seg.w - 2.0f * trim, rule_width},
                0.0f, rule.with_alpha(alpha));
    }

    // The focus: a ring inside the button it is on, since a ring around it
    // would lie across its neighbours. On a selected plate it takes the
    // plate's text colour, or it would vanish where focus and plate match.
    if (amount > 0.01f && n > 0)
    {
        const float on = tween::clamp01(chosen_[static_cast<std::size_t>(focus_)].value);
        const Color solid{theme.focus.r, theme.focus.g, theme.focus.b,
                          std::max(theme.focus.a, 0.85f)};
        const Color ring =
            theme.style == SurfaceStyle::glow ? solid : gfx::mix(solid, chosen.ink, on);
        const float gap = square ? line + 3.0f : inset;
        const float width = std::clamp(theme.focus_width, 3.0f, 5.0f);
        list.push_opacity(amount);
        paint.stroke(glide.inset(gap), plate_radius, width, ring);
        list.pop_opacity();
    }

    for (int i = 0; i < n; ++i)
    {
        const float on = tween::clamp01(chosen_[static_cast<std::size_t>(i)].value);
        const Color ink = gfx::mix(rest.ink, chosen.ink, on);
        draw_content(canvas, paint, m, i, item_rect(i), ink);
    }
}

void ButtonGroup::draw(Canvas &canvas) const
{
    if (items_.empty())
        return;
    const ButtonMetrics m =
        button_metrics(style.size, style.height, style.text_size, style.padding);
    if (!style.joined)
    {
        draw_separate(canvas, m);
        return;
    }
    Painter paint(canvas.list, canvas.fonts, style.theme, canvas.glass);
    draw_joined(canvas, paint, m);
}

// ---- SplitButton -----------------------------------------------------------

void SplitButton::set_alternatives(std::vector<MenuItem> items)
{
    menu_.set_items(std::move(items));
}

void SplitButton::set_active(bool active)
{
    active_ = active;
}

void SplitButton::set_part(int part)
{
    part_ = std::clamp(part, 0, 1);
}

Rect SplitButton::rect() const
{
    const ButtonMetrics m =
        button_metrics(style.size, style.height, style.text_size, style.padding);
    return {bounds_.x, bounds_.cy() - m.height * 0.5f, bounds_.w, m.height};
}

Rect SplitButton::part_rect(int part) const
{
    const Rect r = rect();
    const float chevron =
        std::min(style.chevron_width > 0.0f ? style.chevron_width : r.h, r.w * 0.5f);
    if (part == 0)
        return {r.x, r.y, r.w - chevron, r.h};
    return {r.x + r.w - chevron, r.y, chevron, r.h};
}

Event SplitButton::handle(const InputFrame &input, Feedback &feedback)
{
    exit_ = Direction::none;
    if (menu_.is_open())
    {
        const Event event = menu_.handle(input, feedback);
        if (event == Event::activated)
        {
            choice_ = menu_.focus();
            if (style.swap_on_choose)
                std::swap(label, menu_.item(choice_).label);
        }
        return event;
    }
    const float x = part_rect(part_).cx();
    if (input.nav == Direction::left || input.nav == Direction::right)
    {
        const int next = part_ + (input.nav == Direction::right ? 1 : -1);
        if (next < 0 || next > 1)
        {
            if (style.exits.allows(input.nav))
            {
                exit_ = input.nav;
                return Event::none;
            }
            return refuse(feedback, style, input, refusal_, x);
        }
        part_ = next;
        play_cue(feedback, style, style.sounds.move, part_rect(part_).cx());
        return Event::moved;
    }
    if (input.nav != Direction::none)
    {
        if (style.exits.allows(input.nav))
            exit_ = input.nav;
        return Event::none;
    }
    if (!input.is_pressed(Action::confirm))
        return Event::none;
    if (part_ == 0)
    {
        choice_ = -1;
        press_.trigger();
        play_cue(feedback, style, style.sounds.activate, x);
        if (style.sounds.rumble > 0.0f)
            feedback.rumble(0.3f * style.sounds.rumble, 0.04f);
        return Event::activated;
    }
    if (menu_.items().empty())
        return refuse(feedback, style, input, refusal_, x);
    press_.trigger();
    menu_.open(rect(), feedback);
    return Event::none;
}

void SplitButton::update(float dt)
{
    // The menu is this button's: it follows the button's look and voice.
    menu_.style.theme = style.theme;
    menu_.style.reduced_motion = style.reduced_motion;
    menu_.style.sounds = style.sounds;
    menu_.style.width = style.menu_width > 0.0f ? style.menu_width : std::max(rect().w, 300.0f);
    menu_.update(dt);

    const float omega = std::max(style.omega(), 18.0f);
    focus_.target = active_ ? 1.0f : 0.0f;
    focus_.update(dt, omega);
    part_amount_.target = static_cast<float>(part_);
    part_amount_.update(dt, omega);
    open_amount_.target = menu_.is_open() ? 1.0f : 0.0f;
    open_amount_.update(dt, omega);
    press_.update(dt, 9.0f);
    refusal_.update(dt, 9.0f);
}

void SplitButton::draw(Canvas &canvas) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const ButtonMetrics m =
        button_metrics(style.size, style.height, style.text_size, style.padding);
    Rect r = rect();
    r.x += shake(refusal_.value, canvas.time, 8.0f);
    const float focus = tween::clamp01(focus_.value);
    const float press = tween::clamp01(press_.value);
    const float scale = style.reduced_motion ? 1.0f : 1.0f - style.press_scale * press;

    list.push_transform(scale, r.cx(), r.cy(), 0.0f, 0.0f);
    Look look;
    look.focus = focus;
    look.press = press;
    const ButtonFace face = draw_button_face(canvas, theme, r, style.role, look);
    const float dx = face.content.x - rect().x;
    const float dy = face.content.y - rect().y;
    const Rect main = part_rect(0);
    const Rect arrow = part_rect(1);

    // Which part confirm will fire: a plate that glides between the two.
    if (focus > 0.01f)
    {
        const float t = tween::clamp01(part_amount_.value);
        const float inset = std::max(theme.border, 0.0f) + 5.0f;
        const Rect at = Rect{tween::lerp(main.x, arrow.x, t) + dx, main.y + dy,
                             tween::lerp(main.w, arrow.w, t), main.h}
                            .inset(inset);
        const float radius = std::max(std::min(face.radius, at.h * 0.5f) - inset * 0.5f, 0.0f);
        paint.fill(at, radius, face.ink.with_alpha(0.14f * focus));
        paint.stroke(at, radius, 2.0f, face.ink.with_alpha(0.6f * focus));
    }
    const float rule = r.h * tween::clamp01(style.divider);
    list.rounded_rect({arrow.x + dx - 0.75f, r.cy() + dy - rule * 0.5f, 1.5f, rule}, 0.0f,
                      face.ink.with_alpha(0.35f));

    const float room = std::max(main.w - 2.0f * m.padding, 0.0f);
    paint.label(fit_label(paint, label, m.text_size, room), main.cx() + dx,
                baseline_for(main.cy() + dy, m.text_size), m.text_size, face.ink,
                gfx::Align::center);
    // The chevron turns over while the menu is open.
    const float size = m.text_size * 0.62f;
    const float depth = size * 0.28f * (1.0f - 2.0f * tween::clamp01(open_amount_.value));
    const float cx = arrow.cx() + dx;
    const float cy = arrow.cy() + dy;
    list.line(cx - size * 0.5f, cy - depth, cx, cy + depth, 3.0f, face.ink);
    list.line(cx, cy + depth, cx + size * 0.5f, cy - depth, 3.0f, face.ink);
    list.pop_transform();
}

void SplitButton::draw_menu(Canvas &canvas) const
{
    // ui::Menu backs its panel with the page colour only in glass themes. In
    // any other theme whose surface is translucent, what lies under the menu
    // would be read through its rows: give it the same backing here.
    const Theme &theme = style.theme;
    const float open = tween::clamp01(open_amount_.value);
    if (menu_.visible() && open > 0.01f && theme.style != SurfaceStyle::glass &&
        theme.surface.a < 0.95f)
    {
        const Rect panel = menu_.panel_rect();
        Painter paint(canvas.list, canvas.fonts, theme, 0);
        paint.fill(panel, std::min(theme.radius_card, std::min(panel.w, panel.h) * 0.5f),
                   Color{theme.page.r, theme.page.g, theme.page.b, open});
    }
    menu_.draw(canvas);
}

} // namespace hui::ui
