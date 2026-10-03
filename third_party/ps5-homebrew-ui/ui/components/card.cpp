// ps5-homebrew-ui - Component: Card.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/components/card.hpp"

#include <algorithm>
#include <cmath>

namespace hui::ui
{

using gfx::Color;
using gfx::Rect;

namespace
{

const Color kWhite{1.0f, 1.0f, 1.0f, 1.0f};

// Languages whose surfaces draw their own edge inside the rectangle: the
// picture is set inside that edge instead of covering it.
bool framed(const Theme &theme)
{
    return theme.style == SurfaceStyle::bevel || theme.style == SurfaceStyle::pixel ||
           theme.corner == Corner::pixel;
}

// Languages in which elevation is a soft shadow. The others (offset shadows,
// outlines, pixels, pen strokes) say "focused" with their ring alone.
bool casts_soft_shadow(const Theme &theme)
{
    return theme.style == SurfaceStyle::flat || theme.style == SurfaceStyle::soft ||
           theme.style == SurfaceStyle::gloss || theme.style == SurfaceStyle::glass;
}

// Painter::focus_ring lights these styles with a glow, and a glow is filled:
// over a picture it would tint the picture. The card splits it instead, the
// light under the art and the stroke over it.
bool ring_has_light(const Theme &theme)
{
    return theme.style == SurfaceStyle::glow ||
           ((theme.style == SurfaceStyle::soft || theme.style == SurfaceStyle::gloss ||
             theme.style == SurfaceStyle::glass) &&
            theme.focus.a > 0.9f);
}

// The corner a picture needs so it stays inside the themed outline.
float image_radius(const Theme &theme, const Rect &image, float radius)
{
    const float limit = std::min(image.w, image.h) * 0.5f;
    if (framed(theme))
        return 0.0f;
    // A circle of this radius touches the 45 degree cut from the inside.
    if (theme.corner == Corner::chamfer)
        return std::min(radius * 1.71f, limit);
    return std::min(radius, limit);
}

// The part of `uv` that fills a rectangle of another shape without
// stretching: the middle of the picture, cropped ("cover" fit).
Rect cover_uv(const Rect &uv, float image_aspect, const Rect &target)
{
    if (target.h <= 0.0f || image_aspect <= 0.0f)
        return uv;
    const float wanted = target.w / target.h;
    Rect out = uv;
    if (wanted > image_aspect)
    {
        const float keep = image_aspect / wanted;
        out.y = uv.y + uv.h * (1.0f - keep) * 0.5f;
        out.h = uv.h * keep;
    }
    else
    {
        const float keep = wanted / image_aspect;
        out.x = uv.x + uv.w * (1.0f - keep) * 0.5f;
        out.w = uv.w * keep;
    }
    return out;
}

float chip_radius(const Theme &theme, const Rect &r)
{
    return theme.pill_chips ? r.h * 0.5f : std::min(theme.radius, r.h * 0.5f);
}

// A small themed plate for a mark that sits on the picture.
void mark(Painter &paint, const Theme &theme, const Rect &r, float radius, Color fill)
{
    paint.fill(r, radius, fill);
    // Languages built on outlines outline their marks too.
    if (theme.style == SurfaceStyle::hard || theme.style == SurfaceStyle::pixel ||
        theme.style == SurfaceStyle::sketch)
        paint.stroke(r, radius, std::min(theme.border, 2.5f), theme.outline);
}

float text_x(gfx::Align align, const Rect &r, float inset)
{
    if (align == gfx::Align::center)
        return r.cx();
    return align == gfx::Align::right ? r.x + r.w - inset : r.x + inset;
}

} // namespace

float card_text_height(const CardLook &look)
{
    if (look.text != CardText::below)
        return 0.0f;
    const float subtitle = look.subtitle_size > 0.0f ? look.subtitle_size * 1.3f : 0.0f;
    return look.text_gap + look.title_size * 1.1f + subtitle + 2.0f;
}

float card_height(const CardLook &look, float width)
{
    const float pad = look.plate ? look.plate_padding : 0.0f;
    const float aspect = look.art_aspect > 0.0f ? look.art_aspect : 1.0f;
    return (width - 2.0f * pad) / aspect + card_text_height(look) + 2.0f * pad;
}

Rect card_art(const CardLook &look, const Rect &card)
{
    const Rect in = look.plate ? card.inset(look.plate_padding) : card;
    const float room = std::max(in.h - card_text_height(look), 0.0f);
    const float height = look.art_aspect > 0.0f ? std::min(in.w / look.art_aspect, room) : room;
    return {in.x, in.y, in.w, height};
}

Rect card_frame(const CardLook &look, const Rect &card)
{
    return look.plate ? card : card_art(look, card);
}

float card_radius(const ComponentStyle &style, const CardLook &look, const Rect &frame)
{
    const float wanted = look.radius >= 0.0f ? look.radius : style.theme.radius_card;
    // Very round languages stay round, but a small card never turns into a blob.
    return std::min(wanted, std::min(frame.w, frame.h) * 0.22f);
}

float card_scale(const ComponentStyle &style, const CardLook &look, const CardState &state)
{
    if (style.reduced_motion)
        return 1.0f;
    const float emphasis = state.emphasis < 0.0f ? state.focus : state.emphasis;
    return 1.0f + (look.focus_scale - 1.0f) * tween::clamp01(emphasis) -
           look.press_scale * tween::clamp01(state.press);
}

float card_lift(const ComponentStyle &style, const CardLook &look, const CardState &state)
{
    return style.reduced_motion ? 0.0f : look.lift * tween::clamp01(state.focus);
}

float card_ring_reach(const Theme &theme)
{
    switch (theme.style)
    {
    case SurfaceStyle::hard:
        return 2.0f * theme.border + 3.0f + theme.shadow_offset;
    case SurfaceStyle::bevel:
        return 12.0f;
    case SurfaceStyle::glow:
        return 7.0f;
    default:
        return theme.focus_gap + theme.focus_width;
    }
}

Rect card_placed(const Rect &card, const Rect &frame, float scale, float lift)
{
    return {card.cx() + (frame.x - card.cx()) * scale,
            card.cy() + (frame.y - card.cy()) * scale - lift, frame.w * scale, frame.h * scale};
}

void draw_card_halo(Canvas &canvas, const ComponentStyle &style, const CardLook &look,
                    const Rect &frame, Color glow, float amount)
{
    amount = tween::clamp01(amount);
    if (amount <= 0.01f)
        return;
    const Theme &theme = style.theme;
    const float radius = card_radius(style, look, frame);
    const float idle = style.reduced_motion ? 1.0f : 0.8f + 0.2f * breathe(canvas.time);
    if (look.glow)
    {
        const Color color =
            look.glow_color.a > 0.0f ? look.glow_color : (glow.a > 0.0f ? glow : theme.focus);
        canvas.list.glow(frame, radius, 34.0f, color.with_alpha(0.6f * amount * idle));
    }
    if (look.ring && theme.style == SurfaceStyle::bevel)
    {
        // A dotted rectangle alone is lost beside a picture. Old desktops
        // also put the selection colour behind the chosen icon: so does this.
        canvas.list.rounded_rect(frame.inset(-6.0f), 0.0f, theme.primary.with_alpha(amount));
    }
    else if (look.ring && ring_has_light(theme))
    {
        const bool lit = theme.style == SurfaceStyle::glow;
        canvas.list.glow(frame, radius, lit ? 22.0f : 14.0f,
                         theme.focus.with_alpha((lit ? 0.55f : 0.35f) * amount));
    }
}

void draw_card_ring(Canvas &canvas, const ComponentStyle &style, const CardLook &look,
                    const Rect &frame, float amount)
{
    amount = tween::clamp01(amount);
    if (!look.ring || amount <= 0.01f)
        return;
    const Theme &theme = style.theme;
    Painter paint(canvas.list, canvas.fonts, theme, canvas.glass);
    const float radius = card_radius(style, look, frame);
    if (theme.style == SurfaceStyle::bevel)
    {
        // The dotted rectangle is drawn inside its target: over a picture
        // it would be lost, so it goes around the card and its selection block.
        paint.focus_ring(frame.inset(-17.0f), 0.0f, amount);
    }
    else if (ring_has_light(theme))
    {
        // The stroke of Painter::focus_ring without its (filled) glow.
        const bool lit = theme.style == SurfaceStyle::glow;
        const float reach = lit ? 5.0f : theme.focus_gap + theme.focus_width;
        const float width = lit ? 2.0f : theme.focus_width;
        paint.stroke(frame.inset(-reach), radius > 0.0f ? radius + reach : 0.0f, width,
                     theme.focus.with_alpha(amount));
    }
    else
    {
        paint.focus_ring(frame, radius, amount);
    }
}

void draw_card(Canvas &canvas, const ComponentStyle &style, const CardLook &look, const Rect &card,
               const CardItem &item, const CardState &state, const CardArt &art)
{
    const Theme &theme = style.theme;
    gfx::DrawList &list = canvas.list;
    Painter paint(list, canvas.fonts, theme, canvas.glass);
    const float focus = tween::clamp01(state.focus);
    const float press = tween::clamp01(state.press);
    const float checked =
        state.selected < 0.0f ? (item.selected ? 1.0f : 0.0f) : tween::clamp01(state.selected);

    const float opacity = (item.disabled ? 0.42f : 1.0f) * (1.0f - look.dim * (1.0f - focus));
    list.push_opacity(opacity);
    list.push_transform(card_scale(style, look, state), card.cx(), card.cy(), 0.0f,
                        -card_lift(style, look, state));

    const Rect frame = card_frame(look, card);
    const float radius = card_radius(style, look, frame);

    // ---- under the card: its shadow and its light ----
    if (look.shadow && focus > 0.01f && casts_soft_shadow(theme))
    {
        const Color dark{theme.shadow.r, theme.shadow.g, theme.shadow.b,
                         std::max(theme.shadow.a, 0.34f) * focus};
        list.shadow({frame.x + 4.0f, frame.y + 10.0f + 8.0f * focus, frame.w - 8.0f, frame.h},
                    radius, 22.0f + 16.0f * focus, dark);
    }
    if (state.marks)
        draw_card_halo(canvas, style, look, frame, item.accent, focus);

    // ---- the plate and the art ----
    Rect body = card;
    if (look.plate)
        body = paint.surface(card, radius, theme.surface, theme.outline, 1.0f - press);
    Rect picture = card_art(look, body);
    float picture_radius = look.plate ? std::max(radius - look.plate_padding, 0.0f) : radius;
    const Color well = item.top.a > 0.0f ? item.top : theme.surface_high;
    if (!look.plate)
    {
        // The art is a surface of the theme: it gets the language's shadow,
        // edge and pressed state, and shows through until a picture arrives.
        picture = paint.surface(picture, radius, well, theme.outline, 1.0f - press);
    }
    const Rect bed = picture; // the themed shape the picture lies in
    if (framed(theme))
        picture = picture.inset(theme.border);
    picture_radius = image_radius(theme, picture, picture_radius);

    if (art)
        art(canvas, picture, picture_radius, item, focus);
    else if (item.texture != 0)
        list.image(item.texture, picture, cover_uv(item.uv, item.image_aspect, picture), kWhite,
                   picture_radius);
    else if (item.top.a > 0.0f)
        list.gradient_rect(picture, picture_radius, item.top,
                           item.bottom.a > 0.0f ? item.bottom : item.top);
    else if (look.plate)
        paint.well(picture, picture_radius, well);

    // ---- text over the art ----
    const bool bar = item.progress >= 0.0f;
    const float bar_room = bar ? look.progress_height + 10.0f : 0.0f;
    const float words = tween::clamp01(state.text);
    if (look.text == CardText::over && !item.title.empty() && words > 0.0f)
    {
        list.push_opacity(words);
        const bool two = look.subtitle_size > 0.0f && !item.subtitle.empty();
        const float block = look.title_size * 1.15f + (two ? look.subtitle_size * 1.3f : 0.0f);
        const float height =
            std::min(picture.h, block + look.text_inset + bar_room + look.title_size * 2.2f);
        const Rect shade{picture.x, picture.y + picture.h - height, picture.w, height};
        list.gradient_rect(shade, std::min(picture_radius, height * 0.5f),
                           look.scrim.with_alpha(0.0f), look.scrim);
        const Color ink = Painter::on({look.scrim.r, look.scrim.g, look.scrim.b, 1.0f});
        const float room = std::max(picture.w - 2.0f * look.text_inset, 20.0f);
        const float x = text_x(look.align, picture, look.text_inset);
        float baseline = picture.y + picture.h - look.text_inset - bar_room;
        if (two)
        {
            paint.body(fit_body(paint, item.subtitle, look.subtitle_size, room), x,
                       baseline - look.subtitle_size * 0.2f, look.subtitle_size,
                       ink.with_alpha(0.78f), look.align);
            baseline -= look.subtitle_size * 1.3f;
        }
        paint.label(fit_label(paint, item.title, look.title_size, room), x,
                    baseline - look.title_size * 0.2f, look.title_size, ink, look.align);
        list.pop_opacity();
    }

    // ---- marks on the art ----
    const float edge = std::max(10.0f, picture_radius * 0.45f);
    if (bar)
    {
        const Rect track{picture.x + edge,
                         picture.y + picture.h - edge - look.progress_height + 2.0f,
                         picture.w - 2.0f * edge, look.progress_height};
        const float round =
            theme.corner == Corner::round && theme.radius >= 2.0f ? track.h * 0.5f : 0.0f;
        // The track takes the colour that reads against the fill, so the bar
        // shows on any picture.
        list.rounded_rect(track, round, Painter::on(theme.accent).with_alpha(0.5f));
        const float filled = track.w * tween::clamp01(item.progress);
        if (filled > 1.0f)
            list.rounded_rect({track.x, track.y, filled, track.h}, round, theme.accent);
    }
    if (!item.badge.empty())
    {
        const float size = look.badge_size;
        const float width = paint.label_width(item.badge, size) + size * 1.4f;
        const float height = size + 14.0f;
        const bool right = look.badge_corner == CardCorner::top_right ||
                           look.badge_corner == CardCorner::bottom_right;
        const bool low = look.badge_corner == CardCorner::bottom_left ||
                         look.badge_corner == CardCorner::bottom_right;
        const Rect pill{right ? picture.x + picture.w - edge - width : picture.x + edge,
                        low ? picture.y + picture.h - edge - height - bar_room : picture.y + edge,
                        width, height};
        mark(paint, theme, pill, chip_radius(theme, pill), theme.accent);
        paint.label(item.badge, pill.cx(), pill.cy() + size * 0.35f, size,
                    Painter::on(theme.accent), gfx::Align::center);
    }
    if (checked > 0.01f)
    {
        // The check pops: it overshoots its size once and settles.
        const float size =
            look.check_size * (style.reduced_motion ? 1.0f : tween::back_out(checked));
        const bool left = look.badge_corner == CardCorner::top_right;
        const float cx = left ? picture.x + edge + look.check_size * 0.5f
                              : picture.x + picture.w - edge - look.check_size * 0.5f;
        const float cy = picture.y + edge + look.check_size * 0.5f;
        const Rect box{cx - size * 0.5f, cy - size * 0.5f, size, size};
        list.push_opacity(tween::clamp01(checked * 2.0f));
        mark(paint, theme, box, theme.radius < 2.0f ? 0.0f : size * 0.5f, theme.accent);
        const Color ink = Painter::on(theme.accent);
        const float width = std::max(3.0f, size * 0.11f);
        list.line(box.x + size * 0.27f, box.y + size * 0.52f, box.x + size * 0.44f,
                  box.y + size * 0.68f, width, ink);
        list.line(box.x + size * 0.44f, box.y + size * 0.68f, box.x + size * 0.74f,
                  box.y + size * 0.34f, width, ink);
        list.pop_opacity();
    }

    // The outline again, over the picture that has just covered it.
    if (!look.plate && !framed(theme) && theme.border > 0.0f)
        paint.stroke(bed, radius, theme.border,
                     theme.style == SurfaceStyle::glass ? theme.light : theme.outline);

    // ---- text under the art ----
    if (look.text == CardText::below && !item.title.empty() && words > 0.0f)
    {
        list.push_opacity(words);
        const Color ink = look.plate || look.on_panel ? theme.text : paint.page_text();
        const Color quiet =
            look.plate || look.on_panel ? theme.text_muted : paint.page_text_muted();
        const Rect in = look.plate ? body.inset(look.plate_padding) : body;
        const float inset = look.align == gfx::Align::left && !look.plate ? 2.0f : 0.0f;
        const float room = std::max(in.w - 2.0f * inset, 20.0f);
        const float x = text_x(look.align, in, inset);
        const float first = bed.y + bed.h + look.text_gap + look.title_size * 0.82f;
        paint.label(fit_label(paint, item.title, look.title_size, room), x, first, look.title_size,
                    ink, look.align);
        if (look.subtitle_size > 0.0f && !item.subtitle.empty())
            paint.body(fit_body(paint, item.subtitle, look.subtitle_size, room), x,
                       first + look.subtitle_size * 1.3f, look.subtitle_size,
                       gfx::mix(quiet, ink, focus * 0.5f), look.align);
        list.pop_opacity();
    }

    if (state.marks)
        draw_card_ring(canvas, style, look, look.plate ? body : bed, focus);

    list.pop_transform();
    list.pop_opacity();
}

void Card::draw(Canvas &canvas, const Rect &card, const CardItem &item, float focus,
                float press) const
{
    CardState state;
    state.focus = focus;
    state.press = press;
    draw_card(canvas, style, style, card, item, state, art);
}

void Card::draw(Canvas &canvas, const Rect &card, const CardItem &item,
                const CardState &state) const
{
    draw_card(canvas, style, style, card, item, state, art);
}

} // namespace hui::ui
