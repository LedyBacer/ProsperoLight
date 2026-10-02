#!/usr/bin/env python3
# ps5-native-app-boilerplate - ProsperoLight loading label generator.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
#
"""Generate compact loading labels from the launcher's Montserrat bitmap fonts."""

from pathlib import Path
import struct
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[1]


def render_label(font_size: int, text: str, output_name: str) -> tuple[int, int]:
    font_root = ROOT / "third_party/fonts/lvgl-bitmap"
    definition = ET.parse(font_root / f"Montserrat-{font_size}.fnt").getroot()
    common = definition.find("common")
    assert common is not None
    atlas_width = int(common.attrib["scaleW"])
    atlas_height = int(common.attrib["scaleH"])
    line_height = int(common.attrib["lineHeight"])
    glyphs = {
        int(item.attrib["id"]): {
            key: int(value) for key, value in item.attrib.items() if key != "page"
        }
        for item in definition.findall("./chars/char")
    }
    kernings = {
        (int(item.attrib["first"]), int(item.attrib["second"])): int(
            item.attrib["amount"]
        )
        for item in definition.findall("./kernings/kerning")
    }

    data = (font_root / f"Montserrat-{font_size}.tga").read_bytes()
    if data[2] != 2 or data[16] != 32 or not (data[17] & 0x20):
        raise SystemExit("expected an uncompressed, top-origin 32-bit TGA")
    image_width, image_height = struct.unpack_from("<HH", data, 12)
    if (image_width, image_height) != (atlas_width, atlas_height):
        raise SystemExit("bitmap font atlas dimensions do not match its definition")
    pixels = memoryview(data)[18 + data[0] :]

    pen = 0
    previous = None
    positions = []
    for character in map(ord, text):
        glyph = glyphs[character]
        if previous is not None:
            pen += kernings.get((previous, character), 0)
        positions.append((pen, glyph))
        pen += glyph["xadvance"]
        previous = character

    width = pen
    output = bytearray(width * line_height)
    for pen_x, glyph in positions:
        for y in range(glyph["height"]):
            source_y = glyph["y"] + y
            destination_y = glyph["yoffset"] + y
            for x in range(glyph["width"]):
                source_x = glyph["x"] + x
                destination_x = pen_x + glyph["xoffset"] + x
                source = (source_y * atlas_width + source_x) * 4
                output[destination_y * width + destination_x] = pixels[source + 3]

    (ROOT / "assets/private" / output_name).write_bytes(output)
    print(f"{output_name}: {width}x{line_height}")
    return width, line_height


if __name__ == "__main__":
    render_label(32, "ProsperoLight", "loading-prosperolight-alpha.bin")
    render_label(48, "Connecting", "loading-connecting-alpha.bin")
