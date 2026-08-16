#!/usr/bin/env python3
"""Batch-screening client example for the vision pre-filter service.

Shows the standard request/response protocol, including how to submit a base64
(or data-URL) render snapshot plus a compact geometry text snapshot.
"""

from __future__ import annotations

import argparse
import base64
import io
import json
import sys
from pathlib import Path
from typing import Optional

import requests


def encode_image(path: str) -> str:
    data = Path(path).read_bytes()
    return "data:image/png;base64," + base64.b64encode(data).decode()


def main() -> int:
    ap = argparse.ArgumentParser(description="Submit scenes to the vision pre-filter.")
    ap.add_argument("--url", default="http://127.0.0.1:8531/v1/prefilter/batch")
    ap.add_argument("--image", required=True, help="path to a render snapshot PNG/JPEG")
    ap.add_argument("--geometry", default=None, help="optional compact geometry text file")
    ap.add_argument("--id", default="scene-001")
    args = ap.parse_args()

    geometry: Optional[str] = None
    if args.geometry:
        geometry = Path(args.geometry).read_text(encoding="utf-8")

    payload = {
        "protocol_version": "1.0",
        "scenes": [
            {
                "id": args.id,
                "image": encode_image(args.image),
                "geometry": geometry,
            }
        ],
    }

    r = requests.post(args.url, json=payload, timeout=120)
    r.raise_for_status()
    print(json.dumps(r.json(), ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
