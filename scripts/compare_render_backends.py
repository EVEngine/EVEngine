#!/usr/bin/env python3
"""Compare Vulkan and WebGPU render-parity artifact directories."""

from __future__ import annotations

import argparse
import json
import math
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from PIL import Image

try:
    from scripts.ci_result import CIResult, FailureKind
except ModuleNotFoundError:  # Direct execution sets sys.path to scripts/.
    from ci_result import CIResult, FailureKind


class ArtifactContractError(ValueError):
    """Raised when render-parity producers emitted incomplete artifact sets."""


@dataclass(frozen=True)
class RenderContract:
    """Versioned, backend-neutral comparison semantics for one rendered scene."""

    color_space: str
    alpha_coverage_delta_max: float
    mean_rgb_error_max: float
    p99_rgb_error_max: float


def parse_manifest(path: Path) -> dict[str, Any]:
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ArtifactContractError(f"manifest {path.name} is invalid: {error}") from error
    for field in ("schema", "version", "scene", "backend", "contract"):
        if field not in manifest:
            raise ArtifactContractError(
                f"manifest {path.name} missing required field: {field}"
            )
    if manifest["schema"] != "evengine.render-parity" or manifest["version"] != 1:
        raise ArtifactContractError(
            f"manifest {path.name} has unsupported schema/version: "
            f"{manifest['schema']!r}/{manifest['version']!r}"
        )
    return manifest


def parse_render_contract(manifest: dict[str, Any], path: Path) -> RenderContract:
    payload = manifest["contract"]
    required = (
        "color_space",
        "alpha_coverage_delta_max",
        "mean_rgb_error_max",
        "p99_rgb_error_max",
    )
    if not isinstance(payload, dict):
        raise ArtifactContractError(f"manifest {path.name} contract must be an object")
    missing = [field for field in required if field not in payload]
    if missing:
        raise ArtifactContractError(
            f"manifest {path.name} contract missing: {', '.join(missing)}"
        )
    if payload["color_space"] != "srgb-linearized":
        raise ArtifactContractError(
            f"manifest {path.name} has unsupported color space: {payload['color_space']!r}"
        )
    limits = [float(payload[field]) for field in required[1:]]
    if any(not math.isfinite(value) or value < 0.0 for value in limits):
        raise ArtifactContractError(f"manifest {path.name} has invalid tolerance")
    return RenderContract(str(payload["color_space"]), *limits)


def srgb_to_linear(value: int) -> float:
    channel = value / 255.0
    if channel <= 0.04045:
        return channel / 12.92
    return math.pow((channel + 0.055) / 1.055, 2.4)


def compare_scene(
    reference_dir: Path, candidate_dir: Path, manifest_path: Path
) -> list[str]:
    manifest = parse_manifest(manifest_path)
    contract = parse_render_contract(manifest, manifest_path)
    scene = manifest["scene"]
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

    if alpha_ratio > contract.alpha_coverage_delta_max:
        failures.append(
            f"{scene}: alpha coverage delta {alpha_ratio:.4%} > "
            f"{contract.alpha_coverage_delta_max:.4%}"
        )
    if mean_error > contract.mean_rgb_error_max:
        failures.append(
            f"{scene}: mean linear RGB error {mean_error:.6f} > "
            f"{contract.mean_rgb_error_max:.6f}"
        )
    if percentile_99 > contract.p99_rgb_error_max:
        failures.append(
            f"{scene}: p99 linear RGB error {percentile_99:.6f} > "
            f"{contract.p99_rgb_error_max:.6f}"
        )
    print(
        f"{scene}: alpha={alpha_ratio:.4%} mean={mean_error:.6f} "
        f"p99={percentile_99:.6f} color_space={contract.color_space}"
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
                manifest = parse_manifest(manifest_path)
                parse_render_contract(manifest, manifest_path)
                scene = manifest["scene"]
            except (ArtifactContractError, KeyError, TypeError, ValueError) as error:
                errors.append(f"{backend} manifest {name} is invalid: {error}")
                continue
            image_path = directory / f"{scene}.png"
            if not image_path.is_file():
                errors.append(
                    f"{backend} manifest {name} is missing image {image_path.name}"
                )

    for name in sorted(reference_manifests.keys() & candidate_manifests.keys()):
        try:
            reference_manifest = parse_manifest(reference_manifests[name])
            candidate_manifest = parse_manifest(candidate_manifests[name])
            if reference_manifest["scene"] != candidate_manifest["scene"]:
                errors.append(f"manifest {name} scene differs between backends")
            if reference_manifest["contract"] != candidate_manifest["contract"]:
                errors.append(f"manifest {name} contract differs between backends")
        except ArtifactContractError:
            # The backend-specific validation above already reports malformed input.
            pass

    if errors:
        raise ArtifactContractError("\n".join(errors))
    return [reference_manifests[name] for name in sorted(reference_manifests)]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("reference", type=Path, help="Vulkan artifact directory")
    parser.add_argument("candidate", type=Path, help="WebGPU artifact directory")
    parser.add_argument("--result-json", type=Path, help="write a versioned CI result")
    args = parser.parse_args()

    try:
        manifests = validate_artifact_contract(args.reference, args.candidate)
    except ArtifactContractError as error:
        print("ARTIFACT CONTRACT FAILURE:", file=sys.stderr)
        print(error, file=sys.stderr)
        if args.result_json:
            CIResult.failure(
                FailureKind.CONTRACT,
                "RENDER_ARTIFACT_CONTRACT_INVALID",
                "render-parity",
                str(error),
            ).write(args.result_json)
        return 2
    failures: list[str] = []
    for manifest in manifests:
        failures.extend(compare_scene(args.reference, args.candidate, manifest))
    if failures:
        print("PIXEL PARITY FAILURE:", file=sys.stderr)
        print("\n".join(f"ERROR: {failure}" for failure in failures), file=sys.stderr)
        if args.result_json:
            CIResult.failure(
                FailureKind.PARITY,
                "RENDER_PARITY_EXCEEDED",
                "render-parity",
                "one or more scenes exceeded their render contract",
                {"failures": failures},
            ).write(args.result_json)
        return 1
    if args.result_json:
        CIResult.success("render-parity").write(args.result_json)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
