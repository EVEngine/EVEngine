import hashlib
import json
import tempfile
import unittest
from pathlib import Path

from scripts.verify_webgpu_dependencies import DependencyContractError, verify


class VerifyWebgpuDependenciesTest(unittest.TestCase):
    def fixture(self, root: Path) -> tuple[Path, Path]:
        archive = root / "port.zip"
        archive.write_bytes(b"immutable-port")
        lock = root / "lock.json"
        lock.write_text(
            json.dumps(
                {
                    "schema": "evengine.webgpu-dependencies",
                    "version": 1,
                    "emscripten": "6.0.9",
                    "emdawnwebgpu": {
                        "release": "v1",
                        "sha256": hashlib.sha256(b"immutable-port").hexdigest(),
                    },
                }
            ),
            encoding="utf-8",
        )
        return lock, archive

    def test_matching_identity_passes(self):
        with tempfile.TemporaryDirectory() as temporary:
            lock, archive = self.fixture(Path(temporary))
            self.assertEqual(verify(lock, archive, "6.0.9")["version"], 1)

    def test_version_drift_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            lock, archive = self.fixture(Path(temporary))
            with self.assertRaisesRegex(DependencyContractError, "version mismatch"):
                verify(lock, archive, "latest")

    def test_content_drift_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            lock, archive = self.fixture(Path(temporary))
            archive.write_bytes(b"tampered")
            with self.assertRaisesRegex(DependencyContractError, "SHA-256 mismatch"):
                verify(lock, archive, "6.0.9")


if __name__ == "__main__":
    unittest.main()
