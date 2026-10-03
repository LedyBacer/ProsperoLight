# Measuring frame pacing on PS5

Enable **Diagnostic logs**, select the desired frame pacing mode and stop the
stream normally after 60–90 seconds. Compare the same codec, scene, resolution,
FPS, VSync setting and network conditions. Repeat each mode rather than using
one run as proof of improvement.

After stopping, download these files through FTP from the mounted app's
`download0` directory:

- H.264 / HEVC: `performance-frames-modeN.csv` and `performance-last.json`.
- PyroWave: `pyrowave-output-modeN.csv` and `prosperolight-pyrowave.log`.

`N` is 0 (Unpaced), 1 (Paced), or 2 (Paced+VRR). Each new stream replaces
its previous trace for that backend and mode, so all three modes can be tested
before collecting files. H.264 and HEVC share native traces: collect one codec
before testing the other. There are at most three native and three PyroWave traces.
Recording stops at 32,768 samples (about 273 seconds at 120 FPS); the header
reports omitted samples. Capture uses bounded memory and does not write files
in the streaming loop. Turning logs off prevents export. The previous native
reports inside `moonlight/` are legacy files; new reports are outside that
restricted pairing directory. Pairing credentials are never included.

Keep copies with names such as `hevc-paced.csv`, `hevc-vrr.csv` and
`hevc-unpaced.csv`. Generate a standalone HTML graph and JSON statistics:

```sh
python3 tools/plot-frame-pacing.py hevc-unpaced.csv hevc-paced.csv hevc-vrr.csv \
    --fps 120 --output hevc-pacing.html
```

The green line is the requested frame period: 8.33 ms at 120 FPS, 16.67 ms at
60 FPS. Look for fewer long intervals followed by short intervals, lower p95,
p99 and RMS interval error, without more dropped frames or additional latency.
The summary excludes the first three seconds where the trace is long enough.
Plots preserve interval minima/maxima when reducing samples for display.
Counts above 1.5 periods and below 0.5 periods are indicators, not a universal
pass/fail threshold. Fixed-refresh output can legitimately alternate intervals
when the source FPS is not divisible into the physical refresh rate.

## What these clocks mean

Native submit is a CPU marker after the presentation call. Native completion is
the CPU observation after the completed flip wait; polling/scheduling delay is
included. This is stronger than measuring decode speed, but it is not an exact
hardware scanout timestamp.

PyroWave submit is the CPU callback immediately before `queuePresent`. Paced
modes wait for GPU preparation before this callback; Unpaced preserves its
original order and only records the marker. `observed_us` samples the VideoOut
flip count after present returns. Repeated counts do not represent new flips.
If the count jumps by more than one, the graph excludes that interval and reports
an aggregated observation: individual display intervals cannot be reconstructed.
A single prepared image is still used; this instrumentation adds no GPU pipeline.

CPU submission graphs alone cannot prove smooth display. GPU fence completion
is not scanout. VideoOut counter observations do not prove that the TV activated
VRR. For end-to-end confirmation, use a high-speed camera on a scrolling scene
or visible frame counter, alongside the TV's refresh-rate indicator. These traces
do not measure input-to-photon latency.
