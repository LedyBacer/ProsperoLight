#!/usr/bin/env python3
# ps5-native-app-boilerplate - ProsperoLight controller button icons.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
#
"""Draw the controller button icons used by the README and the launcher.

docs/images/buttons/*.svg are the icons the README shows in place of button
names. Each sits on its own dark plate, so it reads on a light or a dark page,
and is 20 pixels tall so it fits in a line of text.

ui/icons/touchpad.tga, l1.tga and r1.tga are launcher icons in the same form
as the existing ones: 40 x 40, uncompressed, straight alpha, top origin. They
need Pillow and are drawn from ui/fonts/Inter-SemiBold.ttf.

usage: generate-button-icons.py [--check]

--check compares the committed SVG files with what this script draws and
writes nothing.
"""

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
SVG_DIRECTORY = ROOT / "docs/images/buttons"
TGA_DIRECTORY = ROOT / "ui/icons"

PLATE = "#101c36"
PLATE_EDGE = "#2f4470"
NEUTRAL = "#F2F7F8"
# The launcher's palette for the four shapes (ui/icons/README.md).
CROSS = "#70E1DC"
CIRCLE = "#FF8793"
SQUARE = "#D18BE5"
TRIANGLE = "#69D997"

FONT = "Inter, 'Segoe UI', 'Helvetica Neue', Arial, sans-serif"


def svg(width, body, label):
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width // 2}" height="20" '
        f'viewBox="0 0 {width} 40" role="img" aria-label="{label}">\n'
        f"  <title>{label}</title>\n{body}</svg>\n"
    )


def disc():
    return f'  <circle cx="20" cy="20" r="18.25" fill="{PLATE}" stroke="{PLATE_EDGE}" stroke-width="1.5"/>\n'


def cap(width):
    return (
        f'  <rect x="2.75" y="6.75" width="{width - 5.5}" height="26.5" rx="9" '
        f'fill="{PLATE}" stroke="{PLATE_EDGE}" stroke-width="1.5"/>\n'
    )


def stroke(colour, width):
    return (
        f'fill="none" stroke="{colour}" stroke-width="{width}" '
        'stroke-linecap="round" stroke-linejoin="round"'
    )


def shoulder(label):
    return svg(
        56,
        cap(56)
        + f'  <text x="28" y="25.4" text-anchor="middle" font-family="{FONT}" font-size="15" '
        f'font-weight="700" fill="{NEUTRAL}">{label}</text>\n',
        label,
    )


def svg_icons():
    return {
        "cross": svg(
            40, disc() + f'  <path d="M13.5 13.5 26.5 26.5M26.5 13.5 13.5 26.5" {stroke(CROSS, 3.2)}/>\n',
            "Cross"),
        "circle": svg(40, disc() + f'  <circle cx="20" cy="20" r="7.6" {stroke(CIRCLE, 3)}/>\n', "Circle"),
        "square": svg(
            40, disc() + f'  <rect x="13" y="13" width="14" height="14" rx="1" {stroke(SQUARE, 3)}/>\n',
            "Square"),
        "triangle": svg(
            40, disc() + f'  <path d="M20 11.6 28.4 26.2H11.6Z" {stroke(TRIANGLE, 3)}/>\n', "Triangle"),
        "options": svg(
            40, disc() + f'  <path d="M13.5 14.5h13M13.5 20h13M13.5 25.5h13" {stroke(NEUTRAL, 2.6)}/>\n',
            "Options"),
        "dpad": svg(
            40,
            disc()
            + f'  <path d="M16.5 9.5h7v7h7v7h-7v7h-7v-7h-7v-7h7z" {stroke(NEUTRAL, 2.4)}/>\n',
            "D-pad"),
        "stick": svg(
            40,
            disc()
            + f'  <circle cx="20" cy="20" r="9.4" {stroke(NEUTRAL, 2.4)}/>\n'
            + f'  <circle cx="20" cy="20" r="4" fill="{NEUTRAL}"/>\n',
            "Analog stick"),
        "l1": shoulder("L1"),
        "r1": shoulder("R1"),
        "touchpad": svg(
            56,
            cap(56)
            + f'  <rect x="11" y="12.5" width="34" height="15" rx="5" {stroke(NEUTRAL, 2.4)}/>\n'
            + f'  <path d="M28 12.5v15" {stroke(NEUTRAL, 2)}/>\n',
            "Touchpad"),
    }


def write_tga_icons():
    from PIL import Image, ImageDraw, ImageFont

    scale = 8
    size = 40 * scale
    colour = tuple(int(NEUTRAL[i:i + 2], 16) for i in (1, 3, 5))
    font = ImageFont.truetype(str(ROOT / "ui/fonts/Inter-SemiBold.ttf"), 15 * scale)

    def plate(draw):
        draw.rounded_rectangle(
            [1.5 * scale, 8.5 * scale, 38.5 * scale, 31.5 * scale],
            radius=8 * scale, outline=255, width=int(2.5 * scale))

    def touchpad(draw):
        plate(draw)
        draw.line([20 * scale, 10 * scale, 20 * scale, 30 * scale], fill=255, width=2 * scale)

    def shoulder_key(label):
        def draw_key(draw):
            plate(draw)
            draw.text((20 * scale, 20 * scale), label, font=font, fill=255, anchor="mm")
        return draw_key

    for name, paint in (("touchpad", touchpad), ("l1", shoulder_key("L1")), ("r1", shoulder_key("R1"))):
        # One colour and a coverage mask keep the alpha straight after scaling down.
        mask = Image.new("L", (size, size), 0)
        paint(ImageDraw.Draw(mask))
        mask = mask.resize((40, 40), Image.LANCZOS)
        # The same bytes as the other launcher icons: an 18-byte header, then
        # BGRA rows from the top, and no TGA 2.0 footer.
        header = bytes([0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 40, 0, 40, 0, 32, 0x28])
        blue_green_red = bytes(reversed(colour))
        pixels = b"".join(blue_green_red + bytes([alpha]) for alpha in mask.getdata())
        path = TGA_DIRECTORY / f"{name}.tga"
        path.write_bytes(header + pixels)
        print(f"wrote {path.relative_to(ROOT)}")


def main(arguments):
    check = "--check" in arguments
    stale = []
    for name, image in sorted(svg_icons().items()):
        path = SVG_DIRECTORY / f"{name}.svg"
        if check:
            if not path.is_file() or path.read_text(encoding="utf-8") != image:
                stale.append(path)
            continue
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(image, encoding="utf-8", newline="\n")
        print(f"wrote {path.relative_to(ROOT)}")
    if check:
        for path in stale:
            print(f"{path} is out of date; run tools/generate-button-icons.py", file=sys.stderr)
        return 1 if stale else 0
    write_tga_icons()
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
