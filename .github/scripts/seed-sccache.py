"""Seed the official sccache action's tool cache with a verified release asset.

Prefer the public release download; use the API only after transient failures.
The upstream action still owns PATH, runtime credentials and statistics.
"""

import hashlib
import io
import os
from email.utils import parsedate_to_datetime
from pathlib import Path
import platform
import tarfile
import time
import urllib.error
import urllib.request


ASSETS = {
    ("Windows", "AMD64"): (452060663, "c82319cc392b693e9fc72e6567cce47e218308f00fbf1df942e39b62e30f98da"),
    ("Linux", "x86_64"): (452060682, "aec995a83ad3dff3d14b6314e08858b7b73d35ca85a5bcf3d3a9ec07dee35588"),
    ("Darwin", "arm64"): (452060416, "ded590cae2c72042c61178632906bef62d635fa20d45f8b22110a2241f430960"),
    ("Darwin", "x86_64"): (452060604, "f7dbd055db75a938ab1539f5316c5d08e73a1b94c40ab170ddcc617f5bf18343"),
}


def server_delays(headers):
    """Return all server-specified retry delays; callers honor the longest."""
    delays = []
    retry = headers.get("Retry-After")
    reset = headers.get("X-RateLimit-Reset")
    if retry:
        try:
            delays.append(float(retry))
        except ValueError:
            delays.append(parsedate_to_datetime(retry).timestamp() - time.time())
    if reset:
        delays.append(float(reset) - time.time())
    return delays


def fetch_verified(urls, expected):
    """Fetch at most five times; rate-limited routes never switch endpoints."""
    route = 0
    waited = 0.0
    for attempt in range(5):
        try:
            request = urllib.request.Request(urls[route], headers={"Accept": "application/octet-stream"})
            with urllib.request.urlopen(request, timeout=120) as response:
                data = response.read()
        except urllib.error.HTTPError as error:
            error.close()
            limited = error.code == 429 or (
                error.code == 403
                and (error.headers.get("X-RateLimit-Remaining") == "0" or "rate limit" in str(error.reason).lower())
            )
            if limited:
                # Do not evade API quotas by moving to another host/route.
                delays = server_delays(error.headers)
                if not delays:
                    raise RuntimeError("Rate limited without a retry time; stopping requests") from error
                delay = max([1.0, *delays])
                if attempt == 4 or waited + delay > 120:
                    raise RuntimeError("Rate-limit reset exceeds the bounded wait budget; stopping requests") from error
                print(f"HTTP {error.code}: respecting rate-limit wait of {delay:.1f}s")
            elif 500 <= error.code < 600:
                if attempt == 4:
                    raise
                delay = max([2 * (attempt + 1), *server_delays(error.headers)])
                if waited + delay > 120:
                    raise RuntimeError("Server retry time exceeds the bounded wait budget") from error
                route = (route + 1) % len(urls)
                print(f"HTTP {error.code}: retrying an official route after {delay}s")
            else:
                # Includes access denial; never turn a denied request into a route switch.
                raise
        except OSError:
            if attempt == 4:
                raise
            delay = 2 * (attempt + 1)
            route = (route + 1) % len(urls)
        else:
            if hashlib.sha256(data).hexdigest() != expected:
                raise ValueError("sccache v0.16.0 archive SHA256 mismatch")
            return data
        time.sleep(delay)
        waited += delay
    raise RuntimeError("sccache download exhausted its five attempts")


def main():
    system, machine = platform.system(), platform.machine()
    asset, expected = ASSETS[(system, machine)]
    release_arch = {"AMD64": "x86_64", "x86_64": "x86_64", "arm64": "aarch64"}[machine]
    release_platform = {"Windows": "pc-windows-msvc", "Linux": "unknown-linux-musl", "Darwin": "apple-darwin"}[system]
    filename = f"sccache-v0.16.0-{release_arch}-{release_platform}.tar.gz"
    data = fetch_verified([
        f"https://github.com/mozilla/sccache/releases/download/v0.16.0/{filename}",
        f"https://api.github.com/repos/mozilla/sccache/releases/assets/{asset}",
    ], expected)
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
