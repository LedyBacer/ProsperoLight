// ps5-homebrew-ui - Component: RadialMenu.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/radial.hpp"

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

constexpr float kPi = 3.14159265f;
constexpr float kTau = 6.2831853f;
constexpr float kFan = 40.0f;      // how far inside its place a wedge starts its entrance
constexpr float kFanTime = 0.34f;  // seconds one wedge takes to arrive
constexpr float kRimReach = 14.0f; // the bracket's distance beyond a pushed wedge

// Folds an angle into -pi..pi: the shortest signed way from one angle to another.
float wrap_angle(float angle)
{
    angle = std::fmod(angle + kPi, kTau);
    if (angle < 0.0f)
        angle += kTau;
    return angle - kPi;
}

// Moves an angle spring toward `angle` by the shortest way round, and keeps
// its value near zero so it cannot lose precision over a long session.
void chase(tween::Spring &spring, float angle, float dt, float omega)
{
    spring.target = spring.value + wrap_angle(angle - spring.value);
    spring.update(dt, omega);
    if (std::fabs(spring.value) > kPi)
    {
        const float turn = spring.value > 0.0f ? -kTau : kTau;
        spring.value += turn;
        spring.target += turn;
    }
}

Color solid(Color c)
{
    return {c.r, c.g, c.b, 1.0f};
}

// Design languages whose surfaces carry their own depth cue: a soft drop
// shadow under the ring would belong to another language.
bool own_depth(SurfaceStyle style)
{
    return style == SurfaceStyle::hard || style == SurfaceStyle::bevel ||
           style == SurfaceStyle::pixel || style == SurfaceStyle::neumorphic ||
           style == SurfaceStyle::sketch || style == SurfaceStyle::outline;
}

// A round hub would lose the corners that define these languages.
bool square_language(const Theme &theme)
{
    return theme.corner != Corner::round || theme.radius < 2.0f ||
           theme.style == SurfaceStyle::bevel || theme.style == SurfaceStyle::sketch;
}

// Where one wedge is this frame.
struct Wedge
{
    float angle;  // its centre
    float outer;  // outer radius
    float thick;  // radial thickness
    float start;  // first edge
    float sweep;  // angular size
    float arrive; // 0..1: its entrance
    float lit;    // 0..1: how pointed-at it is
};

} // namespace

void RadialMenu::set_items(std::vector<RadialItem> items)
{
    items_ = std::move(items);
    lifts_.assign(items_.size(), tween::Bounce{});
    focus_ = std::clamp(focus_, 0, std::max(static_cast<int>(items_.size()) - 1, 0));
    choice_ = -1;
    if (!items_.empty())
        lifts_[static_cast<std::size_t>(focus_)].snap(open_ ? 1.0f : 0.0f);
    hub_shown_ = hub_previous_ = focus_;
    hub_.running = false;
    needle_.snap(wedge_angle(focus_));
    rim_.snap(wedge_angle(focus_));
}

float RadialMenu::step_angle() const
{
    return kTau / static_cast<float>(std::max<std::size_t>(items_.size(), 1));
}

float RadialMenu::wedge_angle(int index) const
{
    return style.start_angle + static_cast<float>(index) * step_angle();
}

int RadialMenu::nearest(float angle) const
{
    const int count = static_cast<int>(items_.size());
    if (count == 0)
        return 0;
    const int index =
        static_cast<int>(std::lround(wrap_angle(angle - style.start_angle) / step_angle()));
    return ((index % count) + count) % count;
}

// The wedge a direction selects. The pointed wedge's zone is widened on both
// sides, so the direction has to be clearly inside a neighbour before the
// focus leaves.
int RadialMenu::pick(float angle) const
{
    if (std::fabs(wrap_angle(angle - wedge_angle(focus_))) <=
        step_angle() * 0.5f + style.hysteresis)
        return focus_;
    return nearest(angle);
}

void RadialMenu::wedge_center(int index, float *x, float *y) const
{
    const float angle = wedge_angle(index);
    const float middle = style.radius - std::min(style.thickness, style.radius) * 0.5f;
    *x = bounds_.cx() + std::sin(angle) * middle;
    *y = bounds_.cy() - std::cos(angle) * middle;
}

void RadialMenu::open(Feedback &feedback)
{
    open_ = true;
    held_ = true;
    engaged_ = false;
    age_ = 0.0f;
    choice_ = -1;
    hub_shown_ = hub_previous_ = focus_;
    hub_.running = false;
    needle_.snap(wedge_angle(focus_));
    rim_.snap(wedge_angle(focus_));
    play_cue(feedback, style, style.sounds.open, bounds_.cx());
}

void RadialMenu::close(Feedback &feedback)
{
    if (!open_)
        return;
    dismiss();
    play_cue(feedback, style, style.sounds.close, bounds_.cx());
}

void RadialMenu::dismiss()
{
    open_ = false;
    engaged_ = false;
}

void RadialMenu::set_focus(int index)
{
    if (items_.empty())
        return;
    focus_ = std::clamp(index, 0, static_cast<int>(items_.size()) - 1);
}

void RadialMenu::point(int index, Feedback &feedback)
{
    focus_ = index;
    float x = 0.0f;
    float y = 0.0f;
    wedge_center(index, &x, &y);
    // One detent of the dial: each wedge has its own note and its own place
    // between the speakers.
    const float along = items_.size() > 1
                            ? static_cast<float>(index) / static_cast<float>(items_.size() - 1)
                            : 0.0f;
    play_cue(feedback, style, style.sounds.move, x,
             style.pitch_by_position ? tween::lerp(0.92f, 1.12f, along) : 1.0f);
}

Event RadialMenu::activate(const InputFrame &input, Feedback &feedback, bool closes)
{
    float x = 0.0f;
    float y = 0.0f;
    wedge_center(focus_, &x, &y);
    if (items_[static_cast<std::size_t>(focus_)].disabled)
        return refuse(feedback, style, input, refusal_, x);
    choice_ = focus_;
    flash_.trigger();
    play_cue(feedback, style, style.sounds.activate, x);
    if (style.sounds.rumble > 0.0f)
        feedback.rumble(0.3f * style.sounds.rumble, 0.05f);
    if (closes)
        dismiss();
    return Event::activated;
}

Event RadialMenu::handle(const InputFrame &input, Feedback &feedback)
{
    if (!open_ || items_.empty())
        return Event::none;
    const int count = static_cast<int>(items_.size());
    Event event = Event::none;

    // The stick aims. Its deflection has two thresholds, so a thumb hovering
    // at the edge of the dead zone does not make the wheel twitch.
    const float length =
        std::min(1.0f, std::sqrt(input.stick_x * input.stick_x + input.stick_y * input.stick_y));
    engaged_ = !input.focus_lost && length > (engaged_ ? style.stick_release : style.stick_engage);
    if (engaged_)
    {
        stick_angle_ = std::atan2(input.stick_x, -input.stick_y);
        const int sector = pick(stick_angle_);
        if (sector != focus_)
        {
            point(sector, feedback);
            event = Event::moved;
        }
    }
    else if (input.nav != Direction::none && !input.nav_from_stick)
    {
        // The D-pad turns the dial: right and left go one detent and wrap,
        // because a ring has no end; up and down head for the top and the
        // bottom wedge by the shorter side and refuse softly once there.
        int direction = 0;
        if (input.nav == Direction::right)
            direction = 1;
        else if (input.nav == Direction::left)
            direction = -1;
        else
        {
            const int goal = nearest(input.nav == Direction::up ? 0.0f : kPi);
            const int clockwise = ((goal - focus_) % count + count) % count;
            if (clockwise != 0)
                direction = clockwise <= count / 2 ? 1 : -1;
        }
        if (direction == 0 || count < 2)
        {
            float x = 0.0f;
            float y = 0.0f;
            wedge_center(focus_, &x, &y);
            return refuse(feedback, style, input, refusal_, x);
        }
        point((focus_ + direction + count) % count, feedback);
        event = Event::moved;
    }

    if (style.mode == RadialMode::quick && !held_)
    {
        // The button came up: whatever is pointed at is the answer. A wedge
        // that cannot be used still lets the wheel go.
        const Event released = activate(input, feedback, true);
        dismiss();
        return released;
    }
    if (input.is_pressed(Action::confirm))
        return activate(input, feedback,
                        style.close_on_activate || style.mode == RadialMode::quick);
    if (input.is_pressed(Action::back))
    {
        dismiss();
        play_cue(feedback, style, style.sounds.cancel, bounds_.cx());
        return Event::cancelled;
    }
    return event;
}

void RadialMenu::update(float dt)
{
    const bool still = style.reduced_motion;
    age_ += dt;
    pop_.target = open_ ? 1.0f : 0.0f;
    // The wheel should land, not wobble: the theme's bounce is kept mild.
    pop_.update(dt, std::max(style.omega(), 12.0f), std::max(style.damping(), 0.7f));
    fade_.target = open_ ? 1.0f : 0.0f;
    fade_.update(dt, still ? 40.0f : 18.0f);
    for (std::size_t i = 0; i < lifts_.size(); ++i)
    {
        lifts_[i].target = open_ && static_cast<int>(i) == focus_ ? 1.0f : 0.0f;
        lifts_[i].update(dt, 22.0f, still ? 1.0f : std::clamp(style.damping(), 0.55f, 1.0f));
    }
    if (!items_.empty())
    {
        // The needle is the stick: while it is deflected the needle shows its
        // exact direction, otherwise it rests on the pointed wedge.
        const float focus_angle = wedge_angle(focus_);
        chase(needle_, engaged_ ? stick_angle_ : focus_angle, dt, engaged_ ? 30.0f : 18.0f);
        chase(rim_, focus_angle, dt, std::max(style.omega(), 18.0f));
    }
    if (focus_ != hub_shown_)
    {
        hub_previous_ = hub_shown_;
        hub_shown_ = focus_;
        hub_.start(still ? 0.12f : 0.26f);
    }
    hub_.update(dt);
    flash_.update(dt, 7.0f);
    refusal_.update(dt, 9.0f);
}

void RadialMenu::draw(Canvas &canvas) const
{
    if (!visible() || items_.empty())
        return;
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    const bool still = style.reduced_motion;
    const float fade = tween::clamp01(fade_.value);
    const float cx = bounds_.cx();
    const float cy = bounds_.cy();
    const int count = static_cast<int>(items_.size());

    // The veil. In a glass theme the screen behind is blurred as well, and a
    // blurred screen needs less veil to stop competing with the wheel.
    const bool blurred = style.frosted && canvas.glass != 0;
    const bool own_colour = style.scrim_color.a > 0.0f;
    const Color veil = solid(own_colour ? style.scrim_color : theme.page);
    const float veiled = tween::clamp01(style.scrim * (blurred ? 0.72f : 1.0f));
    if (blurred)
        list.glass(canvas.glass, bounds_, 0.0f, Color{1.0f, 1.0f, 1.0f, fade});
    if (veiled > 0.0f)
        list.rounded_rect(bounds_, 0.0f, veil.with_alpha(veiled * fade));
    // Labels outside the ring have no panel under them. On a veil in the page
    // colour they are page text; on any other they take whichever of black
    // and white reads there.
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const Color on_scrim = own_colour && veiled > 0.01f
                               ? Painter::on(gfx::mix(solid(theme.page), veil, veiled))
                               : paint.page_text();

    const float scale = still ? 1.0f : tween::lerp(0.86f, 1.0f, pop_.value);
    list.push_opacity(tween::clamp01(fade * 1.5f));
    list.push_transform(scale, cx, cy, 0.0f, 0.0f);

    const float thick = std::min(style.thickness, style.radius);
    if (!own_depth(theme.style))
    {
        // One soft shadow under the whole ring lifts it off the veil.
        const float r = style.radius;
        const Color shade = theme.shadow.a > 0.05f ? solid(theme.shadow) : Color{};
        list.shadow({cx - r, cy - r + 14.0f, r * 2.0f, r * 2.0f}, r, 70.0f,
                    shade.with_alpha(theme.dark ? 0.42f : 0.2f));
    }
    if (theme.style == SurfaceStyle::neumorphic)
    {
        // The ring is raised from the page the way the theme raises a card:
        // lit from the top-left, shaded bottom-right. What shows between the
        // wedges and round the hub is the shade: a groove.
        const float r = style.radius;
        const float o = theme.shadow_offset;
        list.shadow({cx - r - o, cy - r - o, r * 2.0f, r * 2.0f}, r, theme.shadow_blur,
                    theme.light);
        list.shadow({cx - r + o, cy - r + o, r * 2.0f, r * 2.0f}, r, theme.shadow_blur,
                    theme.shadow);
    }
    if (theme.style == SurfaceStyle::hard)
    {
        // Every wedge sits up-left of a solid shadow, like the theme's buttons.
        const float step = step_angle();
        const float middle = std::max(style.radius - thick * 0.5f, 1.0f);
        const float gap = std::min(style.gap / middle, step * 0.5f);
        for (int i = 0; i < count; ++i)
        {
            const float lift = still ? 0.0f : lifts_[static_cast<std::size_t>(i)].value;
            list.arc(cx + theme.shadow_offset, cy + theme.shadow_offset,
                     style.radius + style.push * lift, thick,
                     wedge_angle(i) - step * 0.5f + gap * 0.5f, step - gap, theme.shadow, false);
        }
    }

    for (int i = 0; i < count; ++i)
    {
        if (i != focus_)
            draw_wedge(canvas, i, on_scrim);
    }
    draw_wedge(canvas, focus_, on_scrim); // last: its light lies over its neighbours

    const float arrive = tween::cubic_out(age_ / 0.3f);
    if (style.rim)
    {
        // The focus highlight is its own object: a bracket that glides round
        // the rim from wedge to wedge, ahead of the wedges' own springs.
        const float sweep = step_angle() * 0.62f;
        list.arc(cx, cy, style.radius + style.push + kRimReach, 4.0f, rim_.value - sweep * 0.5f,
                 sweep, solid(theme.focus).with_alpha(std::max(theme.focus.a, 0.9f) * arrive));
    }
    if (style.needle)
    {
        // An arrowhead in the channel between the hub and the ring.
        const float at = style.radius - thick - 20.0f;
        const float hub =
            square_language(theme) ? style.hub_radius * 0.74f * 1.42f : style.hub_radius;
        if (at - 9.0f > hub || style.hub_radius <= 0.0f)
        {
            const float x = cx + std::sin(needle_.value) * at;
            const float y = cy - std::cos(needle_.value) * at;
            list.triangle({x - 10.0f, y - 9.0f, 20.0f, 18.0f}, on_scrim.with_alpha(0.9f * arrive),
                          0.0f, needle_.value);
        }
    }
    draw_hub(canvas);

    list.pop_transform();
    list.pop_opacity();
}

void RadialMenu::draw_wedge(Canvas &canvas, int index, Color on_scrim) const
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const RadialItem &item = items_[static_cast<std::size_t>(index)];
    const bool still = style.reduced_motion;
    const float cx = bounds_.cx();
    const float cy = bounds_.cy();

    // ---- where it is ----
    const float raw =
        still ? tween::clamp01(age_ / 0.2f)
              : (style.entrance_step > 0.0f
                     ? tween::clamp01((age_ - static_cast<float>(index) * style.entrance_step) /
                                      kFanTime)
                     : 1.0f);
    const float arrive = tween::cubic_out(raw);
    if (arrive <= 0.01f)
        return;
    const float pop = still ? 1.0f : tween::back_out(raw);
    const float lift = lifts_[static_cast<std::size_t>(index)].value;
    const float lit = tween::clamp01(lift);
    float angle = wedge_angle(index);
    if (index == focus_)
        angle += shake(refusal_.value, canvas.time, 0.05f, 9.0f); // a refusal rattles it
    const float thick = std::min(style.thickness, style.radius);
    const float outer = style.radius + (still ? 0.0f : style.push * lift) - kFan * (1.0f - pop);
    const float inner = outer - thick;
    const float middle = outer - thick * 0.5f;
    const float step = step_angle();
    const float gap = std::min(style.gap / std::max(middle, 1.0f), step * 0.5f);
    const float start = angle - step * 0.5f + gap * 0.5f;
    const float sweep = step - gap;

    // ---- what it is made of ----
    const bool glows = theme.style == SurfaceStyle::glow;
    const bool glass = theme.style == SurfaceStyle::glass;
    const Color tone = solid(item.accent.a > 0.0f ? item.accent : theme.primary);
    // A glass wedge stays translucent over the veil; every other language
    // gets its panel colour made opaque, so the page never shows through.
    const Color rest = glass ? theme.surface : opaque_over(theme.page, theme.surface);
    // Lit consoles carry the colour on the edge, not in the fill.
    const Color lit_fill = glows ? gfx::mix(solid(rest), tone, 0.24f) : tone;
    Color fill = gfx::mix(rest, lit_fill, lit);
    if (index == focus_)
        fill = gfx::mix(fill, Color{1.0f, 1.0f, 1.0f, 1.0f}, 0.6f * flash_.value);
    const Color lit_ink =
        glows ? tone : (item.accent.a > 0.0f ? Painter::on(tone) : solid(theme.on_primary));
    Color ink = gfx::mix(theme.text, lit_ink, lit);
    // Dimmed, but still readable on the plate of a pointed wedge.
    if (item.disabled)
        ink = ink.with_alpha(tween::lerp(0.42f, 0.66f, lit));

    list.push_opacity(arrive);
    const float light =
        style.light >= 0.0f ? style.light : (glows ? 1.0f : (theme.dark ? 0.55f : 0.0f));
    if (lit > 0.02f && light > 0.0f)
    {
        // Light spilling from the pointed wedge: wider, fainter copies of
        // its own shape.
        const float breath = still ? 0.85f : 0.75f + 0.25f * breathe(canvas.time);
        constexpr float kSpread[] = {6.0f, 14.0f, 24.0f};
        constexpr float kStrength[] = {0.2f, 0.11f, 0.05f};
        for (int g = 0; g < 3; ++g)
        {
            const float wider = kSpread[g] / std::max(middle, 1.0f);
            list.arc(cx, cy, outer + kSpread[g], thick + 2.0f * kSpread[g], start - wider,
                     sweep + 2.0f * wider, tone.with_alpha(kStrength[g] * lit * breath * light),
                     false);
        }
    }
    list.arc(cx, cy, outer, thick, start, sweep, fill, false);

    // The theme's stroke, drawn as the wedge's four edges.
    float line = theme.border;
    if (theme.style == SurfaceStyle::outline || glows || glass)
        line = std::max(line, 1.5f);
    Color edge_out = theme.outline;
    Color edge_in = theme.outline;
    if (glows)
        edge_out = edge_in = gfx::mix(theme.outline, tone, lit);
    else if (glass)
        edge_out = edge_in = theme.light;
    else if (theme.style == SurfaceStyle::bevel)
    {
        edge_out = theme.light;
        edge_in = theme.shadow;
    }
    if (line > 0.0f && (edge_out.a > 0.0f || edge_in.a > 0.0f))
    {
        list.arc(cx, cy, outer, line, start, sweep, edge_out, false);
        list.arc(cx, cy, inner + line, line, start, sweep, edge_in, false);
        // The straight sides: each is moved half its width into the wedge,
        // so two neighbours keep the whole gap between them.
        const float half = line * 0.5f;
        for (int side = 0; side < 2; ++side)
        {
            const float a = side == 0 ? start : start + sweep;
            const float way = side == 0 ? half : -half;
            const float sx = std::sin(a);
            const float sy = -std::cos(a);
            const float ox = std::cos(a) * way;
            const float oy = std::sin(a) * way;
            list.line(cx + sx * (inner + half) + ox, cy + sy * (inner + half) + oy,
                      cx + sx * (outer - half) + ox, cy + sy * (outer - half) + oy, line,
                      side == 0 ? edge_out : edge_in);
        }
    }

    // ---- what it holds ----
    const float mx = cx + std::sin(angle) * middle;
    const float my = cy - std::cos(angle) * middle;
    const float size = style.icon_size * (still ? 1.0f : 1.0f + 0.1f * lift);
    const bool inside = style.labels == RadialLabels::inside;
    if (inside)
    {
        // Icon over label, the pair centred on the wedge's middle. The label
        // is cut to the chord it has at that radius.
        const float chord = 2.0f * middle * std::sin(sweep * 0.5f) - 20.0f;
        const std::string text =
            fit_label(paint, item.label, style.label_size, std::max(chord, 40.0f));
        if (icon)
        {
            const float block = size + 6.0f + style.label_size;
            const float top = my - block * 0.5f;
            icon(canvas, {mx - size * 0.5f, top, size, size}, item, index, lit, ink);
            paint.label(text, mx, top + size + 6.0f + style.label_size * 0.82f, style.label_size,
                        ink, gfx::Align::center);
        }
        else
        {
            paint.label(text, mx, my + style.label_size * 0.35f, style.label_size, ink,
                        gfx::Align::center);
        }
    }
    else if (icon)
    {
        icon(canvas, {mx - size * 0.5f, my - size * 0.5f, size, size}, item, index, lit, ink);
    }
    else
    {
        list.circle(mx, my, 6.0f + 2.0f * lit, ink);
    }

    if (style.labels == RadialLabels::outside)
    {
        // The label continues the wedge's direction and is anchored on the
        // side facing the wheel, so no label ever grows into it. It keeps
        // its place while the wedge springs: text that moves is hard to read.
        const float reach = style.radius + style.label_gap;
        const float base = wedge_angle(index);
        const float side = std::sin(base);
        const float up = std::cos(base);
        const float x = cx + side * reach;
        const float y = cy - up * reach;
        const gfx::Align align = side > 0.3f    ? gfx::Align::left
                                 : side < -0.3f ? gfx::Align::right
                                                : gfx::Align::center;
        const float right = bounds_.x + bounds_.w - 24.0f;
        const float left = bounds_.x + 24.0f;
        const float room = align == gfx::Align::left
                               ? right - x
                               : (align == gfx::Align::right
                                      ? x - left
                                      : std::min(2.0f * std::min(x - left, right - x), 320.0f));
        paint.label(fit_label(paint, item.label, style.label_size, std::max(room, 40.0f)), x,
                    y + style.label_size * 0.35f * (1.0f - up), style.label_size,
                    on_scrim.with_alpha((0.74f + 0.26f * lit) * (item.disabled ? 0.55f : 1.0f)),
                    align);
    }
    list.pop_opacity();
}

void RadialMenu::draw_hub(Canvas &canvas) const
{
    if (style.hub_radius <= 0.0f)
        return;
    const Theme &theme = style.theme;
    const bool still = style.reduced_motion;
    const bool square = square_language(theme);
    // A square plate has to fit inside the ring with its corners.
    const float half = square ? style.hub_radius * 0.74f : style.hub_radius;
    const Rect plate{bounds_.cx() - half, bounds_.cy() - half, half * 2.0f, half * 2.0f};
    canvas.list.push_opacity(tween::stagger(age_, 1, 0.05f, 0.3f));
    draw_overlay_panel(canvas, theme, plate, style.frosted, 0.55f,
                       square ? std::min(theme.radius_card, half) : half);

    // The words sit in the square a round plate has room for.
    const float room = square ? half - 18.0f : half * 0.74f;
    const Rect area{bounds_.cx() - room, bounds_.cy() - room, room * 2.0f, room * 2.0f};
    if (hub_.running)
    {
        // The old item leaves quickly; the new one arrives a beat later.
        const float t = hub_.progress();
        const float coming = tween::clamp01((t - 0.2f) / 0.8f);
        draw_hub_item(canvas, area, hub_previous_, 1.0f - tween::smoothstep(t * 2.4f),
                      still ? 0.0f : -8.0f * tween::cubic_in(tween::clamp01(t * 2.4f)));
        draw_hub_item(canvas, area, hub_shown_, tween::smoothstep(coming),
                      still ? 0.0f : 12.0f * (1.0f - tween::quint_out(coming)));
    }
    else
    {
        draw_hub_item(canvas, area, hub_shown_, 1.0f, 0.0f);
    }
    canvas.list.pop_opacity();
}

void RadialMenu::draw_hub_item(Canvas &canvas, const Rect &area, int index, float alpha,
                               float drop) const
{
    if (alpha <= 0.01f || index < 0 || index >= static_cast<int>(items_.size()))
        return;
    const Theme &theme = style.theme;
    const RadialItem &item = items_[static_cast<std::size_t>(index)];
    canvas.list.push_opacity(alpha);
    if (hub)
    {
        hub(canvas, area, item, index, alpha);
        canvas.list.pop_opacity();
        return;
    }
    Painter paint(canvas.list, canvas.fonts, theme, canvas.glass);
    const float small = style.description_size;
    const std::vector<std::string> lines = wrap_body(paint, item.description, small, area.w, 2);
    const float block = style.title_size + (lines.empty() ? 0.0f : 8.0f) +
                        static_cast<float>(lines.size()) * small * 1.35f +
                        (item.value.empty() ? 0.0f : small * 1.6f);
    const float x = area.cx() + (index == focus_ ? shake(refusal_.value, canvas.time, 8.0f) : 0.0f);
    float y = area.cy() - block * 0.5f + drop;
    paint.label(fit_label(paint, item.label, style.title_size, area.w), x,
                y + style.title_size * 0.84f, style.title_size,
                theme.text.with_alpha(item.disabled ? 0.55f : 1.0f), gfx::Align::center);
    y += style.title_size + 8.0f;
    for (const std::string &line : lines)
    {
        paint.body(line, x, y + small * 0.95f, small, theme.text_muted, gfx::Align::center);
        y += small * 1.35f;
    }
    if (!item.value.empty())
    {
        // What a disabled wedge says here is why it cannot be used.
        const Color note = item.disabled ? gfx::mix(theme.danger, theme.text, 0.25f) : theme.text;
        paint.label(fit_label(paint, item.value, small, area.w), x, y + small * 1.25f, small, note,
                    gfx::Align::center);
    }
    canvas.list.pop_opacity();
}

} // namespace hui::ui
