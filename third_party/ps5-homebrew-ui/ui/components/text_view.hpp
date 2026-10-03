// ps5-homebrew-ui - Component: TextView, a scrolling article for licences, notes and help.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ui/components/card.hpp"
#include "ui/components/component.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace hui::ui
{

enum class TextBlockKind : std::uint8_t
{
    heading,   // level 1..3
    paragraph, // running text
    bullet,    // one item of an unordered list
    numbered,  // one item of an ordered list; the number counts by itself
    quote,     // set in from a bar, in the quiet colour
    code,      // monospaced lines on a sunken plate; '\n' breaks lines
    divider,   // a hairline
    key_value, // "Version ........ 1.4.0"
    image,     // room for a picture, drawn by the `image` slot
};

// One block of an article. Build them with the helpers below.
struct TextBlock
{
    TextBlockKind kind = TextBlockKind::paragraph;
    std::string text;    // the words; key_value: the key; image: the caption
    std::string value;   // key_value: the value; numbered: replaces the counted marker
    int level = 1;       // heading: 1 (largest) to 3
    float height = 0.0f; // image: the picture's height; 0 uses style.image_height
    int tag = 0;         // yours

    static TextBlock heading(std::string text, int level = 1);
    static TextBlock paragraph(std::string text);
    static TextBlock bullet(std::string text);
    static TextBlock numbered(std::string text, std::string marker = {});
    static TextBlock quote(std::string text);
    static TextBlock code(std::string text);
    static TextBlock divider();
    static TextBlock key_value(std::string key, std::string value);
    static TextBlock image(std::string caption, float height = 0.0f, int tag = 0);
};

struct TextViewStyle : ComponentStyle
{
    // ---- geometry ----
    float padding = 28.0f;       // between the bounds and the text
    float max_width = 0.0f;      // the widest a line may be; 0 fills the view
    float block_gap = 20.0f;     // between blocks
    float item_gap = 10.0f;      // between neighbouring list items and key-value lines
    float heading_gap = 34.0f;   // above a level 1 or 2 heading
    float indent = 36.0f;        // list items and quotes
    float image_height = 170.0f; // an image block with no height of its own
    float footer_height = 44.0f; // the section name and the reading progress
    float toc_width = 220.0f;    // the contents column
    float toc_row = 44.0f;       // one of its rows
    float toc_gap = 26.0f;       // between it and the text
    // ---- type ----
    float body_size = 24.0f;
    float line_height = 1.45f; // of running text, in body sizes
    float heading_size = 36.0f;
    float subheading_size = 28.0f; // level 2
    float minor_size = 20.0f;      // level 3: the theme's label face, in the quiet colour
    float code_size = 21.0f;
    float caption_size = 20.0f; // image captions, the footer and the contents
    // ---- look ----
    bool panel = true;        // a themed panel behind the article
    bool focus_ring = true;   // the theme's ring around it while it has the focus
    bool scroll_thumb = true; // shown only when the text overflows
    bool footer = true;       // section name on the left, "42%" on the right
    bool toc = false;         // a contents column built from the headings
    int toc_levels = 2;       // headings down to this level are listed (and jumped to)
    bool heading_rule = true; // a short rule under level 1 headings
    float edge_fade = 72.0f;  // the text thins out over this many pixels above the lower
                              // edge while more follows; 0 turns the edge fades off
    // ---- behaviour ----
    float step = 132.0f;        // pixels one press of up or down scrolls
    float page_share = 0.86f;   // of the view, per page up or down
    float stick_speed = 900.0f; // pixels per second at full deflection of the right stick
    float stick_boost = 2.6f;   // ... times this once the stick has been held
    float stick_ramp = 1.4f;    // seconds of holding to reach the boost
    bool pitch_by_position = true;
    EdgeExits exits; // up / down: at the top or the end, hand the focus on instead of refusing
};

// A scrolling article. The words are wrapped once, when the content, the
// width or the theme changes; a frame only draws the lines in view.
//
//   ui::TextView notes;
//   notes.style.theme = theme;
//   notes.set_content({ui::TextBlock::heading("Update 1.4"),
//                      ui::TextBlock::paragraph("What changed in this release."),
//                      ui::TextBlock::bullet("Faster loading")});
//   notes.set_bounds({96, 240, 720, 640});
//   ...
//   notes.handle(input, feedback);   // up / down and the right stick scroll
//   notes.update(dt);
//   notes.draw(canvas);
//
// Left and right are never the article's: they return Event::none untouched.
class TextView
{
  public:
    // box is where the picture goes, on screen.
    using ImageSlot = std::function<void(Canvas &canvas, const gfx::Rect &box,
                                         const TextBlock &block, int index)>;

    TextViewStyle style;
    ImageSlot image; // draws an image block; without it a placeholder is drawn

    void set_content(std::vector<TextBlock> blocks);
    const std::vector<TextBlock> &content() const
    {
        return blocks_;
    }
    void set_bounds(const gfx::Rect &bounds);
    const gfx::Rect &bounds() const
    {
        return bounds_;
    }
    // "Has the screen's focus": the ring, a brighter thumb.
    void set_active(bool active)
    {
        active_ = active;
    }

    // Wraps the text now. draw() does it by itself; call this when you need
    // heights or progress before the first draw (the fonts must outlive the
    // component, as a Context's do).
    void layout(const Fonts &fonts);

    Event handle(const InputFrame &input, Feedback &feedback);
    void update(float dt);
    void draw(Canvas &canvas) const;

    // ---- for a screen to bind to buttons of its choice ----
    // direction: 1 down, -1 up. Both return moved, or refused at an end.
    Event page(int direction, Feedback &feedback);
    Event next_heading(int direction, Feedback &feedback);
    // Without sound. snap skips the glide.
    void scroll_to(float offset, bool snap = false);
    void scroll_to_block(int index, bool snap = false);

    // 0 at the top, 1 at the end (1 when everything fits).
    float progress() const;
    float scroll() const
    {
        return scroll_.target;
    }
    float content_height() const
    {
        return height_;
    }
    // How far the article can scroll.
    float scroll_limit() const;
    // Where a block starts in the article: scroll_to(block_offset(i)) puts it
    // at the top of the view.
    float block_offset(int index) const;
    // The blocks the contents column lists, in order.
    const std::vector<int> &headings() const
    {
        return headings_;
    }
    // The heading whose section is at the top of the view: a block index, -1
    // before the first one.
    int current_heading() const;
    // The edge the focus asked to leave through in the last handle() (see
    // TextViewStyle::exits), or Direction::none. handle() returned Event::none.
    Direction exit() const
    {
        return exit_;
    }

  private:
    enum class Face : std::uint8_t
    {
        body,
        heading,
        label,
        mono,
    };
    enum class Ink : std::uint8_t
    {
        text,
        muted,
        code,
    };
    struct Line
    {
        std::string text;
        float x = 0.0f;        // from the left of the text column
        float baseline = 0.0f; // from the top of the article
        float size = 0.0f;
        Face face = Face::body;
        Ink ink = Ink::text;
        gfx::Align align = gfx::Align::left;
    };
    struct Placed
    {
        float top = 0.0f;
        float height = 0.0f;
        std::size_t first_line = 0;
        std::size_t line_count = 0;
        int number = 0; // numbered: the counted marker
    };
    struct Key
    {
        const Fonts *fonts = nullptr;
        const char *theme = nullptr;
        float values[20] = {};
        bool operator==(const Key &other) const;
    };

    Key key() const;
    void ensure_layout() const;
    void build(const Fonts &fonts) const;
    gfx::Rect inner() const;  // inside the padding
    gfx::Rect view() const;   // where the text scrolls
    gfx::Rect column() const; // the text column inside the view
    gfx::Rect toc_rect() const;
    float toc_scroll() const;
    Event scroll_by(float delta, const InputFrame *input, Feedback &feedback, audio::Cue cue,
                    Direction direction);
    void sync_scroll();
    void remember_place();
    void draw_toc(Canvas &canvas, Painter &paint) const;
    void draw_footer(Canvas &canvas, Painter &paint) const;

    std::vector<TextBlock> blocks_;
    gfx::Rect bounds_{0.0f, 0.0f, 640.0f, 480.0f};
    bool active_ = true;
    Direction exit_ = Direction::none;

    // The wrapped article. It is rebuilt inside draw() when its key went
    // stale, hence mutable: drawing stays const for the caller.
    mutable std::vector<Line> lines_;
    mutable std::vector<Placed> placed_;
    mutable std::vector<int> headings_;
    mutable float height_ = 0.0f;
    mutable Key key_;
    mutable const Fonts *fonts_ = nullptr;
    mutable gfx::DrawList scratch_; // a Painter needs a list, even to measure

    tween::Spring scroll_;
    // The reader's place, kept through a re-wrap: the block at the top of the
    // view and how far into it the view starts.
    int anchor_block_ = 0;
    float anchor_share_ = 0.0f;
    mutable std::uint32_t serial_ = 0; // counts layouts
    std::uint32_t seen_serial_ = 0;    // the layout the scroll position belongs to
    tween::Spring active_amount_{1.0f, 0.0f, 1.0f};
    Highlight toc_mark_;
    bool toc_mark_set_ = false;
    Pulse bump_; // the nudge of a refusal at either end
    float bump_sign_ = 1.0f;
    float analog_ = 0.0f; // the right stick this frame, consumed by update()
    float hold_ = 0.0f;   // how long it has been held
};

} // namespace hui::ui
