#!/usr/bin/env python3
# ps5-native-app-boilerplate - Rewrite a release ZIP so every entry is stored open to all.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
"""The console only starts an app whose files are open to every user (0777). A tool that
unpacks the release ZIP and keeps its permissions (on the console, or with rsync/scp from
a Mac or Linux PC) would otherwise leave 0644 files, and the console can answer
CE-107750-0. The contents are not changed, only the stored permissions.

usage: zip-open-modes.py ARCHIVE.zip
"""
import os
import sys
import zipfile

path = sys.argv[1]
temporary = path + ".tmp"
with zipfile.ZipFile(path) as source, zipfile.ZipFile(temporary, "w") as target:
    for info in source.infolist():
        data = source.read(info)
        folder = info.is_dir()
        info.create_system = 3  # Unix: the high half of external_attr is the mode
        info.external_attr = ((0o040777 if folder else 0o100777) << 16) | (0x10 if folder else 0)
        target.writestr(info, data, compress_type=info.compress_type)
os.replace(temporary, path)
with zipfile.ZipFile(path) as check:
    wrong = [i.filename for i in check.infolist() if (i.external_attr >> 16) & 0o777 != 0o777]
    if wrong or check.testzip() is not None:
        raise SystemExit(f"ZIP check failed: {wrong[:3]}")
    print(f"{path}: {len(check.infolist())} entries stored as 0777")
