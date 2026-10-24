#!/usr/bin/env python3
"""Download the pinned binary release from EVEngine/third-party."""

from __future__ import annotations

import argparse
import hashlib
import http.client
import json
import os
import platform
import re
import tempfile
import urllib.parse
import urllib.error
import urllib.request
import zipfile
from pathlib import Path

import utf8_stdio

ROOT = Path(__file__).resolve().parents[1]
PLATFORMS = ("win32", "linux", "macosx", "android", "ios")


class DownloadUnavailable(RuntimeError):
    """Release transfer failed; AUTO may build the pinned source instead."""


def pin() -> str:
    text = (ROOT / "cmake/third_party_pin.cmake").read_text(encoding="utf-8")
    match = re.search(r'set\(EVENGINE_THIRD_PARTY_PIN "([0-9a-f]{40})"', text)
    if not match:
        raise ValueError("Cannot read third-party pin")
    return match[1]


def release_tag() -> str:
    text = (ROOT / "cmake/third_party_pin.cmake").read_text(encoding="utf-8")
    match = re.search(r'set\(EVENGINE_THIRD_PARTY_RELEASE "([^\"]+)"', text)
    if not match:
        raise ValueError("Cannot read pinned third-party release")
    return match[1]


def validate_install(destination: Path, target: str, configuration: str, tag: str, architecture: str) -> None:
    manifest = json.loads((destination / "eve-third-party-package.json").read_text(encoding="utf-8"))
    expected = {"schema": "evengine.third-party-package", "version": 1,
                "platform": target, "configuration": configuration,
                "aggregate": pin(), "release": tag, "architecture": architecture}
    for key, value in expected.items():
        if manifest.get(key) != value:
            raise ValueError(f"Incompatible installed package {key}")
    validate_stamp((destination / "third-party-version.txt").read_text(encoding="utf-8"), target, configuration)
    if not (destination / "include").is_dir() or not (destination / "lib").is_dir():
        raise ValueError("Incomplete installed dependency package")


def package_name(target: str, configuration: str, tag: str, architecture: str) -> str:
    if target not in PLATFORMS or configuration not in ("Debug", "Release"):
        raise ValueError("Unsupported platform/configuration")
    if configuration == "Debug" and target != "win32":
        raise ValueError("Non-Windows dependencies are built as Release; use the Release package")
    if not re.fullmatch(r"[A-Za-z0-9_.-]+", tag):
        raise ValueError("Invalid release tag")
    return f"eve-third-party-{target}-{architecture}-{configuration.lower()}-{tag}.zip"


def validate_stamp(text: str, target: str, configuration: str) -> None:
    fields = dict(line.split("=", 1) for line in text.splitlines() if "=" in line)
    for key, expected in (("aggregate", pin()), ("platform", target), ("build_type", configuration)):
        if fields.get(key) != expected:
            raise ValueError(f"Third-party stamp {key}: {fields.get(key)!r}, expected {expected!r}")


def unpack(archive: Path, destination: Path, target: str, configuration: str, tag: str, architecture: str) -> None:
    """Validate before publishing; reject replacement of an existing install."""
    if destination.exists():
        raise ValueError(f"Destination already exists: {destination}; choose a new directory")
    with zipfile.ZipFile(archive) as bundle:
        manifest = json.loads(bundle.read("eve-third-party-package.json"))
        expected = {"schema": "evengine.third-party-package", "version": 1,
                    "platform": target, "configuration": configuration,
                    "aggregate": pin(), "release": tag, "architecture": architecture}
        for key, value in expected.items():
            if manifest.get(key) != value:
                raise ValueError(f"Incompatible package {key}: {manifest.get(key)!r}, expected {value!r}")
        # Unknown fields in schema v1 are ignored; unknown versions are rejected.
        validate_stamp(bundle.read("third-party-version.txt").decode("utf-8"), target, configuration)
        for entry in bundle.infolist():
            parts = entry.filename.replace("\\", "/").split("/")
            if entry.filename.startswith(("/", "\\")) or ".." in parts or ":" in entry.filename:
                raise ValueError(f"Unsafe archive entry: {entry.filename}")
        destination.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="eve-tp-", dir=destination.parent) as staging:
            tree = Path(staging) / "install"
            bundle.extractall(tree)
            if os.name != "nt":
                for entry in bundle.infolist():
                    mode = (entry.external_attr >> 16) & 0o777
                    if mode:
                        (tree / entry.filename).chmod(mode)
            if not (tree / "include").is_dir() or not (tree / "lib").is_dir():
                raise ValueError("Incomplete install in package")
            tree.rename(destination)


def fetch(repo: str, tag: str, target: str, configuration: str, destination: Path, architecture: str) -> None:
    # Match third_party_build.cmake: only Windows compiles Debug dependency libs.
    if target != "win32":
        configuration = "Release"
    if destination.exists():
        validate_install(destination, target, configuration, tag, architecture)
        print(f"Reusing validated dependency release: {destination}")
        return
    name = package_name(target, configuration, tag, architecture)
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repo):
        raise ValueError("Invalid GitHub repository")
    base = f"https://github.com/{repo}/releases/download/{urllib.parse.quote(tag, safe='')}/"
    with tempfile.TemporaryDirectory(prefix="eve-tp-download-") as temporary:
        sums = Path(temporary) / "SHA256SUMS"
        archive = Path(temporary) / name
        for filename, path in (("SHA256SUMS", sums), (name, archive)):
            print(f"Downloading third-party release: {base + filename}", flush=True)
            with path.open("wb") as stream:
                try:
                    with urllib.request.urlopen(base + filename, timeout=30) as response:
                        while True:
                            try:
                                block = response.read(1024 * 1024)
                            except (OSError, http.client.HTTPException) as error:
                                raise DownloadUnavailable(f"{filename}: {error}") from error
                            if not block:
                                break
                            stream.write(block)
                except (urllib.error.URLError, TimeoutError, ConnectionError, http.client.HTTPException) as error:
                    raise DownloadUnavailable(f"{filename}: {error}") from error
        entries = dict((parts[1].lstrip("*"), parts[0]) for line in sums.read_text(encoding="utf-8").splitlines()
                       if len(parts := line.split()) == 2)
        with archive.open("rb") as stream:
            hasher = hashlib.sha256()
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                hasher.update(block)
            digest = hasher.hexdigest()
        if entries.get(name) != digest:
            raise ValueError("Release package checksum mismatch or missing checksum")
        print(f"Verified third-party release SHA256: {name} {digest}", flush=True)
        unpack(archive, destination, target, configuration, tag, architecture)
    print(f"Installed {destination.resolve()}")
    print(f'CMAKE_EXTRA_ARGS="-DEVENGINE_THIRD_PARTY_BINARY_DIR={destination.resolve().as_posix()}"')


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("platform", choices=PLATFORMS)
    parser.add_argument("--tag", default=release_tag())
    parser.add_argument("--configuration", choices=("Debug", "Release"), default="Release")
    parser.add_argument("--architecture", choices=("x86_64", "arm64"))
    parser.add_argument("--prefix", type=Path)
    args = parser.parse_args()
    host = platform.machine().lower()
    architecture = args.architecture or ("arm64" if host in ("arm64", "aarch64") else "x86_64")
    if args.platform in ("android", "ios") and not args.architecture:
        architecture = "arm64"
    configuration = args.configuration if args.platform == "win32" else "Release"
    prefix = args.prefix or Path(f"build/third-party-releases/{args.tag}/{args.platform}-{architecture}-{configuration}")
    try:
        fetch("EVEngine/third-party", args.tag, args.platform, args.configuration, prefix, architecture)
    except DownloadUnavailable as error:
        parser.exit(2, f"third-party download unavailable: {error}\n")
    except (OSError, ValueError, KeyError, zipfile.BadZipFile) as error:
        parser.exit(1, f"third-party package: {error}\n")


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    main()
