#!/usr/bin/env python3
# ps5-native-app-boilerplate - Raw production exFAT image and payload verification.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import hashlib
from pathlib import Path
import sys
from mkpfs.exfat import open_exfat
from mkpfs.exfat_writer import write_exfat_image

source, output = map(Path, sys.argv[1:])
write_exfat_image(source, output)
reader = open_exfat(str(output))
count = 0
for entry in reader.iter_files():
    expected = hashlib.sha256((source / entry.rel_path).read_bytes()).digest()
    actual = hashlib.sha256()
    for chunk in reader.read_file(entry):
        actual.update(chunk)
    if actual.digest() != expected:
        raise SystemExit("exFAT payload mismatch: " + entry.rel_path)
    count += 1
print(f"Raw exFAT payload verified: {count} files, {output.stat().st_size} bytes")
