#!/usr/bin/env python3
"""Serve the EVEngine WebGPU build directory with cross-origin isolation.

The Emscripten build runs the engine on pthreads and needs SharedArrayBuffer,
so the page MUST be served with COOP/COEP headers -- opening eve.html via
file:// will not work. This server adds those headers plus no-store caching so
rebuilt eve.js / eve.wasm / eve.data are always picked up.

Usage (from the repo root):
    python scripts/serve_webgpu.py                  # serves build/webgpu-web/src/engine :8090
    python scripts/serve_webgpu.py <engine-dir> [port]
"""

import argparse
import http.server
import os
import sys
from pathlib import Path


class PlaygroundHandler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        # Required for Emscripten pthreads / SharedArrayBuffer.
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        # The engine files are regenerated on every build; never cache them.
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def log_message(self, fmt, *args):
        sys.stderr.write("[serve] %s\n" % (fmt % args))


def main():
    default_dir = (
        Path(__file__).resolve().parent.parent
        / "build" / "webgpu-web" / "src" / "engine"
    )
    parser = argparse.ArgumentParser(
        description="Serve the EVEngine WebGPU build (COOP/COEP)."
    )
    parser.add_argument(
        "engine_dir",
        nargs="?",
        type=Path,
        default=default_dir,
        help="Directory containing eve.html (default: %(default)s)",
    )
    parser.add_argument("port", nargs="?", type=int, default=8090,
                        help="Port (default: 8090)")
    args = parser.parse_args()

    engine_dir = args.engine_dir.resolve()
    if not (engine_dir / "eve.html").is_file():
        print("error: %s does not contain eve.html" % engine_dir)
        print(
            "Build first:\n"
            "  emcmake cmake -B build/webgpu-web -G Ninja -DCMAKE_BUILD_TYPE=Release\n"
            "  cmake --build build/webgpu-web"
        )
        return 1

    os.chdir(engine_dir)
    httpd = http.server.ThreadingHTTPServer(("127.0.0.1", args.port), PlaygroundHandler)
    print("Serving %s" % engine_dir)
    print("Open http://127.0.0.1:%d/eve.html in Chrome/Edge 113+ (WebGPU enabled)." % args.port)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
