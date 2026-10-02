// ps5-homebrew-ui - The thirty built-in themes.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ten themes are design languages in their own right (glass, neo-brutalism,
// neumorphism, ...). Twenty are modelled on well-known web frameworks: their
// colours, corner radii, borders, shadows and type weights were read from the
// frameworks' own component pages and scaled from desktop to television size
// (about 1.6x: a 4 px web radius is 7 here). A theme is an homage to a look,
// rebuilt with this kit's shapes; no framework code or asset is used.

#include "ui/theme.hpp"

#include <array>

namespace hui::ui
{

namespace
{

using gfx::BackdropMode;
using gfx::Color;

Color rgb(std::uint32_t hex, float alpha = 1.0f)
{
    return Color::rgb(hex, alpha);
}

gfx::BackdropSpec backdrop(BackdropMode mode, std::uint32_t c0, std::uint32_t c1,
                           std::uint32_t c2 = 0, std::uint32_t c3 = 0, float p0 = 0.0f,
                           float p1 = 0.0f, float p2 = 0.0f)
{
    gfx::BackdropSpec spec;
    spec.mode = mode;
    spec.colors[0] = rgb(c0);
    spec.colors[1] = rgb(c1);
    spec.colors[2] = rgb(c2);
    spec.colors[3] = rgb(c3);
    spec.params[0] = p0;
    spec.params[1] = p1;
    spec.params[2] = p2;
    return spec;
}

// A plain page in one colour.
gfx::BackdropSpec plain(std::uint32_t colour)
{
    return backdrop(BackdropMode::gradient, colour, colour);
}

// The starting point of the light, flat, bordered web looks.
Theme web_light(const char *id, const char *name, const char *family, const char *summary)
{
    Theme t{};
    t.id = id;
    t.name = name;
    t.family = family;
    t.summary = summary;
    t.backdrop = plain(0xffffff);
    t.page = rgb(0xffffff);
    t.surface = rgb(0xffffff);
    t.surface_high = rgb(0xf1f3f5);
    t.text = rgb(0x212529);
    t.text_muted = rgb(0x6c757d);
    t.primary = rgb(0x0d6efd);
    t.on_primary = rgb(0xffffff);
    t.secondary = rgb(0xffffff);
    t.on_secondary = rgb(0x212529);
    t.accent = t.primary;
    t.outline = rgb(0xdee2e6);
    t.focus = t.primary;
    t.shadow = rgb(0x000000, 0.0f);
    t.light = rgb(0xffffff);
    t.style = SurfaceStyle::flat;
    t.radius = 10.0f;
    t.radius_card = 13.0f;
    t.border = 1.5f;
    t.shadow_offset = 0.0f;
    t.shadow_blur = 0.0f;
    t.heading = FontRole::semibold;
    t.label = FontRole::semibold;
    t.omega = 18.0f;
    t.dark = false;
    return t;
}

Theme web_dark(const char *id, const char *name, const char *family, const char *summary)
{
    Theme t = web_light(id, name, family, summary);
    t.backdrop = plain(0x111827);
    t.page = rgb(0x111827);
    t.surface = rgb(0x1f2937);
    t.surface_high = rgb(0x374151);
    t.text = rgb(0xffffff);
    t.text_muted = rgb(0x9ca3af);
    t.secondary = rgb(0x1f2937);
    t.on_secondary = rgb(0xffffff);
    t.outline = rgb(0x374151);
    t.dark = true;
    return t;
}

// ---- design languages --------------------------------------------------

Theme acrylic()
{
    Theme t = web_dark("acrylic", "Acrylic", "Frosted glass",
                       "Blurred translucent panels, hairline light edges, small radii");
    t.backdrop = backdrop(BackdropMode::aurora, 0x0b1026, 0x1b1f4a, 0x3a5bd9, 0xc04bd6);
    t.page = rgb(0x14182e);
    t.surface = rgb(0x1c2140, 0.5f);
    t.surface_high = rgb(0xffffff, 0.14f);
    t.text_muted = rgb(0xffffff, 0.7f);
    t.primary = rgb(0x76d6ff);
    t.on_primary = rgb(0x04263a);
    t.secondary = rgb(0xffffff, 0.1f);
    t.accent = rgb(0x76d6ff);
    t.outline = rgb(0xffffff, 0.26f);
    t.focus = rgb(0xffffff);
    t.shadow = rgb(0x000000, 0.4f);
    t.light = rgb(0xffffff, 0.3f);
    t.style = SurfaceStyle::glass;
    t.radius = 8.0f;
    t.radius_card = 14.0f;
    t.shadow_offset = 10.0f;
    t.shadow_blur = 28.0f;
    t.heading = FontRole::display;
    return t;
}

Theme brutal()
{
    Theme t = web_light("brutal", "Brutal", "Neo-brutalism",
                        "Square corners, thick black outlines, hard offset shadows, loud flats");
    t.backdrop = plain(0xfff3c9);
    t.page = rgb(0xfff3c9);
    t.surface_high = rgb(0xa6e3ff);
    t.text = rgb(0x000000);
    t.text_muted = rgb(0x000000, 0.72f);
    t.primary = rgb(0xff5c8a);
    t.on_primary = rgb(0x000000);
    t.on_secondary = rgb(0x000000);
    t.accent = rgb(0x36d67e);
    t.outline = rgb(0x000000);
    t.focus = rgb(0x2f5bff);
    t.shadow = rgb(0x000000);
    t.style = SurfaceStyle::hard;
    t.radius = 0.0f;
    t.radius_card = 0.0f;
    t.border = 4.0f;
    t.shadow_offset = 8.0f;
    t.heading = FontRole::display;
    t.caps = true;
    t.tracking = 1.0f;
    t.omega = 26.0f;
    t.damping = 0.5f;
    t.sounds = audio::SoundSet::paper;
    t.pill_switches = false;
    return t;
}

Theme clay()
{
    Theme t = web_light("clay", "Clay", "Neumorphism",
                        "Everything is the page colour, raised and pressed by two shadows");
    t.backdrop = plain(0xe6ebf2);
    t.page = rgb(0xe6ebf2);
    t.surface = rgb(0xe6ebf2);
    t.surface_high = rgb(0xd9e0ea);
    t.text = rgb(0x38445a);
    t.text_muted = rgb(0x7a879b);
    t.primary = rgb(0x4f86ee);
    t.secondary = rgb(0xe6ebf2);
    t.on_secondary = rgb(0x38445a);
    t.accent = rgb(0x4f86ee);
    t.outline = rgb(0xc3cddb, 0.0f);
    t.focus = rgb(0x4f86ee);
    t.shadow = rgb(0x9fb0c8, 0.85f);
    t.light = rgb(0xffffff, 0.95f);
    t.style = SurfaceStyle::neumorphic;
    t.radius = 20.0f;
    t.radius_card = 34.0f;
    t.border = 0.0f;
    t.shadow_offset = 8.0f;
    t.shadow_blur = 14.0f;
    t.heading = FontRole::display;
    t.omega = 11.0f;
    t.sounds = audio::SoundSet::paper;
    return t;
}

Theme tiles()
{
    Theme t = web_dark("tiles", "Tiles", "Flat",
                       "No depth at all: square colour blocks, big type, capitals");
    t.backdrop = plain(0x0e0f13);
    t.page = rgb(0x0e0f13);
    t.surface = rgb(0x22242c);
    t.surface_high = rgb(0x363944);
    t.text_muted = rgb(0xffffff, 0.62f);
    t.primary = rgb(0x1e88ff);
    t.secondary = rgb(0x363944);
    t.accent = rgb(0x00c2a8);
    t.outline = rgb(0xffffff, 0.0f);
    t.focus = rgb(0xffffff);
    t.radius = 0.0f;
    t.radius_card = 0.0f;
    t.border = 0.0f;
    t.focus_width = 4.0f;
    t.heading = FontRole::display;
    t.caps = true;
    t.tracking = 2.0f;
    t.omega = 24.0f;
    t.pill_switches = false;
    return t;
}

Theme gloss()
{
    Theme t = web_light("gloss", "Gloss", "Skeuomorphic",
                        "Gradients, glassy highlights and dark edges: buttons that look pressable");
    t.backdrop =
        backdrop(BackdropMode::gradient, 0xdfe6ee, 0xb9c5d3, 0xffffff, 0, 0.5f, 0.0f, 0.6f);
    t.page = rgb(0xcdd6e0);
    t.surface = rgb(0xeef2f6);
    t.surface_high = rgb(0xb4c0ce);
    t.text = rgb(0x1b2430);
    t.text_muted = rgb(0x566579);
    t.primary = rgb(0x2f8cf0);
    t.secondary = rgb(0xe3e9f0);
    t.on_secondary = rgb(0x1b2430);
    t.accent = rgb(0x35b44a);
    t.outline = rgb(0x66768c);
    t.focus = rgb(0x2f8cf0);
    t.shadow = rgb(0x1b2430, 0.4f);
    t.light = rgb(0xffffff, 0.55f);
    t.style = SurfaceStyle::gloss;
    t.radius = 12.0f;
    t.radius_card = 16.0f;
    t.shadow_offset = 3.0f;
    t.shadow_blur = 7.0f;
    t.heading = FontRole::display;
    t.sounds = audio::SoundSet::paper;
    return t;
}

Theme classic()
{
    Theme t =
        web_light("classic", "Classic", "Bevelled desktop",
                  "Grey panels with a light and a dark edge; nothing eases, everything clicks");
    t.backdrop = plain(0x0a7a7a);
    t.page = rgb(0x0a7a7a);
    t.surface = rgb(0xc3c3c3);
    t.surface_high = rgb(0xffffff);
    t.text = rgb(0x000000);
    t.text_muted = rgb(0x3c3c3c);
    t.primary = rgb(0x0a0a84);
    t.secondary = rgb(0xc3c3c3);
    t.on_secondary = rgb(0x000000);
    t.accent = rgb(0x0a0a84);
    t.outline = rgb(0x000000);
    t.focus = rgb(0x000000);
    t.shadow = rgb(0x6f6f6f);
    t.style = SurfaceStyle::bevel;
    t.radius = 0.0f;
    t.radius_card = 0.0f;
    t.border = 3.0f;
    t.label = FontRole::regular;
    t.omega = 80.0f;
    t.sounds = audio::SoundSet::paper;
    t.pill_switches = false;
    t.page_text = rgb(0xffffff);
    t.page_text_muted = rgb(0xffffff, 0.8f);
    t.dark = true;
    return t;
}

Theme blueprint()
{
    Theme t = web_dark("blueprint", "Blueprint", "Wireframe",
                       "Strokes only, one colour, monospaced capitals on a drafting grid");
    t.backdrop = backdrop(BackdropMode::dots, 0x0a2747, 0x0c3157, 0x2f6fa6);
    t.page = rgb(0x0b2b4d);
    t.surface = rgb(0x8fd3ff, 0.06f);
    t.surface_high = rgb(0x8fd3ff, 0.16f);
    t.text = rgb(0xdff1ff);
    t.text_muted = rgb(0x8fd3ff, 0.8f);
    t.primary = rgb(0x8fd3ff);
    t.on_primary = rgb(0x06203a);
    t.secondary = rgb(0x8fd3ff, 0.06f);
    t.on_secondary = rgb(0xdff1ff);
    t.accent = rgb(0xffe27a);
    t.outline = rgb(0x8fd3ff, 0.85f);
    t.focus = rgb(0xffe27a);
    t.style = SurfaceStyle::outline;
    t.radius = 3.0f;
    t.radius_card = 3.0f;
    t.border = 2.0f;
    t.heading = FontRole::mono;
    t.label = FontRole::mono;
    t.caps = true;
    t.tracking = 1.0f;
    t.omega = 22.0f;
    t.sounds = audio::SoundSet::paper;
    t.pill_switches = false;
    return t;
}

Theme hazard()
{
    Theme t = web_dark("hazard", "Hazard", "Sci-fi console",
                       "Cut corners, lit strokes and glow on near-black");
    t.backdrop = backdrop(BackdropMode::dots, 0x07090d, 0x0d1118, 0xffd400);
    t.page = rgb(0x0a0c11);
    t.surface = rgb(0x121722);
    t.surface_high = rgb(0x1e2634);
    t.text = rgb(0xe9f2ff);
    t.text_muted = rgb(0x8ea0b8);
    t.primary = rgb(0xffd400);
    t.on_primary = rgb(0x0a0c11);
    t.secondary = rgb(0x121722);
    t.on_secondary = rgb(0xe9f2ff);
    t.accent = rgb(0x00e5ff);
    t.outline = rgb(0x3d4a60);
    t.focus = rgb(0x00e5ff);
    t.shadow = rgb(0x000000, 0.6f);
    t.style = SurfaceStyle::glow;
    t.corner = Corner::chamfer;
    t.radius = 12.0f;
    t.radius_card = 22.0f;
    t.border = 2.0f;
    t.heading = FontRole::display;
    t.caps = true;
    t.tracking = 3.0f;
    t.omega = 26.0f;
    t.pill_switches = false;
    return t;
}

Theme candy()
{
    Theme t = web_light("candy", "Candy", "Playful pastel",
                        "Fully round, pastel, coloured shadows, everything bounces");
    t.backdrop = backdrop(BackdropMode::bokeh, 0xffe3f0, 0xe6f0ff, 0xffb3d1, 0x9fdcff);
    t.page = rgb(0xfde8f3);
    t.surface_high = rgb(0xffd3e6);
    t.text = rgb(0x5a3a5f);
    t.text_muted = rgb(0x9a7a9d);
    t.primary = rgb(0xff6fae);
    t.on_secondary = rgb(0x5a3a5f);
    t.accent = rgb(0x57c8ff);
    t.outline = rgb(0xffb3d1, 0.0f);
    t.focus = rgb(0x9a6bff);
    t.shadow = rgb(0xff7fb6, 0.42f);
    t.style = SurfaceStyle::soft;
    t.radius = 100.0f;
    t.radius_card = 44.0f;
    t.border = 0.0f;
    t.shadow_offset = 9.0f;
    t.shadow_blur = 18.0f;
    t.heading = FontRole::display;
    t.omega = 15.0f;
    t.damping = 0.42f;
    return t;
}

Theme contrast()
{
    Theme t = web_dark("contrast", "Contrast", "High contrast",
                       "Black, white and one signal colour; thick strokes; nothing subtle");
    t.backdrop = plain(0x000000);
    t.page = rgb(0x000000);
    t.surface = rgb(0x000000);
    t.surface_high = rgb(0x000000);
    t.text_muted = rgb(0xffffff);
    t.primary = rgb(0xffe600);
    t.on_primary = rgb(0x000000);
    t.secondary = rgb(0x000000);
    t.accent = rgb(0x00f0ff);
    t.outline = rgb(0xffffff);
    t.focus = rgb(0xffe600);
    t.style = SurfaceStyle::outline;
    t.radius = 6.0f;
    t.radius_card = 6.0f;
    t.border = 4.0f;
    t.focus_width = 5.0f;
    t.omega = 30.0f;
    t.pill_switches = false;
    return t;
}

// ---- modelled on web frameworks ----------------------------------------

Theme pixel()
{
    Theme t = web_light("pixel", "Pixel", "8-bit pixel art \xC2\xB7 after NES.css",
                        "Notched black outlines, a darker band inside each button, a bitmap font");
    t.surface_high = rgb(0xffffff);
    t.text_muted = rgb(0x212529, 0.75f);
    t.primary = rgb(0x209cee);
    t.accent = rgb(0x92cc41);
    t.outline = rgb(0x212529);
    t.focus = rgb(0xf7d51d);
    t.shadow = rgb(0xadafbc);
    t.style = SurfaceStyle::pixel;
    t.corner = Corner::pixel;
    t.radius = 0.0f;
    t.radius_card = 0.0f;
    t.border = 5.0f;
    t.focus_width = 5.0f;
    t.focus_gap = 3.0f;
    t.heading = FontRole::pixel;
    t.label = FontRole::pixel;
    t.caps = true;
    t.omega = 90.0f;
    t.sounds = audio::SoundSet::paper;
    t.pill_switches = false;
    return t;
}

Theme mantine()
{
    Theme t = web_light("soft", "Soft", "Modern soft \xC2\xB7 after Mantine",
                        "Blue on white, 8 px corners, tinted \"light\" buttons, no shadows");
    t.surface_high = rgb(0xf1f3f5);
    t.text = rgb(0x000000);
    t.text_muted = rgb(0x868e96);
    t.primary = rgb(0x228be6);
    t.secondary = rgb(0xe7f5ff);
    t.on_secondary = rgb(0x1c7ed6);
    t.accent = rgb(0x228be6);
    t.outline = rgb(0xdee2e6);
    t.focus = rgb(0x228be6);
    t.radius = 13.0f;
    t.radius_card = 13.0f;
    t.button_border = 0.0f;
    t.focus_gap = 3.0f;
    t.pill_chips = true;
    return t;
}

Theme daisy()
{
    Theme t =
        web_light("daisy", "Daisy", "Clean and customisable \xC2\xB7 after daisyUI",
                  "Indigo, pink and teal on white; small radii; a hint of depth under buttons");
    t.surface_high = rgb(0xf4f4f5);
    t.text = rgb(0x18181b);
    t.text_muted = rgb(0x71717a);
    t.primary = rgb(0x422ad5);
    t.on_primary = rgb(0xe0e7ff);
    t.secondary = rgb(0xf43098);
    t.on_secondary = rgb(0xfdf2f8);
    t.accent = rgb(0x00d3bb);
    t.outline = rgb(0xe4e4e7);
    t.focus = rgb(0x18181b);
    t.shadow = rgb(0x18181b, 0.22f);
    t.style = SurfaceStyle::soft;
    t.radius = 7.0f;
    t.radius_card = 13.0f;
    t.button_border = 0.0f;
    t.shadow_offset = 3.0f;
    t.shadow_blur = 4.0f;
    t.focus_width = 3.0f;
    t.focus_gap = 3.0f;
    t.pill_chips = true;
    return t;
}

Theme pico()
{
    Theme t = web_light("pico", "Pico", "Minimal and classless \xC2\xB7 after Pico.css",
                        "Large calm type, azure actions, soft layered card shadows");
    t.surface_high = rgb(0xfbfcfc);
    t.text = rgb(0x373c44);
    t.text_muted = rgb(0x646b79);
    t.primary = rgb(0x0172ad);
    t.secondary = rgb(0x525f7a);
    t.on_secondary = rgb(0xffffff);
    t.accent = rgb(0x0172ad);
    t.outline = rgb(0xcfd5e2);
    t.focus = rgb(0x029ae8, 0.42f);
    t.shadow = rgb(0x8191b5, 0.3f);
    t.radius = 8.0f;
    t.radius_card = 8.0f;
    t.button_border = 0.0f;
    t.shadow_offset = 6.0f;
    t.shadow_blur = 22.0f;
    t.focus_width = 5.0f;
    t.focus_gap = 0.0f;
    t.label = FontRole::regular;
    t.omega = 14.0f;
    return t;
}

Theme ant()
{
    Theme t = web_light("ant", "Enterprise", "Structured enterprise \xC2\xB7 after Ant Design",
                        "Dense, orderly, hairline borders, 6 px corners, one confident blue");
    t.backdrop = plain(0xf5f5f5);
    t.page = rgb(0xf5f5f5);
    t.surface_high = rgb(0xf0f0f0);
    t.text = rgb(0x1f1f1f);
    t.text_muted = rgb(0x8c8c8c);
    t.primary = rgb(0x1677ff);
    t.on_secondary = rgb(0x1f1f1f);
    t.accent = rgb(0x1677ff);
    t.outline = rgb(0xd9d9d9);
    t.focus = rgb(0x91caff);
    t.radius = 10.0f;
    t.radius_card = 13.0f;
    t.focus_width = 4.0f;
    t.focus_gap = 1.0f;
    t.label = FontRole::regular;
    t.omega = 20.0f;
    return t;
}

Theme chakra()
{
    Theme t = web_light("chakra", "Chakra", "Modern and accessible \xC2\xB7 after Chakra UI",
                        "Teal on white, grey ghost buttons, soft cards, a blue focus halo");
    t.surface_high = rgb(0xedf2f7);
    t.text = rgb(0x1a202c);
    t.text_muted = rgb(0x718096);
    t.primary = rgb(0x319795);
    t.secondary = rgb(0xedf2f7);
    t.on_secondary = rgb(0x1a202c);
    t.accent = rgb(0x319795);
    t.outline = rgb(0xe2e8f0);
    t.focus = rgb(0x4299e1, 0.6f);
    t.shadow = rgb(0x000000, 0.12f);
    t.radius = 10.0f;
    t.radius_card = 13.0f;
    t.button_border = 0.0f;
    t.shadow_offset = 5.0f;
    t.shadow_blur = 12.0f;
    t.focus_width = 5.0f;
    t.focus_gap = 0.0f;
    return t;
}

Theme nuxt()
{
    Theme t = web_dark("fresh", "Fresh", "Modern flat \xC2\xB7 after Nuxt UI",
                       "Slate night, one vivid green, hairline rings, pill badges");
    t.backdrop =
        backdrop(BackdropMode::gradient, 0x020618, 0x0b1224, 0x00dc82, 0, 0.5f, -0.1f, 0.16f);
    t.page = rgb(0x070d1f);
    t.surface = rgb(0x0f172b);
    t.surface_high = rgb(0x1d293d);
    t.text = rgb(0xe2e8f0);
    t.text_muted = rgb(0x90a1b9);
    t.primary = rgb(0x00dc82);
    t.on_primary = rgb(0x0f172b);
    t.secondary = rgb(0x0f172b);
    t.on_secondary = rgb(0xe2e8f0);
    t.accent = rgb(0x00dc82);
    t.outline = rgb(0x314158);
    t.focus = rgb(0x00dc82);
    t.radius = 10.0f;
    t.radius_card = 13.0f;
    t.focus_gap = 3.0f;
    t.pill_chips = true;
    return t;
}

Theme shoelace()
{
    Theme t = web_light("neutral", "Neutral", "Neutral modern \xC2\xB7 after Shoelace",
                        "Zinc greys, sky blue, 4 px corners, a wide translucent focus ring");
    t.surface_high = rgb(0xe4e4e7);
    t.text = rgb(0x3f3f46);
    t.text_muted = rgb(0x71717a);
    t.primary = rgb(0x0284c7);
    t.on_secondary = rgb(0x3f3f46);
    t.accent = rgb(0x0284c7);
    t.outline = rgb(0xd4d4d8);
    t.focus = rgb(0x0ea5e9, 0.42f);
    t.shadow = rgb(0x71717a, 0.14f);
    t.radius = 7.0f;
    t.radius_card = 7.0f;
    t.shadow_offset = 3.0f;
    t.shadow_blur = 7.0f;
    t.focus_width = 5.0f;
    t.focus_gap = 1.0f;
    return t;
}

Theme propeller()
{
    Theme t =
        web_light("paper", "Material", "Material Design \xC2\xB7 after Propeller",
                  "Raised sheets with real shadows, capitals, underlined fields, a pink accent");
    t.backdrop = plain(0xeeeeee);
    t.page = rgb(0xeeeeee);
    t.surface_high = rgb(0xdcdcdc);
    t.text = rgb(0x212121);
    t.text_muted = rgb(0x757575);
    t.primary = rgb(0x4285f4);
    t.on_secondary = rgb(0x212121);
    t.accent = rgb(0xff4081);
    t.outline = rgb(0x000000, 0.0f);
    t.focus = rgb(0x4285f4, 0.5f);
    t.shadow = rgb(0x000000, 0.3f);
    t.style = SurfaceStyle::soft;
    t.radius = 3.0f;
    t.radius_card = 4.0f;
    t.border = 0.0f;
    t.shadow_offset = 4.0f;
    t.shadow_blur = 9.0f;
    t.focus_width = 5.0f;
    t.focus_gap = 0.0f;
    t.underline_fields = true;
    t.caps = true;
    t.tracking = 1.0f;
    return t;
}

Theme semantic()
{
    Theme t = web_light("humane", "Humane", "Clean and readable \xC2\xB7 after Semantic UI",
                        "Soft grey buttons with bold labels, white segments, 4 px corners");
    t.backdrop = plain(0xf7f8f9);
    t.page = rgb(0xf7f8f9);
    t.surface_high = rgb(0xe9eaeb);
    t.text = rgb(0x212121);
    t.text_muted = rgb(0x6b6b6b);
    t.primary = rgb(0x2185d0);
    t.secondary = rgb(0xe0e1e2);
    t.on_secondary = rgb(0x5a5a5a);
    t.accent = rgb(0x2185d0);
    t.outline = rgb(0xdededf);
    t.focus = rgb(0x85b7d9);
    t.shadow = rgb(0x222426, 0.16f);
    t.radius = 7.0f;
    t.radius_card = 7.0f;
    t.button_border = 0.0f;
    t.shadow_offset = 2.0f;
    t.shadow_blur = 4.0f;
    t.focus_gap = 1.0f;
    return t;
}

Theme bootstrap()
{
    Theme t =
        web_light("standard", "Standard", "General purpose \xC2\xB7 after Bootstrap 5",
                  "The familiar default: medium corners, blue and grey buttons, a wide focus halo");
    t.surface_high = rgb(0xe9ecef);
    t.primary = rgb(0x0d6efd);
    t.secondary = rgb(0x6c757d);
    t.on_secondary = rgb(0xffffff);
    t.accent = rgb(0x0d6efd);
    t.outline = rgb(0xdee2e6);
    t.focus = rgb(0x0d6efd, 0.28f);
    t.radius = 10.0f;
    t.radius_card = 10.0f;
    t.button_border = 0.0f;
    t.focus_width = 6.0f;
    t.focus_gap = 0.0f;
    t.label = FontRole::regular;
    return t;
}

Theme preline()
{
    Theme t = web_dark("pill", "Pill", "High-contrast dashboard \xC2\xB7 after Preline UI",
                       "Near-black, white and blue; every control is a smooth pill");
    t.backdrop = plain(0x0a0a0a);
    t.page = rgb(0x0a0a0a);
    t.surface = rgb(0x171717);
    t.surface_high = rgb(0x262626);
    t.text_muted = rgb(0xa3a3a3);
    t.primary = rgb(0x3b82f6);
    t.secondary = rgb(0xffffff);
    t.on_secondary = rgb(0x171717);
    t.accent = rgb(0x3b82f6);
    t.outline = rgb(0x404040);
    t.focus = rgb(0x3b82f6, 0.6f);
    t.radius = 100.0f;
    t.radius_card = 19.0f;
    t.button_border = 0.0f;
    t.focus_width = 4.0f;
    t.focus_gap = 3.0f;
    t.pill_chips = true;
    return t;
}

Theme flowbite()
{
    Theme t = web_dark("admin", "Admin", "Dark admin \xC2\xB7 after Flowbite",
                       "Blue-grey night panels, rounded 8 px controls, a four pixel focus ring");
    t.primary = rgb(0x2563eb);
    t.secondary = rgb(0x1f2937);
    t.on_secondary = rgb(0xd1d5db);
    t.accent = rgb(0x2563eb);
    t.outline = rgb(0x4b5563);
    t.focus = rgb(0x1e40af);
    t.shadow = rgb(0x000000, 0.3f);
    t.radius = 13.0f;
    t.radius_card = 13.0f;
    t.shadow_offset = 2.0f;
    t.shadow_blur = 5.0f;
    t.focus_width = 6.0f;
    t.focus_gap = 0.0f;
    return t;
}

Theme bulma()
{
    Theme t = web_light("friendly", "Friendly", "Friendly and soft \xC2\xB7 after Bulma",
                        "Turquoise, generous round boxes floating on a long soft shadow");
    t.surface_high = rgb(0xf5f5f5);
    t.text = rgb(0x363636);
    t.text_muted = rgb(0x7a7a7a);
    t.primary = rgb(0x00d1b2);
    t.on_secondary = rgb(0x363636);
    t.accent = rgb(0x485fc7);
    t.outline = rgb(0xdbdbdb);
    t.focus = rgb(0x485fc7, 0.3f);
    t.shadow = rgb(0x0a0a0a, 0.12f);
    t.radius = 10.0f;
    t.radius_card = 19.0f;
    t.shadow_offset = 12.0f;
    t.shadow_blur = 22.0f;
    t.focus_width = 5.0f;
    t.focus_gap = 0.0f;
    t.label = FontRole::regular;
    t.pill_chips = true;
    return t;
}

Theme uikit()
{
    Theme t = web_light("crisp", "Crisp", "Minimal and square \xC2\xB7 after UIkit",
                        "Square edges, small capitals, grey text, one bright blue");
    t.surface_high = rgb(0xf8f8f8);
    t.text = rgb(0x333333);
    t.text_muted = rgb(0x999999);
    t.primary = rgb(0x1e87f0);
    t.on_secondary = rgb(0x333333);
    t.accent = rgb(0x1e87f0);
    t.outline = rgb(0xe5e5e5);
    t.focus = rgb(0x1e87f0);
    t.shadow = rgb(0x000000, 0.1f);
    t.radius = 0.0f;
    t.radius_card = 0.0f;
    t.shadow_offset = 8.0f;
    t.shadow_blur = 20.0f;
    t.focus_width = 2.5f;
    t.focus_gap = 3.0f;
    t.heading = FontRole::display;
    t.label = FontRole::regular;
    t.caps = true;
    t.tracking = 1.0f;
    t.pill_switches = false;
    return t;
}

Theme materialize()
{
    Theme t =
        web_light("layers", "Layers", "Material Design \xC2\xB7 after Materialize",
                  "Teal and coral, flat layers stacked by elevation, capitals, underlined fields");
    t.backdrop = plain(0xfafafa);
    t.page = rgb(0xfafafa);
    t.surface_high = rgb(0xe0e0e0);
    t.text = rgb(0x212121);
    t.text_muted = rgb(0x9e9e9e);
    t.primary = rgb(0x26a69a);
    t.secondary = rgb(0xee6e73);
    t.on_secondary = rgb(0xffffff);
    t.accent = rgb(0x26a69a);
    t.outline = rgb(0x000000, 0.0f);
    t.focus = rgb(0x26a69a, 0.45f);
    t.shadow = rgb(0x000000, 0.34f);
    t.style = SurfaceStyle::soft;
    t.radius = 3.0f;
    t.radius_card = 3.0f;
    t.border = 0.0f;
    t.shadow_offset = 3.0f;
    t.shadow_blur = 6.0f;
    t.focus_width = 5.0f;
    t.focus_gap = 0.0f;
    t.underline_fields = true;
    t.label = FontRole::regular;
    t.caps = true;
    t.tracking = 1.0f;
    return t;
}

Theme foundation()
{
    Theme t = web_light("utility", "Utility", "Utilitarian \xC2\xB7 after Foundation",
                        "Sharp square blocks, plain borders, no decoration at all");
    t.backdrop = plain(0xfefefe);
    t.page = rgb(0xfefefe);
    t.surface_high = rgb(0xe6e6e6);
    t.text = rgb(0x0a0a0a);
    t.text_muted = rgb(0x8a8a8a);
    t.primary = rgb(0x1779ba);
    t.on_primary = rgb(0xfefefe);
    t.secondary = rgb(0x767676);
    t.on_secondary = rgb(0xfefefe);
    t.accent = rgb(0x3adb76);
    t.outline = rgb(0xcacaca);
    t.focus = rgb(0x0a0a0a);
    t.radius = 0.0f;
    t.radius_card = 0.0f;
    t.button_border = 0.0f;
    t.focus_width = 2.5f;
    t.focus_gap = 3.0f;
    t.label = FontRole::regular;
    t.omega = 26.0f;
    t.pill_switches = false;
    return t;
}

Theme papercss()
{
    Theme t = web_light("sketch", "Sketch", "Hand-drawn paper \xC2\xB7 after PaperCSS",
                        "Crooked pen lines, tinted fills and soft shadows: a UI on a notepad");
    t.backdrop = backdrop(BackdropMode::paper, 0xfffdf7, 0xf6f1e4, 0xffffff);
    t.page = rgb(0xfbf8ef);
    t.surface = rgb(0xffffff);
    t.surface_high = rgb(0xfffdf7);
    t.text = rgb(0x41403e);
    t.text_muted = rgb(0x8a8782);
    t.primary = rgb(0xdeefff);
    t.on_primary = rgb(0x0071de);
    t.on_secondary = rgb(0x41403e);
    t.accent = rgb(0x86a361);
    t.outline = rgb(0x41403e);
    t.focus = rgb(0x0071de);
    t.shadow = rgb(0x000000, 0.2f);
    t.style = SurfaceStyle::sketch;
    t.radius = 4.0f;
    t.radius_card = 4.0f;
    t.border = 3.0f;
    t.shadow_offset = 12.0f;
    t.shadow_blur = 16.0f;
    t.heading = FontRole::hand;
    t.label = FontRole::hand;
    t.omega = 14.0f;
    t.damping = 0.55f;
    t.sounds = audio::SoundSet::paper;
    t.pill_switches = false;
    return t;
}

Theme milligram()
{
    Theme t =
        web_light("light", "Featherweight", "Ultra-light \xC2\xB7 after Milligram",
                  "Almost nothing: thin grey rules and small purple capitals with wide tracking");
    t.surface_high = rgb(0xf4f5f6);
    t.text = rgb(0x606c76);
    t.text_muted = rgb(0x9aa5ae);
    t.primary = rgb(0x9b4dca);
    t.secondary = rgb(0xffffff, 0.0f);
    t.on_secondary = rgb(0x9b4dca);
    t.accent = rgb(0x9b4dca);
    t.outline = rgb(0xd1d1d1);
    t.focus = rgb(0x9b4dca);
    t.radius = 7.0f;
    t.radius_card = 7.0f;
    t.focus_width = 2.5f;
    t.focus_gap = 3.0f;
    t.heading = FontRole::regular;
    t.caps = true;
    t.tracking = 3.0f;
    return t;
}

Theme primer()
{
    Theme t = web_dark("code", "Code", "Developer tools \xC2\xB7 after Primer",
                       "Ink-blue night, hairline borders, a green call to action, blue focus");
    t.backdrop = plain(0x0d1117);
    t.page = rgb(0x0d1117);
    t.surface = rgb(0x161b22);
    t.surface_high = rgb(0x21262d);
    t.text = rgb(0xe6edf3);
    t.text_muted = rgb(0x8b949e);
    t.primary = rgb(0x238636);
    t.secondary = rgb(0x21262d);
    t.on_secondary = rgb(0xc9d1d9);
    t.accent = rgb(0x1f6feb);
    t.outline = rgb(0x30363d);
    t.focus = rgb(0x1f6feb);
    t.radius = 10.0f;
    t.radius_card = 10.0f;
    t.focus_gap = 0.0f;
    t.omega = 22.0f;
    return t;
}

} // namespace

std::span<const Theme> themes()
{
    static const std::array<Theme, 30> kThemes = {
        // Design languages.
        acrylic(),
        brutal(),
        clay(),
        tiles(),
        gloss(),
        classic(),
        blueprint(),
        hazard(),
        candy(),
        contrast(),
        // Modelled on web frameworks.
        pixel(),
        mantine(),
        daisy(),
        pico(),
        ant(),
        chakra(),
        nuxt(),
        shoelace(),
        propeller(),
        semantic(),
        bootstrap(),
        preline(),
        flowbite(),
        bulma(),
        uikit(),
        materialize(),
        foundation(),
        papercss(),
        milligram(),
        primer(),
    };
    return kThemes;
}

} // namespace hui::ui
