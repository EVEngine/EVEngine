"""Failure injection for the browser release SDK, independent of a browser install."""

from pathlib import Path
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from test_webgpu_sdk import check_archive  # noqa: E402


class WebGpuSdkArchiveTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.archive = self.root / "sdk.zip"
        self.destination = self.root / "unpacked"

    def write_archive(self, *, missing=None, version="0.7.0", extra=None):
        with zipfile.ZipFile(self.archive, "w") as bundle:
            for name in ("eve.html", "eve.js", "eve.wasm", "eve.data"):
                if name != missing:
                    bundle.writestr(f"eve-sdk/webgpu/bin/{name}", "runtime")
            bundle.writestr("eve-sdk/webgpu/share/eve/VERSION", version)
            bundle.writestr("eve-sdk/webgpu/share/eve/TARGET_PLATFORM", "webgpu")
            if extra:
                bundle.writestr(extra, "unsafe")

    def test_accepts_complete_sdk(self):
        self.write_archive()
        root = check_archive(self.archive, self.destination, "0.7.0")
        self.assertEqual((root / "eve.data").read_text(encoding="utf-8"), "runtime")

    def test_rejects_missing_preloaded_assets(self):
        self.write_archive(missing="eve.data")
        with self.assertRaisesRegex(KeyError, "eve.data"):
            check_archive(self.archive, self.destination, "0.7.0")

    def test_rejects_wrong_version(self):
        self.write_archive(version="0.6.1")
        with self.assertRaisesRegex(ValueError, "version mismatch"):
            check_archive(self.archive, self.destination, "0.7.0")

    def test_rejects_archive_path_escape_before_extraction(self):
        self.write_archive(extra="../escape")
        with self.assertRaisesRegex(ValueError, "unsafe archive"):
            check_archive(self.archive, self.destination, "0.7.0")
        self.assertFalse((self.root / "escape").exists())
        self.assertFalse(self.destination.exists())


if __name__ == "__main__":
    unittest.main()
