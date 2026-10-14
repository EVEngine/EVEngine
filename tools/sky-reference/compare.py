#!/usr/bin/env python3
"""Compare registered, equally sized SDR reference frames without alignment tricks."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import sys
from pathlib import Path


def metrics(reference, candidate, tolerance: int = 3) -> dict:
    """RGB error and population-covariance SSIM (11x11 Gaussian, sigma 1.5)."""
    import numpy as np

    a, b = np.asarray(reference), np.asarray(candidate)
    if a.shape != b.shape or a.ndim != 3 or a.shape[2] != 3 or min(a.shape[:2]) < 11:
        raise ValueError("need identically sized RGB images/ROIs of at least 11x11 pixels")
    if a.dtype != np.uint8 or b.dtype != np.uint8 or not 0 <= tolerance <= 255:
        raise ValueError("inputs must be uint8 RGB and tolerance must be in [0,255]")
    error = np.abs(a.astype(np.int16) - b.astype(np.int16))
    pixel_error = error.max(axis=2)
    af, bf = a.astype(np.float64), b.astype(np.float64)
    weights = np.exp(-0.5 * (np.arange(-5, 6, dtype=np.float64) / 1.5) ** 2)
    weights /= weights.sum()

    def gaussian_valid(values):
        horizontal = sum(weights[k] * values[:, k:k + values.shape[1] - 10] for k in range(11))
        return sum(weights[k] * horizontal[k:k + horizontal.shape[0] - 10] for k in range(11))

    ma, mb = gaussian_valid(af), gaussian_valid(bf)
    va = np.maximum(gaussian_valid(af * af) - ma * ma, 0)
    vb = np.maximum(gaussian_valid(bf * bf) - mb * mb, 0)
    covariance = gaussian_valid(af * bf) - ma * mb
    c1, c2 = (0.01 * 255) ** 2, (0.03 * 255) ** 2
    ssim_map = ((2 * ma * mb + c1) * (2 * covariance + c2)) / ((ma * ma + mb * mb + c1) * (va + vb + c2))
    mse = float(np.mean((af - bf) ** 2))
    return {"width": a.shape[1], "height": a.shape[0], "pixels": int(pixel_error.size),
            "tolerance8bit": tolerance, "withinToleranceFraction": float(np.mean(pixel_error <= tolerance)),
            "exactPixelFraction": float(np.mean(pixel_error == 0)),
            "mae8bit": float(error.mean()), "maximumChannelError8bit": int(error.max()),
            "p99PixelMaximumError8bit": float(np.percentile(pixel_error, 99)),
            "psnrDb": 10 * math.log10(255 ** 2 / mse) if mse else None,
            "ssim": float(ssim_map.mean()),
            "ssimDefinition": "RGB mean; Gaussian 11x11 sigma=1.5; population covariance; valid window; data_range=255"}


def compare_frames(reference: Path, candidate: Path, output: Path, tolerance: int,
                   minimum_fraction: float, minimum_ssim: float, regions: dict) -> dict:
    import numpy as np
    from PIL import Image

    if output.exists():
        raise ValueError("output directory must not already exist")
    if not 0 <= minimum_fraction <= 1 or not -1 <= minimum_ssim <= 1:
        raise ValueError("invalid comparison thresholds")
    with Image.open(reference) as source:
        if source.mode not in ("RGB", "RGBA"):
            raise ValueError("reference must be 8-bit RGB or RGBA; implicit quantization is forbidden")
        reference_profile = source.info.get("icc_profile")
        a = np.array(source.convert("RGB"))
    with Image.open(candidate) as source:
        if source.mode not in ("RGB", "RGBA"):
            raise ValueError("candidate must be 8-bit RGB or RGBA; implicit quantization is forbidden")
        if source.info.get("icc_profile") != reference_profile:
            raise ValueError("embedded color profiles differ; convert explicitly before comparison")
        b = np.array(source.convert("RGB"))
    result = {"schema": "eve.sky-image-comparison/1", "status": "pass",
              "referenceSha256": hashlib.sha256(reference.read_bytes()).hexdigest(),
              "candidateSha256": hashlib.sha256(candidate.read_bytes()).hexdigest(),
              "minimumWithinToleranceFraction": minimum_fraction, "minimumSsim": minimum_ssim,
              "colorContract": "registered 8-bit RGB SDR in the same declared output color space; no resize, crop alignment, exposure fit or alpha comparison",
              "regions": {"full": metrics(a, b, tolerance)}}
    if not isinstance(regions, dict) or "full" in regions:
        raise ValueError("regions must be a named object without reserved key 'full'")
    for name, box in regions.items():
        if not isinstance(name, str) or not name or not isinstance(box, list) or len(box) != 4 or any(type(v) is not int for v in box):
            raise ValueError("each region needs a name and integer [x,y,width,height]")
        x, y, width, height = box
        if min(x, y) < 0 or min(width, height) < 11 or x + width > a.shape[1] or y + height > a.shape[0]:
            raise ValueError("invalid or out-of-bounds region: " + name)
        result["regions"][name] = metrics(a[y:y+height, x:x+width], b[y:y+height, x:x+width], tolerance)
        result["regions"][name]["box"] = box
    for region in result["regions"].values():
        region["status"] = "pass" if region["withinToleranceFraction"] >= minimum_fraction and region["ssim"] >= minimum_ssim else "fail"
        if region["status"] == "fail":
            result["status"] = "fail"
    output.mkdir(parents=True)
    error = np.abs(a.astype(np.int16) - b.astype(np.int16)).max(axis=2).astype(np.uint8)
    Image.fromarray(error).save(output / "maximum-channel-error.png")
    Image.fromarray(np.where(error <= tolerance, 0, 255).astype(np.uint8)).save(output / "outside-tolerance.png")
    (output / "comparison.json").write_text(json.dumps(result, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--tolerance", type=int, default=3)
    parser.add_argument("--minimum-fraction", type=float, default=0.99)
    parser.add_argument("--minimum-ssim", type=float, default=0.99)
    parser.add_argument("--regions", type=Path, help="JSON object of named [x,y,width,height] ROIs")
    args = parser.parse_args()
    try:
        regions = json.loads(args.regions.read_text(encoding="utf-8")) if args.regions else {}
        result = compare_frames(args.reference, args.candidate, args.output, args.tolerance,
                                args.minimum_fraction, args.minimum_ssim, regions)
    except (OSError, ValueError, ImportError) as error:
        print(f"sky-reference compare: {error}", file=sys.stderr)
        return 2
    print(json.dumps({"status": result["status"], "regions": result["regions"]}, indent=2))
    return 0 if result["status"] == "pass" else 1


if __name__ == "__main__":
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8")
    raise SystemExit(main())
