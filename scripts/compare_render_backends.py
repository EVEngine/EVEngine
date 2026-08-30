#!/usr/bin/env python3
"""Compare Vulkan and WebGPU render-parity artifact directories."""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path

from PIL import Image


class ArtifactContractError(ValueError):
    """Raised when render-parity producers emitted incomplete artifact sets."""


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


def validate_artifact_contract(
    reference_dir: Path, candidate_dir: Path
) -> list[Path]:
    """Validate that both backends emitted the same complete manifest/image set."""
    reference_manifests = {path.name: path for path in reference_dir.glob("*.json")}
    candidate_manifests = {path.name: path for path in candidate_dir.glob("*.json")}
    if not reference_manifests:
        raise ArtifactContractError(f"no manifests found in {reference_dir}")
    if not candidate_manifests:
        raise ArtifactContractError(f"no manifests found in {candidate_dir}")

    errors: list[str] = []
    missing = sorted(reference_manifests.keys() - candidate_manifests.keys())
    unexpected = sorted(candidate_manifests.keys() - reference_manifests.keys())
    if missing:
        errors.append("candidate missing manifests: " + ", ".join(missing))
    if unexpected:
        errors.append("candidate has unexpected manifests: " + ", ".join(unexpected))

    for backend, directory, manifests in (
        ("reference", reference_dir, reference_manifests),
        ("candidate", candidate_dir, candidate_manifests),
    ):
        for name, manifest_path in sorted(manifests.items()):
            try:
                manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
                scene = manifest["scene"]
            except (OSError, json.JSONDecodeError, KeyError) as error:
                errors.append(f"{backend} manifest {name} is invalid: {error}")
                continue
            image_path = directory / f"{scene}.png"
            if not image_path.is_file():
                errors.append(
                    f"{backend} manifest {name} is missing image {image_path.name}"
                )

    if errors:
        raise ArtifactContractError("\n".join(errors))
    return [reference_manifests[name] for name in sorted(reference_manifests)]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("reference", type=Path, help="Vulkan artifact directory")
    parser.add_argument("candidate", type=Path, help="WebGPU artifact directory")
    args = parser.parse_args()

    try:
        manifests = validate_artifact_contract(args.reference, args.candidate)
    except ArtifactContractError as error:
        print("ARTIFACT CONTRACT FAILURE:", file=sys.stderr)
        print(error, file=sys.stderr)
        return 2
    failures: list[str] = []
    for manifest in manifests:
        failures.extend(compare_scene(args.reference, args.candidate, manifest))
    if failures:
        print("PIXEL PARITY FAILURE:", file=sys.stderr)
        print("\n".join(f"ERROR: {failure}" for failure in failures), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
