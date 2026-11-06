"""Run the actual C++ client against deterministic SSUI protocol fault fixtures."""
import argparse
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
import subprocess
import socketserver
import threading
import struct
import zlib
from pathlib import Path
import sys
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    target = parser.add_mutually_exclusive_group(required=True)
    target.add_argument('--client')
    target.add_argument('--native-eve')
    parser.add_argument('--project')
    parser.add_argument('--mode', choices=['success', 'hash', 'missing', 'failed', 'absent', 'cancel', 'destroy'], required=True)
    args = parser.parse_args()
    def chunk(kind, data):
        return struct.pack('!I', len(data)) + kind + data + struct.pack('!I', zlib.crc32(kind + data))

    png = (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('!2I5B', 8, 8, 8, 2, 0, 0, 0))
           + chunk(b'IDAT', zlib.compress((b'\0' + b'\xc0\x40\x20' * 8) * 8)) + chunk(b'IEND', b''))

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_GET(self):
            if args.mode == 'absent':
                self.send_error(503)
                return
            if self.path.endswith('/capabilities'):
                if args.mode in {'cancel', 'destroy'}:
                    time.sleep(.2)
                payload = {'protocol': 'ssui-generation', 'version': '1'}
            elif self.path.endswith('/recipes'):
                payload = {'items': [{'id': 'mock.checker@1'}]}
            elif self.path == '/asset.png':
                self.send_response(200)
                self.send_header('Content-Length', str(len(png)))
                self.end_headers()
                self.wfile.write(png)
                return
            else:
                self.send_error(404)
                return
            self.reply(payload)

        def do_POST(self):
            self.rfile.read(int(self.headers.get('Content-Length', '0')))
            if self.path.endswith('/cancel'):
                self.reply({'state': 'cancelled'})
                return
            artifacts = None if args.mode == 'missing' else [{
                'kind': 'image', 'mimeType': 'image/png', 'url': '/asset.png',
                'sha256': 'bad' if args.mode == 'hash' else hashlib.sha256(png).hexdigest(),
            }]
            self.reply({'jobId': 'fixture', 'state': 'failed' if args.mode == 'failed' else 'succeeded',
                        'progress': 100, 'artifacts': artifacts})

        def reply(self, payload):
            data = json.dumps(payload).encode()
            self.send_response(200)
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            self.wfile.write(data)

    class Server(ThreadingHTTPServer):
        def server_bind(self):
            # A loopback fixture does not need reverse-DNS discovery of this host.
            socketserver.TCPServer.server_bind(self)
            self.server_name = 'localhost'
            self.server_port = self.server_address[1]

    server = Server(('127.0.0.1', 0), Handler)
    thread = threading.Thread(target=server.serve_forever)
    thread.start()
    try:
        if args.native_eve:
            if args.mode != 'success' or not args.project:
                parser.error('Native smoke requires --mode success and --project')
            command = [sys.executable, str(Path(__file__).with_name('native_smoke.py')),
                       '--eve', args.native_eve, '--project', args.project,
                       '--server', f'http://127.0.0.1:{server.server_port}']
        else:
            command = [args.client, '--testcase=^ssui.protocol$']
        result = subprocess.run(command, timeout=150 if args.native_eve else 20, check=False,
                                env={**os.environ, 'SSUI_TEST_SERVER': f'http://127.0.0.1:{server.server_port}',
                                     'SSUI_TEST_MODE': args.mode})
        return result.returncode
    finally:
        server.shutdown()
        server.server_close()
        thread.join()


if __name__ == '__main__':
    raise SystemExit(main())
