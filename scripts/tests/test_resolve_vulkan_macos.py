"""SDK shell setup must work with absent and pre-existing optional variables."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(os.name != "nt" and shutil.which("bash"), "requires POSIX Bash")
class ResolveVulkanMacosTests(unittest.TestCase):
    def test_optional_pkg_config_path_is_initialized_and_preserved(self):
        for existing in (None, "/custom/pkgconfig"):
            with self.subTest(existing=existing), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                sdk = root / "SDK" / "1.4.363.0" / "macOS"
                (sdk / "lib").mkdir(parents=True)
                (sdk / "lib" / "libvulkan.dylib").touch()
                (sdk.parent / "iOS/lib/MoltenVK.xcframework/ios-arm64/MoltenVK.framework").mkdir(parents=True)
                (sdk.parent / "setup-env.sh").write_text(
                    'export PKG_CONFIG_PATH="/sdk/pkgconfig:$PKG_CONFIG_PATH"\n', encoding="utf-8")
                script = root / "resolve.sh"
                script.write_text((ROOT / ".github/scripts/resolve-vulkan-macos.sh").read_text(
                    encoding="utf-8") + '\nprintf "RESULT=%s\\n" "$PKG_CONFIG_PATH"\n', encoding="utf-8")
                env = dict(os.environ, VULKAN_SDK=str(sdk), HOME=str(root), GITHUB_ENV=str(root / "env"))
                env.pop("PKG_CONFIG_PATH", None)
                if existing is not None:
                    env["PKG_CONFIG_PATH"] = existing
                result = subprocess.run(["bash", str(script), str(root / "SDK")], env=env,
                                        capture_output=True, text=True, encoding="utf-8", check=False)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn("RESULT=/sdk/pkgconfig:" + (existing or ""), result.stdout)
