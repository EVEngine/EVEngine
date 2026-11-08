#!/usr/bin/env python3
"""Install the CI SwiftShader release without mutable latest-version metadata."""

from __future__ import annotations

import argparse
import hashlib
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
    install(parser.parse_args().destination)


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    main()
