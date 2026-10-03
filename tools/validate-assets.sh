#!/usr/bin/env bash
# ps5-native-app-boilerplate - Portable presentation asset validation.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Validates the launcher icon, optional BC7 backgrounds, optional ATRAC9
# selection audio, and launcher PCM sound effects without Windows tools.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
asset_directory=${1:-"$root/sce_sys"}

[[ -d $asset_directory ]] || {
    echo "presentation asset directory not found: $asset_directory" >&2
    exit 2
}
command -v python3 >/dev/null || {
    echo "missing required command: python3" >&2
    exit 2
}

python3 - "$asset_directory" <<'PY'
from pathlib import Path
import struct
import sys

root = Path(sys.argv[1])
icon_path = root / "icon0.png"
if not icon_path.is_file():
    raise SystemExit(f"required launcher icon not found: {icon_path}")
icon = icon_path.read_bytes()
if (len(icon) < 24 or icon[:8] != b"\x89PNG\r\n\x1a\n"
        or struct.unpack(">II", icon[16:24]) != (512, 512)):
    raise SystemExit(f"{icon_path} must be a 512x512 PNG")

pics = [root / "pic0.dds", root / "pic1.dds"]
if pics[0].exists() != pics[1].exists():
    raise SystemExit("supply both pic0.dds and pic1.dds, or neither")
for path in pics if pics[0].exists() else []:
    data = path.read_bytes()
    if len(data) < 148 or data[:4] != b"DDS ":
        raise SystemExit(f"{path} is not a DDS file")
    header_size = struct.unpack_from("<I", data, 4)[0]
    height, width = struct.unpack_from("<II", data, 12)
    mipmaps = struct.unpack_from("<I", data, 28)[0]
    format_id, dimension = struct.unpack_from("<II", data, 128)
    array_size = struct.unpack_from("<I", data, 140)[0]
    expected = 148 + width // 4 * (height // 4) * 16
    profile = (header_size, width, height, mipmaps, data[84:88], format_id,
               dimension, array_size, len(data))
    expected_profile = (124, 3840, 2160, 1, b"DX10", 98, 3, 1, expected)
    if profile != expected_profile:
        raise SystemExit(
            f"{path} must be a single 3840x2160 BC7_UNORM DX10 2D DDS without mipmaps"
        )

sound = root / "snd0.at9"
if sound.exists():
    data = sound.read_bytes()
    if (len(data) < 128 or data[:4] != b"RIFF" or data[8:12] != b"WAVE"
            or struct.unpack_from("<I", data, 4)[0] + 8 != len(data)
            or len(data) > 2_097_152):
        raise SystemExit(f"{sound} is not a complete supported RIFF/WAVE file")
    chunks = {}
    position = 12
    while position + 8 <= len(data):
        chunk_id = data[position:position + 4]
        size = struct.unpack_from("<I", data, position + 4)[0]
        payload = position + 8
        if payload + size > len(data):
            raise SystemExit(f"{sound} contains a truncated RIFF chunk")
        chunks[chunk_id] = (payload, size)
        position = payload + size + size % 2
    required = (b"fmt ", b"fact", b"smpl", b"data")
    if not all(chunk in chunks for chunk in required):
        raise SystemExit(f"{sound} is missing required ATRAC9 RIFF chunks")
    fmt = chunks[b"fmt "][0]
    smpl = chunks[b"smpl"][0]
    if chunks[b"fmt "][1] < 40 or chunks[b"smpl"][1] < 32:
        raise SystemExit(f"{sound} contains undersized format or loop metadata")
    channels = struct.unpack_from("<H", data, fmt + 2)[0]
    byte_rate = struct.unpack_from("<I", data, fmt + 8)[0]
    atrac9_guid = bytes.fromhex("D242E147BA368D4D88FC61654F8C836C")
    if (struct.unpack_from("<H", data, fmt)[0] != 0xFFFE
            or channels not in (1, 2)
            or struct.unpack_from("<I", data, fmt + 4)[0] != 48_000
            or not 0 < byte_rate <= channels * 12_000
            or data[fmt + 24:fmt + 40] != atrac9_guid
            or struct.unpack_from("<I", data, smpl + 28)[0] < 1):
        raise SystemExit(
            f"{sound} must be looped 48 kHz ATRAC9 within the supported bitrate"
        )

# The launcher's sounds: one or more recordings per cue, "<cue>_NN.wav".
sfx_root = root.parent / "assets" / "audio" / "sfx" / "glass"
sounds = sorted(sfx_root.glob("*.wav")) if sfx_root.is_dir() else []
for cue in ("focus", "select", "back", "tab", "error", "notify", "launch", "welcome"):
    if not any(path.name.startswith(cue + "_") for path in sounds):
        raise SystemExit(f"required launcher sound not found: {sfx_root}/{cue}_01.wav")
for path in sounds:
    data = path.read_bytes()
    if (len(data) < 44 or len(data) > 2_097_152 or data[:4] != b"RIFF"
            or data[8:12] != b"WAVE"
            or struct.unpack_from("<I", data, 4)[0] + 8 != len(data)):
        raise SystemExit(f"{path} is not a complete supported RIFF/WAVE file")
    chunks = {}
    position = 12
    while position + 8 <= len(data):
        chunk_id = data[position:position + 4]
        size = struct.unpack_from("<I", data, position + 4)[0]
        payload = position + 8
        if payload + size > len(data):
            raise SystemExit(f"{path} contains a truncated RIFF chunk")
        chunks[chunk_id] = (payload, size)
        position = payload + size + size % 2
    if b"fmt " not in chunks or b"data" not in chunks:
        raise SystemExit(f"{path} is missing PCM format or sample data")
    fmt, fmt_size = chunks[b"fmt "]
    encoding, channels, rate, _, _, bits = struct.unpack_from("<HHIIHH", data, fmt)
    if (fmt_size < 16 or encoding != 1 or channels not in (1, 2) or rate != 48_000
            or bits != 16 or chunks[b"data"][1] == 0):
        raise SystemExit(f"{path} must be 48 kHz signed 16-bit PCM, mono or stereo")

print(f"Presentation assets validated: {root}")
PY
