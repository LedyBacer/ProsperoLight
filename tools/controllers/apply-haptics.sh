#!/usr/bin/env bash
# ps5-native-app-boilerplate - Vibepollo controller PCM protocol extension.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
python3 "$root/tools/controllers/apply-haptics.py"
