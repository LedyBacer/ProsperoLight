// ps5-homebrew-ui - Component: Accordion, sections that open and close under their headers.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui/components/card.hpp"
#include "ui/components/component.hpp"

#include <functional>
#include <string>
#include <vector>

namespace hui::ui
{

struct AccordionSection
{
    std::string title;
    std::string value{};   // quiet text at the end of the header ("3 items", "On")
    std::string body{};    // the built-in content: wrapped text
    float height = 0.0f;   // content height in pixels; 0 fits the body text. A section
                           // with no body gets its content from the `content` slot
    bool disabled = false; // focusable, but refuses to open
    int tag = 0;           // yours
};

enum class AccordionChevron : std::uint8_t
{
    trailing, // at the end of the header: points down, up when open
    leading,  // before the title: points right, down when open
    none,
};

struct AccordionStyle : ComponentStyle
{
    // ---- geometry ----
    float header_height = 68.0f;
    float gap = 10.0f;           // between sections
    float padding = 24.0f;       // inside a header, left and right
    float body_padding = 18.0f;  // above and below the content
    float panel_padding = 14.0f; // between the panel and the sections (panel = true)
    // ---- type ----
    float title_size = 26.0f;
    float value_size = 21.0f;
    float body_size = 22.0f;
    float body_line = 1.42f; // line height of the body, in text sizes
    int body_lines = 8;      // the body is cut after this many lines
    // ---- look ----
    HighlightStyle highlight;
    AccordionChevron chevron = AccordionChevron::trailing;
    bool cards = true;        // every section is a themed surface of its own
    bool dividers = false;    // hairlines between sections (without cards)
    bool panel = false;       // a themed panel behind the whole accordion
    bool scroll_thumb = true; // shown only when the sections overflow
    // ---- behaviour ----
    bool single = true;          // opening a section closes the others
    bool wrap = false;           // past the last header comes the first
    EdgeExits exits;             // edges that hand the focus back instead of refusing
    float edge_fade = 0.8f;      // sections fade over this share of a header at the clip edges
    float entrance_step = 0.04f; // seconds between sections arriving; 0 for none
    bool pitch_by_position = true;
};

// A vertical list of sections. Each has a header row; confirm (or right and
// left) opens and closes its content with a height spring while the chevron
// turns. One highlight glides between the headers and rides along when the
// sections above it change height. Taller than its bounds, it scrolls.
//
//   ui::Accordion faq;
//   faq.style.theme = theme;
//   faq.style.single = false;                     // several may be open
//   faq.set_sections({{"Saving", "", "The game saves at every lantern."},
//                     {"Controls", "", "Remap any button in Options."}});
//   faq.set_bounds({96, 240, 720, 600});
//   faq.measure(context.fonts);                   // fits the text bodies
//   ...
//   if (faq.handle(input, feedback) == ui::Event::changed) on_toggle(faq.focus());
//   faq.update(dt);
//   faq.draw(canvas);
class Accordion
{
  public:
    // area is where the content goes at its full height (it is revealed by a
    // clip while the section opens); opacity is 0..1.
    using Content = std::function<void(Canvas &canvas, const gfx::Rect &area,
                                       const AccordionSection &section, int index, float opacity)>;

    AccordionStyle style;
    Content content; // draws the content of the sections that have no body text

    void set_sections(std::vector<AccordionSection> sections);
    const std::vector<AccordionSection> &sections() const
    {
        return sections_;
    }
    AccordionSection &section(int index)
    {
        return sections_[static_cast<std::size_t>(index)];
    }
    void set_bounds(const gfx::Rect &bounds);
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    // Measures the text bodies in the theme's face for the current bounds.
    // Call it after set_sections(), set_bounds() or a change of theme; until
    // then the height of a body is estimated from its length.
    void measure(const Fonts &fonts);

    int focus() const
    {
        return focus_;
    }
    // Moves the focus without sound; snap skips the glide.
    void set_focus(int index, bool snap = true);
    bool is_open(int index) const;
    // Opens or closes a section without sound; snap skips the animation.
    void set_open(int index, bool open, bool snap = false);
    // An inactive accordion keeps a faint highlight.
    void set_active(bool active)
    {
        active_ = active;
    }
    // Replays the entrance animation.
    void enter();
    // The edge the last handle() left through, or Direction::none.
    Direction exit() const
    {
        return exit_;
    }

    Event handle(const InputFrame &input, Feedback &feedback);
    void update(float dt);
    void draw(Canvas &canvas) const;

    // Where a header is on screen right now (scroll and animation applied).
    gfx::Rect header_rect(int index) const;
    // Where its content is: the full-height rectangle the slot draws into.
    gfx::Rect content_rect(int index) const;

  private:
    gfx::Rect inner() const;
    float content_height(int index) const;
    float amount(int index) const;
    float top(int index, bool settled) const;
    float total(bool settled) const;
    Event toggle(const InputFrame &input, Feedback &feedback);
    void retarget(bool snap);
    void place_highlight();

    std::vector<AccordionSection> sections_;
    std::vector<bool> open_;
    std::vector<tween::Bounce> amounts_; // how open each section is, 0..1
    std::vector<float> measured_;        // content heights from measure(); < 0: estimate
    gfx::Rect bounds_{0.0f, 0.0f, 600.0f, 400.0f};
    int focus_ = 0;
    bool active_ = true;
    Direction exit_ = Direction::none;
    float age_ = 10.0f;
    // The highlight is placed on the focused header every frame, plus this
    // offset that springs to zero: it glides between headers yet never lags
    // behind a header that moves because a section above it changes height.
    tween::Bounce glide_;
    Highlight highlight_;
    Scroller scroll_;
    tween::Spring active_amount_{1.0f, 0.0f, 1.0f};
    Pulse press_;
};

} // namespace hui::ui
