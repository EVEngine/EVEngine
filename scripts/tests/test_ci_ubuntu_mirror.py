"""Ubuntu replica selection must preserve package identity and signature policy."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from scripts import utf8_stdio

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / ".github/scripts/configure-ubuntu-mirror.sh"


class UbuntuMirrorTests(unittest.TestCase):
    def run_script(self, root: Path) -> subprocess.CompletedProcess:
        bash = shutil.which("bash")
        if os.name == "nt":
            # Windows' system bash.exe launches WSL; use the native Git Bash
            # that the Windows Makefile/CI shell already requires instead.
            git_root = Path(subprocess.check_output(["git", "--exec-path"], encoding="utf-8").strip()).parents[2]
            bash = (git_root / "bin/bash.exe").as_posix()
            self.assertTrue(Path(bash).is_file(), "Git Bash is required on Windows")
        self.assertIsNotNone(bash, "bash is required for the CI provisioning contract")
        return subprocess.run(
            [bash, SCRIPT.as_posix(), root.as_posix()], capture_output=True, encoding="utf-8", check=False
        )

    def test_registered_https_replica_preserves_trust_and_is_idempotent(self):
        with tempfile.TemporaryDirectory(prefix="ubuntu mirror ") as directory:
            root = Path(directory)
            (root / "sources.list.d").mkdir()
            original = (
                "Types: deb\n"
                "URIs: http://azure.archive.ubuntu.com/ubuntu https://security.ubuntu.com/ubuntu/\n"
                "Suites: noble noble-updates noble-backports noble-security\n"
                "Components: main restricted universe multiverse\n"
                "Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg\n"
                "Trusted: no\nCheck-Valid-Until: yes\n"
            )
            source = root / "sources.list.d/ubuntu.sources"
            source.write_text(original, encoding="utf-8")
            (root / "apt-mirrors.txt").write_text(
                "http://archive.ubuntu.com/ubuntu priority:1\n", encoding="utf-8"
            )
            legacy = root / "sources.list"
            legacy.write_text("deb https://us.archive.ubuntu.com/ubuntu noble main\n", encoding="utf-8")
            result = self.run_script(root)
            self.assertEqual(result.returncode, 0, result.stderr)
            expected = original.replace(
                "http://azure.archive.ubuntu.com/ubuntu", "https://mirrors.edge.kernel.org/ubuntu/"
            ).replace("https://security.ubuntu.com/ubuntu/", "https://mirrors.edge.kernel.org/ubuntu/")
            self.assertEqual(source.read_text(encoding="utf-8"), expected)
            self.assertEqual(
                (root / "apt-mirrors.txt").read_text(encoding="utf-8"),
                "https://mirrors.edge.kernel.org/ubuntu/ priority:1\n",
            )
            self.assertEqual(
                legacy.read_text(encoding="utf-8"), "deb https://mirrors.edge.kernel.org/ubuntu/ noble main\n"
            )
            first = {p: p.read_bytes() for p in root.rglob("*") if p.is_file()}
            self.assertEqual(self.run_script(root).returncode, 0)
            self.assertEqual(first, {p: p.read_bytes() for p in first})

    def test_ports_use_the_separate_signed_arm_archive(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "sources.list.d").mkdir()
            source = root / "sources.list.d/ubuntu.sources"
            original = (
                "Types: deb\nURIs: http://ports.ubuntu.com/ubuntu-ports https://ports.ubuntu.com/ubuntu-ports/\n"
                "Architectures: arm64\nSuites: noble noble-updates noble-backports noble-security\n"
                "Components: main universe\nSigned-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg\n"
                "Trusted: no\nCheck-Valid-Until: yes\n"
            )
            source.write_text(original, encoding="utf-8")
            expected = original.replace(
                "https://ports.ubuntu.com/ubuntu-ports/", "https://mirrors.ocf.berkeley.edu/ubuntu-ports/"
            ).replace("http://ports.ubuntu.com/ubuntu-ports", "https://mirrors.ocf.berkeley.edu/ubuntu-ports/")
            result = self.run_script(root)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(source.read_text(encoding="utf-8"), expected)
            self.assertNotIn("mirrors.edge.kernel.org", source.read_text(encoding="utf-8"))
            first = source.read_bytes()
            self.assertEqual(self.run_script(root).returncode, 0)
            self.assertEqual(source.read_bytes(), first)

    def test_unrelated_repositories_are_unchanged(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "sources.list.d").mkdir()
            for name, uri in [
                ("vendor.sources", "https://packages.microsoft.com/ubuntu/24.04/prod"),
                ("other.sources", "https://example.test/archive.ubuntu.com/ubuntu"),
            ]:
                (root / "sources.list.d" / name).write_text(
                    f"Types: deb\nURIs: {uri}\nSuites: noble\nSigned-By: /existing/key.gpg\n", encoding="utf-8"
                )
            first = {p: p.read_bytes() for p in root.rglob("*") if p.is_file()}
            result = self.run_script(root)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(first, {p: p.read_bytes() for p in first})

    def test_missing_apt_root_is_fatal(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assertNotEqual(self.run_script(Path(directory) / "missing").returncode, 0)


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    unittest.main()
