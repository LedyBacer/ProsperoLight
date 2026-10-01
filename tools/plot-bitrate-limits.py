#!/usr/bin/env python3
# ps5-native-app-boilerplate - ProsperoLight decoder bitrate limit chart.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
#
"""Draw docs/images/bitrate-limits.svg from the measured decode-time model.

The model is the time Videodec2 needs for one 3840x2160 HEVC frame as a
function of its compressed size, fitted to console frame traces (HEVC SDR,
eight slices, one frame decoded at a time):

  frames up to about 105 KB:   3.73 ms + 45.3 us per KB   (6,804 frames)
  frames of 120 to 238 KB:     5.48 ms + 28.6 us per KB   (1,380 frames)

A frame rate is sustainable while the average frame decodes within one frame
interval. Above the largest measured frame the limit is an extrapolation.

usage: plot-bitrate-limits.py [--check] [output.svg]
"""

from pathlib import Path
import sys

SMALL = (3.73, 0.0453)  # ms, ms per KB
LARGE = (5.48, 0.0286)
LARGEST_MEASURED_KB = 238.0
FRAME_RATES = (120, 90, 60)
PRESETS = (20, 40, 50, 60, 70, 80, 100, 150, 200, 300)
AXIS_MAX = 300.0
COMFORT = 0.8  # Share of the limit that leaves room for larger-than-average frames.

GREEN, AMBER, RED = "#2da44e", "#d4a72c", "#cf222e"
TEXT, MUTED, BORDER = "#1f2328", "#59636e", "#d1d9e0"
FONT = "-apple-system,BlinkMacSystemFont,'Segoe UI','Noto Sans',Helvetica,Arial,sans-serif"


def mbps(kilobytes, fps):
    return kilobytes * 1024.0 * 8.0 * fps / 1e6


def largest_frame_kb(fps):
    """Largest average frame that still decodes within one frame interval."""
    budget = 1000.0 / fps
    crossover = (LARGE[0] - SMALL[0]) / (SMALL[1] - LARGE[1])
    small = (budget - SMALL[0]) / SMALL[1]
    return small if small <= crossover else (budget - LARGE[0]) / LARGE[1]


def limits():
    """Per frame rate: (fps, comfortable Mbps, limit Mbps, limit is measured)."""
    rows = []
    for fps in FRAME_RATES:
        frame = largest_frame_kb(fps)
        limit = mbps(frame, fps)
        measured = frame <= LARGEST_MEASURED_KB
        comfortable = COMFORT * limit if measured else mbps(LARGEST_MEASURED_KB, fps)
        rows.append((fps, comfortable, limit, measured))
    return rows


def rounded(value):
    return int(round(value / 5.0) * 5)


def render():
    width, height = 900, 380
    left, right = 110.0, 860.0
    scale = (right - left) / AXIS_MAX
    rows_top, row_height, row_gap = 92, 40, 20
    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
        f'viewBox="0 0 {width} {height}" role="img" '
        'aria-labelledby="title description" '
        f'font-family="{FONT}">',
        '<title id="title">4K HEVC: bitrate the PS5 decoder keeps up with</title>',
        '<desc id="description">Horizontal bars for 120, 90 and 60 frames per second. Green is '
        "smooth, amber is at the limit or not measured, red is where the decoder falls behind."
        "</desc>",
        f'<rect x="0.5" y="0.5" width="{width - 1}" height="{height - 1}" rx="10" fill="#ffffff" '
        f'stroke="{BORDER}"/>',
        f'<text x="28" y="38" font-size="18" font-weight="600" fill="{TEXT}">4K HEVC: bitrate '
        "the PS5 decoder keeps up with</text>",
        f'<text x="28" y="60" font-size="12.5" fill="{MUTED}">Bitrate the host actually '
        "delivers. A bitrate setting is only reached in busy scenes.</text>",
    ]
    bottom = rows_top + len(FRAME_RATES) * (row_height + row_gap) - row_gap
    labels = []  # Drawn last, above the preset guides.
    for index, (fps, comfortable, limit, measured) in enumerate(limits()):
        y = rows_top + index * (row_height + row_gap)
        comfortable_x = left + comfortable * scale
        limit_x = left + min(limit, AXIS_MAX) * scale
        middle = y + row_height / 2 + 4.5
        parts += [
            f'<text x="28" y="{middle:.1f}" font-size="15" font-weight="600" fill="{TEXT}">'
            f"{fps} FPS</text>",
            f'<rect x="{left:.1f}" y="{y}" width="{comfortable_x - left:.1f}" '
            f'height="{row_height}" fill="{GREEN}"/>',
            f'<rect x="{comfortable_x:.1f}" y="{y}" width="{limit_x - comfortable_x:.1f}" '
            f'height="{row_height}" fill="{AMBER}"/>',
            f'<rect x="{limit_x:.1f}" y="{y}" width="{right - limit_x:.1f}" '
            f'height="{row_height}" fill="{RED}"/>',
        ]
        labels += [
            f'<text x="{left + 12:.1f}" y="{middle:.1f}" font-size="13" font-weight="600" '
            f'fill="#ffffff">smooth up to {rounded(comfortable)} Mbps</text>',
            f'<text x="{limit_x + 10:.1f}" y="{middle:.1f}" font-size="13" font-weight="600" '
            f'fill="#ffffff">freezes above {"" if measured else "about "}{rounded(limit)} Mbps'
            "</text>",
        ]
        if not measured:
            labels.append(
                f'<text x="{(comfortable_x + limit_x) / 2:.1f}" y="{middle:.1f}" font-size="12" '
                f'font-weight="600" text-anchor="middle" fill="{TEXT}">not measured</text>'
            )
    axis_y = bottom + 14
    parts.append(
        f'<line x1="{left:.1f}" y1="{axis_y}" x2="{right:.1f}" y2="{axis_y}" stroke="{MUTED}" '
        'stroke-width="1"/>'
    )
    for preset in PRESETS:
        x = left + preset * scale
        parts += [
            f'<line x1="{x:.1f}" y1="{rows_top}" x2="{x:.1f}" y2="{bottom}" stroke="#ffffff" '
            'stroke-opacity="0.45" stroke-width="1" stroke-dasharray="3 3"/>',
            f'<line x1="{x:.1f}" y1="{axis_y}" x2="{x:.1f}" y2="{axis_y + 5}" stroke="{MUTED}"/>',
            f'<text x="{x:.1f}" y="{axis_y + 19}" font-size="11.5" text-anchor="middle" '
            f'fill="{TEXT}">{preset}</text>',
        ]
    parts += labels
    legend_y = axis_y + 60
    parts.append(
        f'<text x="{right:.1f}" y="{axis_y + 36}" font-size="11.5" text-anchor="end" '
        f'fill="{MUTED}">bitrate presets, Mbps (400 and 500 are beyond every limit)</text>'
    )
    legend = (
        (GREEN, "Smooth"),
        (AMBER, "At the limit, or not measured"),
        (RED, "Decoder falls behind: latency grows, then freezes"),
    )
    x = left
    for colour, label in legend:
        parts += [
            f'<rect x="{x:.1f}" y="{legend_y - 11}" width="14" height="14" rx="3" '
            f'fill="{colour}"/>',
            f'<text x="{x + 20:.1f}" y="{legend_y + 1}" font-size="12.5" fill="{TEXT}">{label}'
            "</text>",
        ]
        x += 40 + 6.1 * len(label)
    parts += [
        f'<text x="28" y="{legend_y + 30}" font-size="11.5" fill="{MUTED}">Measured on PS5 with '
        "HEVC SDR and eight slices per frame, decoding one frame at a time. 1440p and 1080p: at "
        "least these limits.</text>",
        "</svg>",
    ]
    return "\n".join(parts) + "\n"


def main(arguments):
    check = "--check" in arguments
    paths = [argument for argument in arguments if not argument.startswith("--")]
    root = Path(__file__).resolve().parents[1]
    output = Path(paths[0]) if paths else root / "docs/images/bitrate-limits.svg"
    image = render()
    for fps, comfortable, limit, measured in limits():
        print(
            f"{fps:3d} FPS: smooth up to {rounded(comfortable)} Mbps, limit "
            f"{rounded(limit)} Mbps ({'measured' if measured else 'extrapolated'}; "
            f"{largest_frame_kb(fps):.0f} KB per frame)"
        )
    if check:
        if not output.is_file() or output.read_text(encoding="utf-8") != image:
            print(f"{output} is out of date; run tools/plot-bitrate-limits.py", file=sys.stderr)
            return 1
        return 0
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(image, encoding="utf-8", newline="\n")
    print(f"wrote {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
