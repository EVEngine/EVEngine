import http.server
import io
import os
import threading
import zipfile

import numpy as np
import pytest


class _StubHandler(http.server.BaseHTTPRequestHandler):
    server_version = "Stub/1.0"

    def do_GET(self):
        if self.path == "/health":
            body = b'{"service":"eve.normal.crafter","status":"ok"}'
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        else:
            self.send_response(404)
            self.end_headers()

    def do_POST(self):
        if self.path == "/v1/normal-crafter":
            length = int(self.headers["Content-Length"])
            self.rfile.read(length)  # consume body (the uploaded mp4)
            # Return a stub normal zip of 3 frames.
            bio = io.BytesIO()
            with zipfile.ZipFile(bio, "w", zipfile.ZIP_STORED) as zf:
                for i in range(3):
                    arr = (np.ones((16, 16, 3), dtype=np.uint8) * i * 70)
                    import cv2
                    ok, buf = cv2.imencode(".png", arr)
                    zf.writestr(f"frame_{i + 1:06d}.png", buf.tobytes())
            body = bio.getvalue()
            self.send_response(200)
            self.send_header("Content-Type", "application/zip")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        else:
            self.send_response(404)
            self.end_headers()

    def log_message(self, *args):
        pass


@pytest.fixture(scope="module")
def stub_server():
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), _StubHandler)
    thread = threading.Thread(target=srv.serve_forever, daemon=True)
    thread.start()
    yield f"http://127.0.0.1:{srv.server_address[1]}"
    srv.shutdown()


def test_end_to_end_client_via_server(stub_server):
    from normal_crafter.client import run_client

    import tempfile
    from PIL import Image

    with tempfile.TemporaryDirectory() as root:
        frames = os.path.join(root, "in")
        os.makedirs(frames)
        for i in range(1, 4):
            Image.fromarray(np.ones((16, 16, 3), dtype=np.uint8) * i * 50
                            ).save(os.path.join(frames, f"frame_{i:06d}.png"))
        out = os.path.join(root, "normals")
        n, _, _ = run_client(stub_server, frames, out, fps=15)
        assert n == 3
        assert sorted(os.listdir(out)) == [
            "frame_000001.png", "frame_000002.png", "frame_000003.png"]
