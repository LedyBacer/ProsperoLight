# ps5-native-app-boilerplate - local coordinator server entry point.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import os


def main(argv=None):
    parser = argparse.ArgumentParser(description='Run the local Test Lab API')
    parser.add_argument('--host', default='127.0.0.1')
    parser.add_argument('--port', type=int, default=8765)
    parser.add_argument('--data-dir', default='build/test-lab-coordinator')
    parser.add_argument('--mode', choices=('fake', 'real'), default=os.environ.get('PLTEST_API_MODE', 'fake'))
    parser.add_argument('--reload', action='store_true')
    args = parser.parse_args(argv)
    if args.host not in ('127.0.0.1', 'localhost', '::1') and not os.environ.get('PLTEST_API_TOKEN'):
        parser.error('binding beyond loopback requires PLTEST_API_TOKEN')
    if args.host not in ('127.0.0.1', 'localhost', '::1') and (not os.environ.get('PLTEST_ALLOWED_HOSTS') or not os.environ.get('PLTEST_ALLOWED_ORIGINS')):
        parser.error('LAN bind also requires PLTEST_ALLOWED_HOSTS and PLTEST_ALLOWED_ORIGINS')
    import uvicorn
    os.environ['PLTEST_DATA_DIR'] = args.data_dir
    os.environ['PLTEST_API_MODE'] = args.mode
    uvicorn.run('pltest.coordinator.api:create_app', factory=True, host=args.host,
                port=args.port, reload=args.reload)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
