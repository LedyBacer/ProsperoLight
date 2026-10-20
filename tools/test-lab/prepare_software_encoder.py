#!/usr/bin/env python3
# ps5-native-app-boilerplate - prepare one isolated Vibepollo libx264 app.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import asyncio
import json
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent / 'src'))
from pltest.host.encoder_profile import software_profile
from pltest.host.session_guard import SessionGuard


def main():
    parser = argparse.ArgumentParser(description='Plan or create an isolated software H.264 app; preserve NVENC/global settings')
    parser.add_argument('--url', required=True)
    parser.add_argument('--token-file', type=Path, required=True)
    parser.add_argument('--certificate-sha256', required=True)
    parser.add_argument('--apply', action='store_true', help='Create the missing app; default only inspects/plans')
    args = parser.parse_args()
    if not args.token_file.is_file() or args.token_file.stat().st_size > 4096:
        parser.error('token file unavailable or oversized')
    api = SessionGuard(args.url, args.token_file.read_text().strip(),
                       certificate_sha256=args.certificate_sha256)
    print(json.dumps(asyncio.run(software_profile(api, args.apply)), indent=2))


if __name__ == '__main__':
    main()
