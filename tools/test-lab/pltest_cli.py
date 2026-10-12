#!/usr/bin/env python3
# ps5-native-app-boilerplate - source-tree CLI entry point.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent / 'src'))
from pltest.cli import main
if __name__ == '__main__':
    raise SystemExit(main())
