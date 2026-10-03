#!/usr/bin/env python3
# ps5-native-app-boilerplate / ProsperoLight - Output interval report.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare bounded native/PyroWave CSV traces; no third-party packages required."""
import argparse
import csv
import html
import json
import statistics
from pathlib import Path


def series(rows, field, count_field=None):
    result = []
    previous = None
    skipped = 0
    for row in rows:
        stamp = int(row.get(field, 0))
        if not stamp or (field == 'completion_us' and int(row.get('outcome', 0)) != 1):
            continue
        count = int(row[count_field]) if count_field else 0
        if previous:
            old_stamp, old_count = previous
            if count_field and count == old_count:
                continue
            if count_field and count - old_count != 1:
                skipped += 1
            elif stamp > old_stamp:
                result.append((stamp / 1e6, (stamp - old_stamp) / 1000))
        previous = stamp, count
    if result:
        origin = result[0][0]
        result = [(t - origin, value) for t, value in result]
    return result, skipped


def panel(title, points, expected, skipped):
    # Exclude initial three seconds from summary; plot them for visibility.
    values = [v for t, v in points if t >= 3]
    if not values:
        values = [v for _, v in points]
    if not values:
        return '<p>No samples: ' + html.escape(title) + '</p>', {}
    ordered = sorted(values)
    percentile = lambda p: ordered[min(len(ordered) - 1, int((len(ordered) - 1) * p))]
    stats = dict(samples=len(values), median_ms=statistics.median(values),
                 p95_ms=percentile(.95), p99_ms=percentile(.99), max_ms=max(values),
                 rms_error_ms=(sum((v - expected) ** 2 for v in values) / len(values)) ** .5,
                 over_1_5_period=sum(v > 1.5 * expected for v in values),
                 under_0_5_period=sum(v < .5 * expected for v in values),
                 aggregated_observations_skipped=skipped)
    duration = max(points[-1][0], .001)
    ceiling = max(expected * 2.2, max(v for _, v in points) * 1.05)
    # Per horizontal pixel retain minimum AND maximum so spikes do not disappear.
    buckets = {}
    for t, v in points:
        buckets.setdefault(int(t / duration * 1000), []).append(v)
    lines = ''.join(f'<line x1="{x+55}" x2="{x+55}" y1="{250-max(v)/ceiling*220:.2f}" y2="{250-min(v)/ceiling*220-1:.2f}" stroke="#56b6ff"/>' for x, v in buckets.items())
    y = 250 - expected / ceiling * 220
    svg = f'<svg viewBox="0 0 1100 290"><rect width="1100" height="290" fill="#102030"/><line x1="55" x2="1055" y1="{y}" y2="{y}" stroke="#72df9c"/>{lines}<text x="55" y="20" fill="white">0–{ceiling:.1f} ms; green = {expected:.3f} ms</text><text x="55" y="278" fill="white">0 s</text><text x="980" y="278" fill="white">{duration:.1f} s</text></svg>'
    return '<h2>' + html.escape(title) + '</h2>' + svg + '<pre>' + html.escape(json.dumps(stats, indent=2)) + '</pre>', stats


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('csv', nargs='+', type=Path)
    parser.add_argument('--fps', type=float, required=True)
    parser.add_argument('--output', type=Path, default=Path('frame-pacing.html'))
    args = parser.parse_args()
    if not 0 < args.fps <= 120:
        parser.error('FPS must be greater than zero and at most 120')
    sections, summaries = [], {}
    for path in args.csv:
        with path.open() as stream:
            rows = list(csv.DictReader(line for line in stream if not line.startswith('#')))
        pyro = bool(rows and 'flip_count' in rows[0])
        fields = [('submit_us', 'CPU submission marker', None)]
        fields += [('observed_us', 'VideoOut counter observation (not scanout)', 'flip_count')] if pyro else [('completion_us', 'Native observed flip completion (includes polling delay)', None)]
        for field, label, counter in fields:
            points, skipped = series(rows, field, counter)
            title = str(path) + ': ' + label
            section, stats = panel(title, points, 1000 / args.fps, skipped)
            sections.append(section)
            summaries[title] = stats
    intro = '<h1>Frame pacing intervals</h1><p>Native completion is observed by the CPU, not a hardware scanout timestamp. PyroWave counters can combine multiple flips; those intervals are excluded, never divided into invented samples. PyroWave submit marks the CPU callback immediately before queuePresent (after pacing when enabled). Compare the same codec, FPS and scene. Summary excludes the first 3 seconds when available. These measurements cannot prove panel smoothness or input latency.</p>'
    args.output.write_text('<!doctype html><meta charset="utf-8"><title>Frame pacing</title><style>body{background:#081421;color:#eee;font:16px sans-serif;max-width:1200px;margin:30px auto}svg{width:100%}pre{white-space:pre-wrap}h2{overflow-wrap:anywhere;font-size:18px}</style>' + intro + ''.join(sections))
    args.output.with_suffix('.json').write_text(json.dumps(summaries, indent=2))
    print(args.output.resolve())


if __name__ == '__main__':
    main()
