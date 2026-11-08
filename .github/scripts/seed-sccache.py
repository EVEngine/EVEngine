"""Seed the official sccache action's tool cache with a verified release asset.

Use the Release Asset API to avoid the failing release-download frontend.
The upstream action still owns PATH, runtime credentials and statistics.
"""

import hashlib
import io
import os
from pathlib import Path
import platform
import tarfile
import time
import urllib.request


ASSETS = {
    ("Windows", "AMD64"): (452060663, "c82319cc392b693e9fc72e6567cce47e218308f00fbf1df942e39b62e30f98da"),
    ("Linux", "x86_64"): (452060682, "aec995a83ad3dff3d14b6314e08858b7b73d35ca85a5bcf3d3a9ec07dee35588"),
    ("Darwin", "arm64"): (452060416, "ded590cae2c72042c61178632906bef62d635fa20d45f8b22110a2241f430960"),
    ("Darwin", "x86_64"): (452060604, "f7dbd055db75a938ab1539f5316c5d08e73a1b94c40ab170ddcc617f5bf18343"),
}


def main():
    asset, expected = ASSETS[(platform.system(), platform.machine())]
    url = f"https://api.github.com/repos/mozilla/sccache/releases/assets/{asset}"
    data = None
    for attempt in range(5):
        try:
            request = urllib.request.Request(url, headers={"Accept": "application/octet-stream"})
            with urllib.request.urlopen(request, timeout=120) as response:
                data = response.read()
            break
        except OSError:
            if attempt == 4:
                raise
            time.sleep(2 * (attempt + 1))
    if hashlib.sha256(data).hexdigest() != expected:
        raise ValueError("sccache v0.16.0 archive SHA256 mismatch")
    arch = {"AMD64": "x64", "x86_64": "x64", "arm64": "arm64"}[platform.machine()]
    target = Path(os.environ["RUNNER_TOOL_CACHE"]) / "sccache" / "0.16.0" / arch
    target.mkdir(parents=True, exist_ok=True)
    executable = "sccache.exe" if platform.system() == "Windows" else "sccache"
    # Extract only the executable as bytes; never trust archive paths or links.
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as archive:
        members = [m for m in archive if m.isfile() and Path(m.name).name == executable]
        if len(members) != 1:
            raise ValueError("Expected one sccache executable")
        with archive.extractfile(members[0]) as source:
            (target / executable).write_bytes(source.read())
    (target / executable).chmod(0o755)
    target.with_name(arch + ".complete").touch()
    print(f"Seeded verified sccache v0.16.0: {target}")


if __name__ == "__main__":
    main()
