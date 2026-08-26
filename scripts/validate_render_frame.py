#!/usr/bin/env python3
"""Reject missing, blank, or effectively single-color render captures."""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageStat


def validate(path: Path) -> tuple[float, float]:
    with Image.open(path) as source:
        image = source.convert("RGB")
        if image.width < 64 or image.height < 64:
            raise ValueError(f"capture is too small: {image.width}x{image.height}")
        stat = ImageStat.Stat(image)
        mean = sum(stat.mean) / 3.0
        deviation = sum(stat.stddev) / 3.0
        extrema_span = max(high - low for low, high in stat.extrema)
    if mean < 2.0:
        raise ValueError(f"capture is black: mean={mean:.3f}")
    if mean > 253.0:
        raise ValueError(f"capture is white: mean={mean:.3f}")
    if deviation < 3.0 or extrema_span < 16:
        raise ValueError(
            f"capture is effectively flat: stddev={deviation:.3f}, span={extrema_span}"
        )
    return mean, deviation


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("image", type=Path)
    args = parser.parse_args()
    mean, deviation = validate(args.image)
    print(f"render frame OK: mean={mean:.3f}, stddev={deviation:.3f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
