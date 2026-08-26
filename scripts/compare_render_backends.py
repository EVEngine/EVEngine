#!/usr/bin/env python3
"""Compare Vulkan and WebGPU render-parity artifact directories."""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path

from PIL import Image


def srgb_to_linear(value: int) -> float:
    channel = value / 255.0
    if channel <= 0.04045:
        return channel / 12.92
    return math.pow((channel + 0.055) / 1.055, 2.4)


def compare_scene(
    reference_dir: Path, candidate_dir: Path, manifest_path: Path
) -> list[str]:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    scene = manifest["scene"]
    profile = manifest.get("profile", "lit3d")
    reference = Image.open(reference_dir / f"{scene}.png").convert("RGBA")
    candidate = Image.open(candidate_dir / f"{scene}.png").convert("RGBA")
    failures: list[str] = []
    if reference.size != candidate.size:
        return [f"{scene}: dimensions differ: {reference.size} vs {candidate.size}"]

    ref_pixels = list(reference.get_flattened_data())
    candidate_pixels = list(candidate.get_flattened_data())
    alpha_different = sum(
        (ref[3] >= 128) != (candidate[3] >= 128)
        for ref, candidate in zip(ref_pixels, candidate_pixels)
    )
    alpha_ratio = alpha_different / len(ref_pixels)
    errors = sorted(
        abs(srgb_to_linear(ref[channel]) - srgb_to_linear(candidate[channel]))
        for ref, candidate in zip(ref_pixels, candidate_pixels)
        for channel in range(3)
    )
    mean_error = sum(errors) / len(errors)
    percentile_99 = errors[min(len(errors) - 1, math.ceil(len(errors) * 0.99) - 1)]

    mean_limit = 2.0 / 255.0 if profile == "flat2d" else 6.0 / 255.0
    p99_limit = 8.0 / 255.0 if profile == "flat2d" else 20.0 / 255.0
    if alpha_ratio > 0.005:
        failures.append(f"{scene}: alpha coverage delta {alpha_ratio:.4%} > 0.5%")
    if mean_error > mean_limit:
        failures.append(
            f"{scene}: mean linear RGB error {mean_error:.6f} > {mean_limit:.6f}"
        )
    if percentile_99 > p99_limit:
        failures.append(
            f"{scene}: p99 linear RGB error {percentile_99:.6f} > {p99_limit:.6f}"
        )
    print(
        f"{scene}: alpha={alpha_ratio:.4%} mean={mean_error:.6f} "
        f"p99={percentile_99:.6f} profile={profile}"
    )
    return failures


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("reference", type=Path, help="Vulkan artifact directory")
    parser.add_argument("candidate", type=Path, help="WebGPU artifact directory")
    args = parser.parse_args()

    manifests = sorted(args.reference.glob("*.json"))
    if not manifests:
        parser.error(f"no manifests found in {args.reference}")
    failures: list[str] = []
    for manifest in manifests:
        candidate_manifest = args.candidate / manifest.name
        if not candidate_manifest.exists():
            failures.append(f"missing candidate manifest: {candidate_manifest.name}")
            continue
        failures.extend(compare_scene(args.reference, args.candidate, manifest))
    if failures:
        print("\n".join(f"ERROR: {failure}" for failure in failures), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
