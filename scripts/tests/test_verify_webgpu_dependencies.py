import hashlib
import json
import stat
import tempfile
import unittest
from pathlib import Path
import zipfile

from scripts.verify_webgpu_dependencies import DependencyContractError, seed_port_cache, verify


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

    def port_archive(self, root, extra=None):
        archive = root / "port.zip"
        with zipfile.ZipFile(archive, "w") as bundle:
            bundle.writestr("emdawnwebgpu_pkg/emdawnwebgpu.port.py", "# unchanged upstream port\n")
            bundle.writestr("emdawnwebgpu_pkg/include/api.h", "original header\n")
            if extra:
                if isinstance(extra, str):
                    name = extra
                    extra = zipfile.ZipInfo(name)
                    extra.filename = name  # Keep Windows separators for the hostile-input fixture.
                bundle.writestr(extra, "unsafe")
        content = archive.read_bytes()
        lock = {"emdawnwebgpu": {"url": "https://github.com/google/dawn/releases/download/pin/port.zip",
                                "sha256": hashlib.sha256(content).hexdigest(),
                                "sha512": hashlib.sha512(content).hexdigest()}}
        return lock, archive

    def test_verified_source_cache_is_atomic_and_reusable(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            lock, archive = self.port_archive(root)
            ports = root / "ports"
            seed_port_cache(lock, archive, ports)
            seed_port_cache(lock, archive, ports)
            target = ports / "emdawnwebgpu"
            self.assertEqual((target / ".emscripten_url").read_text(encoding="utf-8").strip(),
                             lock["emdawnwebgpu"]["url"])
            self.assertEqual((target / "emdawnwebgpu_pkg/include/api.h").read_text(encoding="utf-8"),
                             "original header\n")
            self.assertEqual([p.name for p in ports.iterdir()], ["emdawnwebgpu"])

    def test_changed_existing_source_is_rejected_and_preserved(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            lock, archive = self.port_archive(root)
            ports = root / "ports"
            seed_port_cache(lock, archive, ports)
            header = ports / "emdawnwebgpu/emdawnwebgpu_pkg/include/api.h"
            header.write_text("changed", encoding="utf-8")
            with self.assertRaisesRegex(DependencyContractError, "existing port cache differs"):
                seed_port_cache(lock, archive, ports)
            self.assertEqual(header.read_text(encoding="utf-8"), "changed")

    def test_archive_must_match_both_hashes_before_cache_publication(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            lock, archive = self.port_archive(root)
            lock["emdawnwebgpu"]["sha512"] = "0" * 128
            with self.assertRaisesRegex(DependencyContractError, "SHA-512"):
                seed_port_cache(lock, archive, root / "ports")
            self.assertFalse((root / "ports").exists())

    def test_unmarked_existing_directory_is_not_overwritten(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            lock, archive = self.port_archive(root)
            destination = root / "ports/emdawnwebgpu"
            destination.mkdir(parents=True)
            sentinel = destination / "owned.txt"
            sentinel.write_text("preserve", encoding="utf-8")
            with self.assertRaisesRegex(DependencyContractError, "not a validated"):
                seed_port_cache(lock, archive, root / "ports")
            self.assertEqual(sentinel.read_text(encoding="utf-8"), "preserve")
            self.assertEqual(list(destination.iterdir()), [sentinel])

    def test_unsafe_archive_entries_never_publish(self):
        for name in ("../escape", "/absolute", "emdawnwebgpu_pkg/../escape", "emdawnwebgpu_pkg/a\\b"):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                lock, archive = self.port_archive(root, name)
                with self.assertRaisesRegex(DependencyContractError, "unsafe"):
                    seed_port_cache(lock, archive, root / "ports")
                self.assertFalse((root / "ports").exists())

    def test_symlink_entry_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            entry = zipfile.ZipInfo("emdawnwebgpu_pkg/link")
            entry.external_attr = (stat.S_IFLNK | 0o777) << 16
            lock, archive = self.port_archive(root, entry)
            with self.assertRaisesRegex(DependencyContractError, "unsafe"):
                seed_port_cache(lock, archive, root / "ports")


if __name__ == "__main__":
    unittest.main()
