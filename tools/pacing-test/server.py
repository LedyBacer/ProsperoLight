# ps5-native-app-boilerplate - LAN pacing report receiver.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
from ftplib import Error
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import uuid
from decoder_benchmark import DecoderBenchmarks

MAX_BYTES = 2 * 1024 * 1024


def handler(directory, reports, benchmarks=None):
    class Handler(SimpleHTTPRequestHandler):
        def __init__(self, *args, **kwargs):
            super().__init__(*args, directory=str(directory), **kwargs)

        def reply(self, data, code=200):
            body = json.dumps(data, ensure_ascii=False).encode()
            self.send_response(code)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            if self.path.startswith('/api/decoder/runs/') and benchmarks:
                try:
                    self.reply(benchmarks.status(self.path.rsplit('/', 1)[-1]))
                except (ValueError, OSError):
                    self.send_error(404)
                return
            super().do_GET()

        def do_POST(self):
            if self.path not in ('/api/reports', '/api/decoder/start'):
                self.send_error(404)
                return
            try:
                size = int(self.headers.get('Content-Length', '0'))
                if not 0 < size <= MAX_BYTES:
                    self.send_error(413)
                    return
                data = json.loads(self.rfile.read(size))
                if self.path == '/api/decoder/start':
                    if not benchmarks:
                        raise ValueError('PS5 capture disabled')
                    self.reply({'id': benchmarks.start(data['settings'])}, 201)
                    return
                if not isinstance(data, dict) or data.get('schema') != 1 or not isinstance(data.get('settings'), dict):
                    raise ValueError('Invalid report')
                if data['settings'].get('suite') == 'decoder-v1':
                    if not benchmarks:
                        raise ValueError('PS5 capture disabled')
                    self.reply({'id': benchmarks.finish(data)}, 201)
                    return
                report_id = uuid.uuid4().hex
                reports.mkdir(parents=True, exist_ok=True)
                with (reports / (report_id + '.json')).open('x') as output:
                    json.dump(data, output, ensure_ascii=False)
                body = json.dumps({'id': report_id}).encode()
                self.send_response(201)
                self.send_header('Content-Type', 'application/json')
                self.send_header('Content-Length', str(len(body)))
                self.end_headers()
                self.wfile.write(body)
            except (ValueError, UnicodeError, KeyError, TypeError, OSError, Error) as exc:
                self.reply({'error': str(exc)}, 400)

    return Handler


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', type=int, default=9075)
    parser.add_argument('--bind', default='0.0.0.0')
    parser.add_argument('--reports', type=Path, required=True)
    parser.add_argument('--ps5', default='192.168.1.137', help='PS5 FTP host for decoder test capture')
    parser.add_argument('--ftp-port', type=int, default=2121)
    args = parser.parse_args()
    benchmarks = DecoderBenchmarks(args.reports / 'decoder', args.ps5, args.ftp_port)
    ThreadingHTTPServer((args.bind, args.port), handler(Path(__file__).parent, args.reports, benchmarks)).serve_forever()


if __name__ == '__main__':
    main()
