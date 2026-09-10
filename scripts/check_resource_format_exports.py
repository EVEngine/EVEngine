#!/usr/bin/env python3
"""Independently decode engine PNG/TGA exports with Pillow and FFmpeg.

A missing file or decoder is a failure. Run after resourceFormats.image.*Export
with EVENGINE_FORMAT_EXPORT_DIR set. Does not regenerate or modify the exports.
"""
import argparse
import json
from pathlib import Path
import struct
import subprocess

from PIL import Image


def verify(directory):
    expected = [(17 + x * 37, 23 + y * 71, 11 + x * 13 + y * 29, (x + y * 5) * 17)
                for y in range(3) for x in range(5)]
    results = []
    for name in ['rgba8.png', 'rgba16.png', 'rgba8.tga']:
        path = directory / name
        with Image.open(path) as image:
            if image.size != (5, 3):
                raise ValueError(f'{name}: wrong dimensions {image.size}')
            if image.mode != 'RGBA':
                raise ValueError(f'{name}: independent decoder lost alpha ({image.mode})')
            actual = [image.getpixel((x, y)) for y in range(3) for x in range(5)]
            error = max(abs(a - b) for actual_pixel, expected_pixel in zip(actual, expected)
                        for a, b in zip(actual_pixel, expected_pixel))
            if error > 1:
                raise ValueError(f'{name}: pixel error {error}')
        if name == 'rgba16.png':
            if path.read_bytes()[24] != 16:
                raise ValueError('rgba16.png: encoder did not preserve 16-bit depth')
            run = subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-i', str(path),
                                  '-f', 'rawvideo', '-pix_fmt', 'rgba64le', '-'], check=True, capture_output=True)
            if len(run.stdout) != 5 * 3 * 8:
                raise ValueError('rgba16.png: wrong decoded byte count')
            actual16 = struct.unpack('<60H', run.stdout)
            expected16 = [v * 257 + i % 5 + 1 for i, pixel in enumerate(expected) for v in pixel]
            error16 = max(abs(a - b) for a, b in zip(actual16, expected16))
            if error16 > 1:
                raise ValueError(f'rgba16.png: 16-bit pixel error {error16}')
        results.append({'file': name, 'status': 'passed', 'maxRgba8Error': error})
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    try:
        result = {'status': 'passed', 'exports': verify(args.directory)}
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        result = {'status': 'failed', 'error': str(error)}
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))
    return 0 if result['status'] == 'passed' else 1


if __name__ == '__main__':
    raise SystemExit(main())
