#!/usr/bin/env python3
"""Verify immutable WebGPU toolchain inputs before compilation starts."""

from __future__ import annotations

import argparse
import hashlib
import io
import json
from pathlib import Path
import stat
import tempfile
import zipfile

try:
    from scripts.ci_result import CIResult, FailureKind
except ModuleNotFoundError:  # Direct execution sets sys.path to scripts/.
    from ci_result import CIResult, FailureKind
import utf8_stdio


class DependencyContractError(ValueError):
    """The acquired dependency does not match the repository lock."""


def verify(lock_path: Path, archive_path: Path, emscripten_version: str) -> dict:
    lock = json.loads(lock_path.read_text(encoding="utf-8"))
    if lock.get("schema") != "evengine.webgpu-dependencies" or lock.get("version") != 1:
        raise DependencyContractError("unsupported WebGPU dependency lock schema")
    if emscripten_version != lock["emscripten"]:
        raise DependencyContractError(
            f"Emscripten version mismatch: {emscripten_version} != {lock['emscripten']}"
        )
    actual = hashlib.sha256(archive_path.read_bytes()).hexdigest()
    expected = lock["emdawnwebgpu"]["sha256"]
    if actual != expected:
        raise DependencyContractError(
            f"emdawnwebgpu SHA-256 mismatch: {actual} != {expected}"
        )
    return lock


def seed_port_cache(lock: dict, archive_path: Path, ports: Path) -> None:
    """Publish the unchanged locked archive before starting compiler processes.

    Emscripten 6.0.9 tools/ports/__init__.py uses this source-cache layout and
    URL marker. No toolchain sources, headers or include flags are overridden.
    Existing cache contents must match; mismatches are never overwritten.
    """
    data = archive_path.read_bytes()
    identity = lock["emdawnwebgpu"]
    if hashlib.sha256(data).hexdigest() != identity["sha256"]:
        raise DependencyContractError("port archive changed after verification")
    if hashlib.sha512(data).hexdigest() != identity["sha512"]:
        raise DependencyContractError("port archive differs from Emscripten's SHA-512 identity")
    destination = ports / "emdawnwebgpu"
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        entries = archive.infolist()
        names = [entry.filename for entry in entries]
        if len(set(names)) != len(names) or "emdawnwebgpu_pkg/emdawnwebgpu.port.py" not in names:
            raise DependencyContractError("invalid port archive inventory")
        for entry in entries:
            name = entry.orig_filename
            if (name != entry.filename or not name.startswith("emdawnwebgpu_pkg/") or "\\" in name or ":" in name
                    or ".." in name.split("/") or stat.S_ISLNK(entry.external_attr >> 16)):
                raise DependencyContractError(f"unsafe port archive path: {name}")
        marker = destination / ".emscripten_url"
        if destination.exists() or destination.is_symlink():
            if destination.is_symlink() or not marker.is_file() or marker.is_symlink():
                raise DependencyContractError("existing port cache is not a validated source cache")
            if marker.read_text(encoding="utf-8").strip() != identity["url"]:
                raise DependencyContractError("existing port cache URL differs from the lock")
            for entry in entries:
                if entry.is_dir():
                    continue
                path = destination / entry.filename
                if (path.is_symlink() or not path.resolve().is_relative_to(destination.resolve())
                        or not path.is_file() or path.read_bytes() != archive.read(entry)):
                    raise DependencyContractError(f"existing port cache differs: {entry.filename}")
            print(f"Reusing verified Emscripten port source cache: {destination}")
            return
        ports.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="eve-port-", dir=ports) as temporary:
            staging = Path(temporary) / "source"
            archive.extractall(staging)
            (staging / ".emscripten_url").write_text(identity["url"] + "\n", encoding="utf-8")
            staging.rename(destination)
    print(f"Seeded verified Emscripten port source cache: {destination}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--lock", type=Path, required=True)
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--emscripten-version", required=True)
    parser.add_argument("--result-json", type=Path)
    parser.add_argument("--port-cache", type=Path, help="PORTS directory reported by em-config; seed before compilation")
    args = parser.parse_args()
    try:
        lock = verify(args.lock, args.archive, args.emscripten_version)
        if args.port_cache:
            seed_port_cache(lock, args.archive, args.port_cache)
    except (OSError, KeyError, json.JSONDecodeError, zipfile.BadZipFile, DependencyContractError) as error:
        if args.result_json:
            CIResult.failure(
                FailureKind.INFRASTRUCTURE,
                "WEBGPU_DEPENDENCY_VERIFICATION_FAILED",
                "webgpu-dependencies",
                str(error),
            ).write(args.result_json)
        print(f"WEBGPU DEPENDENCY FAILURE: {error}")
        return 2
    if args.result_json:
        CIResult.success("webgpu-dependencies").write(args.result_json)
    print(
        f"WebGPU dependencies verified: Emscripten {lock['emscripten']}, "
        f"emdawnwebgpu {lock['emdawnwebgpu']['release']}"
    )
    return 0


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    raise SystemExit(main())
