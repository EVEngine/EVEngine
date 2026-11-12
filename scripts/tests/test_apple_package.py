"""Failure atomicity and profile authorization for Apple SDK assembly."""
from __future__ import annotations

import argparse
import datetime
from pathlib import Path
import plistlib
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import apple_package as apple  # noqa: E402


class ApplePackageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.game = self.root / "game"
        self.game.mkdir()
        for name in ("config.nut", "main.nut"):
            (self.game / name).write_text("// test\n", encoding="utf-8")
        self.sdk = self.root / "sdk"
        (self.sdk / "share/eve").mkdir(parents=True)
        (self.sdk / "share/eve/TARGET_PLATFORM").write_text("ios\n", encoding="utf-8")
        self.output = self.root / "output"

    def args(self, **changes):
        values = dict(platform="ios", game=str(self.game), sdk=str(self.sdk), output=str(self.output),
                      bundle_id="com.example.game", name="Game", version="1.0.0", sign_identity="",
                      profile="", device="", simulator=False, unsigned=False)
        values.update(changes)
        return argparse.Namespace(**values)

    def test_output_cannot_overwrite_or_recurse_into_sources(self):
        for output in (self.game, self.game / "out", self.sdk / "out", self.root):
            with self.subTest(output=output), self.assertRaises(apple.PackageError):
                apple.validate_paths(self.game, self.sdk, output)
        self.assertTrue((self.game / "main.nut").is_file())

    def test_game_symlinks_rejected(self):
        (self.game / "escape").symlink_to(self.root)
        with self.assertRaisesRegex(apple.PackageError, "symlinks"):
            apple.validate_paths(self.game, self.sdk, self.output)

    @patch.object(apple.sys, "platform", "darwin")
    def test_missing_signing_material_does_not_publish(self):
        with self.assertRaisesRegex(apple.PackageError, "--sign-identity"):
            apple.build(self.args())
        self.assertFalse(self.output.exists())

    @patch.object(apple.sys, "platform", "darwin")
    def test_copy_failure_does_not_publish(self):
        with self.assertRaises(FileNotFoundError):
            apple.build(self.args(unsigned=True))
        self.assertFalse(self.output.exists())
        self.assertEqual([], list(self.root.glob(".eve-apple-*")))

    def shell(self):
        app = self.sdk / "bin/eve.app"
        app.mkdir(parents=True)
        (app / "Info.plist").write_bytes(plistlib.dumps({"CFBundleExecutable": "eve"}))
        (app / "eve").write_bytes(b"fixture-executable")
        return app

    @patch.object(apple.sys, "platform", "darwin")
    def test_signing_failure_cleans_staging_and_preserves_sdk(self):
        app = self.shell()
        with patch.object(apple, "run", return_value=b"platform IOSSIMULATOR\n"), \
                patch.object(apple, "sign_bundle", side_effect=apple.PackageError("signing failed")):
            with self.assertRaisesRegex(apple.PackageError, "signing failed"):
                apple.build(self.args(simulator=True))
        self.assertFalse(self.output.exists())
        self.assertEqual([], list(self.root.glob(".eve-apple-*")))
        self.assertEqual(b"fixture-executable", (app / "eve").read_bytes())
        self.assertFalse((app / "game").exists())

    @patch.object(apple.sys, "platform", "darwin")
    def test_device_sdk_rejected_by_simulator_before_publication(self):
        self.shell()
        with patch.object(apple, "run", return_value=b"platform IOS\n"):
            with self.assertRaisesRegex(apple.PackageError, "IOSSIMULATOR"):
                apple.build(self.args(simulator=True))
        self.assertFalse(self.output.exists())

    def profile(self, **changes):
        profile = dict(ExpirationDate=datetime.datetime(2099, 1, 1), TeamIdentifier=["TEAM"],
                       ApplicationIdentifierPrefix=["TEAM"], ProvisionedDevices=["DEVICE"],
                       DeveloperCertificates=[b"certificate"],
                       Entitlements={"application-identifier": "TEAM.com.example.*", "get-task-allow": True})
        profile.update(changes)
        return profile

    def test_profile_rejects_expiry_bundle_device_and_missing_private_key(self):
        for profile, bundle, device, message in (
            (self.profile(ExpirationDate=datetime.datetime(2000, 1, 1)), "com.example.game", "", "expired"),
            (self.profile(), "com.other.game", "", "bundle ID"),
            (self.profile(), "com.example.game", "OTHER", "UDID"),
            (self.profile(), "com.example.game", "DEVICE", "private key"),
        ):
            with self.subTest(message=message), patch.object(apple, "run", side_effect=[plistlib.dumps(profile), b""]):
                with self.assertRaisesRegex(apple.PackageError, message):
                    apple.load_profile(Path("profile"), bundle, "identity", device)

    def test_authorized_profile_specializes_entitlements(self):
        fingerprint = apple.hashlib.sha1(b"certificate").hexdigest().upper()
        with patch.object(apple, "run", side_effect=[plistlib.dumps(self.profile()),
                          f'  1) {fingerprint} "identity"\n'.encode()]):
            result = apple.load_profile(Path("profile"), "com.example.game", "identity", "DEVICE")
        self.assertEqual("TEAM.com.example.game", result["application-identifier"])

    @patch.object(apple.sys, "platform", "darwin")
    def test_simulator_cannot_use_device_options(self):
        with self.assertRaisesRegex(apple.PackageError, "Simulator"):
            apple.build(self.args(simulator=True, device="DEVICE"))


if __name__ == "__main__":
    unittest.main()
