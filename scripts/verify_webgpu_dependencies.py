#!/usr/bin/env python3
"""Verify immutable WebGPU toolchain inputs before compilation starts."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

try:
    from scripts.ci_result import CIResult, FailureKind
except ModuleNotFoundError:  # Direct execution sets sys.path to scripts/.
    from ci_result import CIResult, FailureKind


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


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--lock", type=Path, required=True)
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--emscripten-version", required=True)
    parser.add_argument("--result-json", type=Path)
    args = parser.parse_args()
    try:
        lock = verify(args.lock, args.archive, args.emscripten_version)
    except (OSError, KeyError, json.JSONDecodeError, DependencyContractError) as error:
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
    raise SystemExit(main())
