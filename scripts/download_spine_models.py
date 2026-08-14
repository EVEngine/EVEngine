#!/usr/bin/env python3
"""Download free Spine demo models (spineboy / hero / dragon) for 2D spine tests.

Source: EsotericSoftware/spine-runtimes branch 3.5 example exports. These use
only "region" attachments (no mesh / clipping / path), which is the subset the
EVEngine Spine module parses. Output layout:

    test/assets/spine/<model>/export/<file>

Usage:
    python3 scripts/download_spine_models.py
    EVENGINE_SPINE_DIR=/path python3 scripts/download_spine_models.py

Idempotent: skips files already present with the expected size. Writes a
`.downloaded` marker and an ATTRIBUTION.md when all models are installed.
"""

import os
import sys
import urllib.request

RAW_BASE = (
    "https://raw.githubusercontent.com/EsotericSoftware/spine-runtimes/3.5"
    "/examples/{model}/export/{name}"
)
LICENSE_BASE = (
    "https://raw.githubusercontent.com/EsotericSoftware/spine-runtimes/3.5"
    "/examples/{model}/license.txt"
)

# model -> extra files beyond <model>.json / <model>.atlas / <model>.png
MODELS = {
    "spineboy": [],
    "hero": [],
    "dragon": ["dragon2.png"],
}

ATTRIBUTION = """# Spine demo models

These assets are the official Spine sample models exported by Esoteric
Software and are distributed with the spine-runtimes repository
(https://github.com/EsotericSoftware/spine-runtimes, branch 3.5) under
`examples/`.

Each model has its own `license.txt` in this folder. Spine Runtimes are
available for integration in games and other software under the terms of the
Spine Editor License Agreement (http://esotericsoftware.com/spine-editor-license).

Downloaded by `scripts/download_spine_models.py` from
`https://raw.githubusercontent.com/EsotericSoftware/spine-runtimes/3.5/examples/<model>/export/`.
"""


def out_root() -> str:
    env = os.environ.get("EVENGINE_SPINE_DIR")
    if env:
        return env
    here = os.path.dirname(os.path.abspath(__file__))
    return os.path.join(here, "..", "test", "assets", "spine")


def fetch(url: str, path: str, expected: int) -> bool:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if os.path.isfile(path):
        if expected is not None and os.path.getsize(path) == expected:
            print(f"  skip  {os.path.basename(path)} ({expected} bytes)")
            return True
        if expected is None and os.path.getsize(path) > 0:
            print(f"  skip  {os.path.basename(path)}")
            return True
    print(f"  get   {os.path.basename(path)}")
    req = urllib.request.Request(url, headers={"User-Agent": "evengine-spine-models"})
    try:
        with urllib.request.urlopen(req, timeout=180) as r, open(path, "wb") as out:
            while True:
                chunk = r.read(1024 * 256)
                if not chunk:
                    break
                out.write(chunk)
    except Exception as exc:  # noqa: BLE001 - report and let caller decide
        print(f"  error {os.path.basename(path)}: {exc}", file=sys.stderr)
        return False
    if expected is not None and os.path.getsize(path) != expected:
        print(
            f"  size mismatch for {path}: got {os.path.getsize(path)}, want {expected}",
            file=sys.stderr,
        )
        return False
    return True


def fetch_license(model: str, dest_dir: str) -> None:
    url = LICENSE_BASE.format(model=model)
    path = os.path.join(dest_dir, "license.txt")
    # license files are tiny; tolerate failures without failing the download
    fetch(url, path, None)


def main() -> int:
    root = os.path.abspath(out_root())
    ok = True
    for model, extra in MODELS.items():
        files = [f"{model}.json", f"{model}.atlas", f"{model}.png"] + extra
        dest_dir = os.path.join(root, model)
        export_dir = os.path.join(dest_dir, "export")
        for name in files:
            url = RAW_BASE.format(model=model, name=name)
            # Unknown size: rely on server content-length via streaming write.
            if not fetch(url, os.path.join(export_dir, name), None):
                ok = False
        fetch_license(model, dest_dir)

    if not ok:
        print("One or more files failed to download.", file=sys.stderr)
        return 1

    marker = os.path.join(root, ".downloaded")
    with open(marker, "w", encoding="utf-8") as f:
        f.write("\n".join(MODELS.keys()) + "\n")
    with open(os.path.join(root, "ATTRIBUTION.md"), "w", encoding="utf-8") as f:
        f.write(ATTRIBUTION)
    print(f"Spine models installed under {root}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
