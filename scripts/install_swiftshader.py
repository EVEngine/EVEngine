#!/usr/bin/env python3
"""Install the CI SwiftShader release without mutable latest-version metadata."""

from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile
import time
import zipfile

import utf8_stdio

TAG = "20261005"
ARCHIVE = "swiftshader-win64-5.0.0.1.zip"
ASSET_ID = 613494736
SHA256 = "657dd4f5ec5f4f05a10bcb7d6227b74a7cfdf1859b4ab3452f3c1764aee24c34"
FILES = {"vk_swiftshader.dll", "vk_swiftshader_icd.json", "vulkan-1.dll"}


def register_driver(destination: Path) -> None:
    """Enable this verified x64 ICD for elevated Windows CI processes."""
    import winreg

    manifest = str((destination / "vk_swiftshader_icd.json").resolve())
    # Elevated loaders ignore VK_ICD_FILENAMES and HKCU driver overrides.
    # Match the upstream installer's HKLM registration, without touching other ICDs.
    with winreg.CreateKeyEx(
        winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Khronos\Vulkan\Drivers", 0,
        winreg.KEY_SET_VALUE | winreg.KEY_QUERY_VALUE | winreg.KEY_WOW64_64KEY,
    ) as key:
        winreg.SetValueEx(key, manifest, 0, winreg.REG_DWORD, 0)
        if winreg.QueryValueEx(key, manifest) != (0, winreg.REG_DWORD):
            raise RuntimeError("SwiftShader ICD registration verification failed")
    print(f"Registered x64 SwiftShader ICD in HKLM: {manifest}")


def verify_runtime(destination: Path, *, registry_only: bool) -> None:
    """Require a real Vulkan instance, WSI extensions and SwiftShader device."""
    sdk_bin = Path(os.environ["VULKAN_SDK"]) / "Bin"
    executable = next((sdk_bin / name for name in ("vulkaninfoSDK.exe", "vulkaninfo.exe")
                       if (sdk_bin / name).is_file()), None)
    if executable is None:
        raise RuntimeError("Vulkan SDK vulkaninfo executable is missing")
    environment = os.environ.copy()
    for name in ("VK_ICD_FILENAMES", "VK_DRIVER_FILES", "VK_ADD_DRIVER_FILES"):
        environment.pop(name, None)
    if not registry_only:
        environment["VK_ICD_FILENAMES"] = str((destination / "vk_swiftshader_icd.json").resolve())
    result = subprocess.run(
        [str(executable), "--summary"], env=environment, capture_output=True,
        text=True, encoding="utf-8", errors="replace", timeout=60,
    )
    print(result.stdout)
    if result.stderr:
        print(result.stderr)
    result.check_returncode()
    for required in ("VK_KHR_surface", "VK_KHR_win32_surface", "SwiftShader Device"):
        if required not in result.stdout:
            raise RuntimeError(f"Vulkan preflight missing {required}")
    print(f"SwiftShader Vulkan preflight passed (registry_only={registry_only})")


def install(destination: Path) -> None:
    """Verify the full archive before touching the installed runtime."""
    with tempfile.TemporaryDirectory(prefix="eve-swiftshader-") as temporary:
        work = Path(temporary)
        archive = work / ARCHIVE
        for attempt in range(3):
            try:
                # A fresh API request avoids cached, expired signed redirects.
                resource = f"repos/jakoch/rasterizers/releases/assets/{ASSET_ID}?request={time.time_ns()}"
                with archive.open("wb") as output:
                    subprocess.run(
                        ["gh", "api", "--hostname", "github.com", "--allow-escape-sequences",
                         "-H", "Accept: application/octet-stream", resource],
                        stdout=output, stderr=subprocess.PIPE, check=True, timeout=120,
                    )
                break
            except (subprocess.CalledProcessError, subprocess.TimeoutExpired):
                if attempt == 2:
                    raise
                time.sleep(5)
        with archive.open("rb") as source:
            digest = hashlib.file_digest(source, "sha256").hexdigest()
        if digest != SHA256:
            raise RuntimeError(f"SwiftShader SHA-256 mismatch: {digest}")
        with zipfile.ZipFile(archive) as bundle:
            if len(bundle.infolist()) != len(FILES) or set(bundle.namelist()) != FILES:
                raise RuntimeError("Unexpected SwiftShader archive contents")
            # Read and CRC-check every member before modifying the destination.
            contents = {name: bundle.read(name) for name in FILES}
        destination.mkdir(parents=True, exist_ok=True)
        for name, data in contents.items():
            (destination / name).write_bytes(data)
        print(f"Installed SwiftShader 5.0.0.1 from {TAG}; SHA-256 verified")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--destination", type=Path, required=True)
    parser.add_argument("--register-driver", action="store_true",
                        help="Register HKLM ICD and verify real Vulkan discovery without environment overrides")
    args = parser.parse_args()
    install(args.destination)
    if args.register_driver:
        import ctypes

        print(f"Windows elevated: {bool(ctypes.windll.shell32.IsUserAnAdmin())}")
        register_driver(args.destination)
        verify_runtime(args.destination, registry_only=True)


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    main()
