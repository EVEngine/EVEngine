"""Pinned software Vulkan installs must fail closed before changing the runtime."""

import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import MagicMock, patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import install_swiftshader as installer  # noqa: E402


class SwiftShaderInstallTests(unittest.TestCase):
    def test_ci_action_requires_registration_and_runtime_preflight(self):
        action = Path(__file__).resolve().parents[2] / ".github/actions/install-vulkan-sdk/action.yml"
        self.assertIn("--destination C:/SwiftShader --register-driver", action.read_text(encoding="utf-8"))

    def test_registry_registration_is_machine_wide_x64_and_read_back(self):
        registry = SimpleNamespace(
            HKEY_LOCAL_MACHINE=1, KEY_SET_VALUE=2, KEY_QUERY_VALUE=4, KEY_WOW64_64KEY=8,
            REG_DWORD=4, CreateKeyEx=MagicMock(), SetValueEx=MagicMock(),
            QueryValueEx=MagicMock(return_value=(0, 4)),
        )
        destination = Path("fixture-runtime")
        manifest = str((destination / "vk_swiftshader_icd.json").resolve())
        with patch.dict(sys.modules, {"winreg": registry}):
            installer.register_driver(destination)
            registry.CreateKeyEx.assert_called_once_with(1, r"SOFTWARE\Khronos\Vulkan\Drivers", 0, 14)
            key = registry.CreateKeyEx.return_value.__enter__.return_value
            registry.SetValueEx.assert_called_once_with(key, manifest, 0, 4, 0)
            registry.QueryValueEx.assert_called_once_with(key, manifest)
            registry.QueryValueEx.return_value = (1, 4)
            with self.assertRaisesRegex(RuntimeError, "registration verification"):
                installer.register_driver(destination)
            registry.CreateKeyEx.side_effect = PermissionError("HKLM denied")
            with self.assertRaises(PermissionError):
                installer.register_driver(destination)

    def test_registry_preflight_requires_real_device_and_wsi_without_env_overrides(self):
        with tempfile.TemporaryDirectory() as temporary:
            destination = Path(temporary)
            executable = destination / "Bin/vulkaninfoSDK.exe"
            executable.parent.mkdir()
            executable.write_bytes(b"tool fixture")
            output = "VK_KHR_surface VK_KHR_win32_surface SwiftShader Device"
            result = subprocess.CompletedProcess([], 0, stdout=output, stderr="")
            environment = {"VULKAN_SDK": str(destination), "VK_ICD_FILENAMES": "ignored.json",
                           "VK_DRIVER_FILES": "ignored.json", "VK_ADD_DRIVER_FILES": "ignored.json"}
            with patch.dict(installer.os.environ, environment), patch.object(
                installer.subprocess, "run", return_value=result
            ) as run:
                installer.verify_runtime(destination, registry_only=True)
                child_env = run.call_args.kwargs["env"]
                for name in ("VK_ICD_FILENAMES", "VK_DRIVER_FILES", "VK_ADD_DRIVER_FILES"):
                    self.assertNotIn(name, child_env)
                for required in ("VK_KHR_surface", "VK_KHR_win32_surface", "SwiftShader Device"):
                    result.stdout = output.replace(required, "")
                    with self.assertRaisesRegex(RuntimeError, "preflight missing"):
                        installer.verify_runtime(destination, registry_only=True)
                result.returncode = 1
                with self.assertRaises(subprocess.CalledProcessError):
                    installer.verify_runtime(destination, registry_only=True)

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
