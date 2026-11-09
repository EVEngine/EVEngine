"""Asset FetchContent helpers must retry transient download failures."""

from __future__ import annotations

import hashlib
import subprocess
import tempfile
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class AssetDependencyDownloadTests(unittest.TestCase):
    def test_verified_alternate_route_cache_and_fail_closed(self) -> None:
        payload = b"pinned canonical archive fixture"
        digest = hashlib.sha256(payload).hexdigest()
        requests: list[str] = []

        class Handler(BaseHTTPRequestHandler):
            def do_GET(self) -> None:
                requests.append(self.path)
                self.send_response(403 if self.path == "/denied" else 503 if self.path == "/fail" else 200)
                if self.path == "/denied":
                    self.send_header("X-RateLimit-Remaining", "0")
                    self.send_header("X-RateLimit-Reset", "1700000000")
                    self.send_header("Authorization", "must-not-be-logged")
                self.end_headers()
                self.wfile.write(payload if self.path == "/good" else b"invalid bytes")

            def log_message(self, *_args: object) -> None:
                pass

        server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            with tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                output = root / "archive.bin"
                script = root / "download.cmake"
                base = f"http://127.0.0.1:{server.server_port}"

                def download(first: str, second: str) -> subprocess.CompletedProcess[str]:
                    script.write_text(
                        f'include("{(ROOT / "cmake/asset_dependencies.cmake").as_posix()}")\n'
                        f'eve_download_archive_with_retries("{base}/{first}" '
                        f'"{output.as_posix()}" "{digest}" "{base}/{second}")\n',
                        encoding="utf-8",
                    )
                    return subprocess.run(
                        ["cmake", "-P", str(script)],
                        capture_output=True,
                        text=True,
                        encoding="utf-8",
                        timeout=40,
                        check=False,
                    )

                recovered = download("fail", "good")
                self.assertEqual(recovered.returncode, 0, recovered.stdout + recovered.stderr)
                self.assertEqual(requests, ["/fail", "/good"])
                self.assertEqual(output.read_bytes(), payload)
                self.assertIn("HTTP/1.0 503", recovered.stderr)
                requests.clear()
                self.assertEqual(download("fail", "fail").returncode, 0)
                self.assertEqual(requests, [])  # Verified cache needs no network.
                output.write_bytes(b"tampered cache")
                recovered = download("bad", "good")
                self.assertEqual(recovered.returncode, 0, recovered.stdout + recovered.stderr)
                self.assertIn("Hash mismatch", recovered.stderr)
                self.assertEqual(requests, ["/bad", "/good"])
                self.assertEqual(output.read_bytes(), payload)
                output.unlink()
                requests.clear()
                failed = download("bad", "fail")
                self.assertNotEqual(failed.returncode, 0)
                self.assertEqual(requests, ["/bad", "/fail", "/bad", "/fail", "/bad"])
                self.assertFalse(output.exists())
                self.assertIn("after 5 attempts", failed.stderr)
                requests.clear()
                denied = download("denied", "good")
                self.assertNotEqual(denied.returncode, 0)
                self.assertEqual(requests, ["/denied"])
                self.assertFalse(output.exists())
                self.assertIn("Access or rate-limit response", denied.stderr)
                self.assertRegex(denied.stderr, r"x-ratelimit-reset:\s+1700000000")
                self.assertNotIn("must-not-be-logged", denied.stderr)
        finally:
            server.shutdown()
            server.server_close()
            thread.join()

    def test_zstd_and_utf8proc_use_retry_prefetch(self) -> None:
        text = (ROOT / "cmake" / "asset_dependencies.cmake").read_text(encoding="utf-8")
        self.assertIn("eve_download_archive_with_retries", text)
        self.assertIn("_eve_downloads/zstd-1.5.7.tar.gz", text)
        self.assertIn("_eve_downloads/utf8proc-2.11.3.tar.gz", text)
        self.assertIn("Failed to download", text)
        self.assertIn("_eve_max_attempts 5", text)
        # Prefetch local archives; do not hit GitHub releases once per URL only.
        self.assertIn('URL "${_eve_zstd_archive}"', text)
        self.assertIn('URL "${_eve_utf8proc_archive}"', text)


class VulkanSdkRetryActionTests(unittest.TestCase):
    def test_windows_swiftshader_jobs_use_retry_action(self) -> None:
        action = ROOT / ".github" / "actions" / "install-vulkan-sdk" / "action.yml"
        self.assertTrue(action.is_file())
        text = action.read_text(encoding="utf-8")
        self.assertIn("attempt 1/3", text)
        self.assertIn("attempt 3/3", text)
        self.assertIn("jakoch/install-vulkan-sdk-action@", text)

        for rel in (
            ".github/workflows/ci.yml",
            ".github/workflows/release-ci.yml",
            ".github/workflows/sdk-release.yml",
        ):
            wf = (ROOT / rel).read_text(encoding="utf-8")
            self.assertIn("install_swiftshader: true", wf)
            # Every SwiftShader install must go through the retry wrapper.
            blocks = wf.split("install_swiftshader: true")
            # Each occurrence (except trailing split) should be preceded by the wrapper.
            for prefix in blocks[:-1]:
                self.assertIn(
                    "uses: ./.github/actions/install-vulkan-sdk",
                    prefix[-400:],
                    msg=f"{rel} SwiftShader install missing retry wrapper",
                )


if __name__ == "__main__":
    unittest.main()
