"""Cross-host inputs must pass the real target packaging gate before running."""

import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(shutil.which("bash"), "cross consumer check requires Bash")
class CrossConsumerTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def prepare(self, *, package_exit=0, frames=30):
        archive = self.root / "eve-sdk-linux-v0.7.0.zip"
        # A controlled native packaging process isolates the transfer runner's
        # responsibilities: fail on command failure, checksum failure or missing acceptance.
        runtime = f"""#!/usr/bin/env bash
set -eu
[ "$1" = package ]
[ -f "$2/main.nut" ]
exit_code={package_exit}
[ "$exit_code" -eq 0 ] || exit "$exit_code"
output="${{@: -1}}"
mkdir -p "$output"
printf 'acceptance=passed\\nframes={frames}\\n' > "$output/build-identity.manifest"
printf '[package-check] passed: {frames} frames\\n' > "$output/acceptance.log"
"""
        with zipfile.ZipFile(archive, "w") as bundle:
            bundle.writestr("eve-sdk/linux/bin/eve", runtime)
            bundle.writestr("eve-sdk/linux/share/eve/TARGET_PLATFORM", "linux")
        (self.root / "SHA256SUMS").write_text(
            hashlib.sha256(archive.read_bytes()).hexdigest() + "  " + archive.name + "\n", encoding="utf-8")
        with zipfile.ZipFile(self.root / "game.eve", "w") as game:
            game.writestr("main.nut", "eve_init = function() {};")

    def run_transfer(self):
        return subprocess.run(
            [shutil.which("bash"), (ROOT / ".github/scripts/prepare-cross-consumer.sh").as_posix(), "linux", self.root.as_posix()],
            capture_output=True, encoding="utf-8", check=False,
        )

    def test_requires_target_acceptance(self):
        self.prepare()
        result = self.run_transfer()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_rejects_failed_target_package(self):
        self.prepare(package_exit=7)
        self.assertNotEqual(self.run_transfer().returncode, 0)
        self.assertFalse((self.root / "verified").exists())

    def test_rejects_incomplete_frame_acceptance(self):
        self.prepare(frames=29)
        self.assertNotEqual(self.run_transfer().returncode, 0)

    def test_rejects_missing_or_duplicate_checksum_before_extracting(self):
        self.prepare()
        sums = self.root / "SHA256SUMS"
        entry = sums.read_text(encoding="utf-8")
        for content in ("", entry + entry, "invalid  eve-sdk-linux-v0.7.0.zip\n"):
            with self.subTest(content=content):
                sums.write_text(content, encoding="utf-8")
                self.assertNotEqual(self.run_transfer().returncode, 0)
                self.assertFalse((self.root / "eve-sdk").exists())

    def test_rejects_modified_sdk_before_running_it(self):
        self.prepare()
        with (self.root / "eve-sdk-linux-v0.7.0.zip").open("ab") as archive:
            archive.write(b"tampered")
        self.assertNotEqual(self.run_transfer().returncode, 0)
        self.assertFalse((self.root / "eve-sdk").exists())


if __name__ == "__main__":
    unittest.main()
