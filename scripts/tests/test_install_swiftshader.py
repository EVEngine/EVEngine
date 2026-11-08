"""Pinned software Vulkan installs must fail closed before changing the runtime."""

import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import install_swiftshader as installer  # noqa: E402


class SwiftShaderInstallTests(unittest.TestCase):
    def test_verified_archive_installs_and_corruption_preserves_existing_files(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            fixture = root / "fixture.zip"
            with zipfile.ZipFile(fixture, "w") as bundle:
                for name in installer.FILES:
                    bundle.writestr(name, b"verified runtime")
            data = fixture.read_bytes()
            target = root / "installed"

            def download(_command, **kwargs):
                kwargs["stdout"].write(data)

            with patch.object(installer.subprocess, "run", side_effect=download), patch.object(
                installer, "SHA256", hashlib.sha256(data).hexdigest()
            ):
                installer.install(target)
            self.assertEqual({p.name for p in target.iterdir()}, installer.FILES)
            with patch.object(installer.subprocess, "run", side_effect=download):
                with self.assertRaisesRegex(RuntimeError, "SHA-256 mismatch"):
                    installer.install(target)
            self.assertTrue(all(p.read_bytes() == b"verified runtime" for p in target.iterdir()))

    def test_download_exhaustion_does_not_install(self):
        with tempfile.TemporaryDirectory() as temporary:
            target = Path(temporary) / "installed"
            with patch.object(installer.subprocess, "run", side_effect=subprocess.TimeoutExpired("gh", 120)) as run:
                with patch.object(installer.time, "sleep"):
                    with self.assertRaises(subprocess.TimeoutExpired):
                        installer.install(target)
            self.assertEqual(run.call_count, 3)
            self.assertFalse(target.exists())

    def test_unexpected_members_are_rejected_before_install(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            fixture = root / "fixture.zip"
            with zipfile.ZipFile(fixture, "w") as bundle:
                bundle.writestr("../escape.dll", b"unexpected")
            data = fixture.read_bytes()

            def download(_command, **kwargs):
                kwargs["stdout"].write(data)

            with patch.object(installer.subprocess, "run", side_effect=download), patch.object(
                installer, "SHA256", hashlib.sha256(data).hexdigest()
            ):
                with self.assertRaisesRegex(RuntimeError, "archive contents"):
                    installer.install(root / "installed")
            self.assertFalse((root / "installed").exists())
