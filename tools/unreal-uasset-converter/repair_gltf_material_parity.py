"""Repair GLB core material flags from an Unreal/glTF parity report."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path


def _read_glb(path: Path) -> tuple[dict, bytes]:
    encoded = path.read_bytes()
    if len(encoded) < 20 or encoded[:4] != b"glTF":
        raise ValueError(f"{path.name}: not a GLB")
    json_length, json_kind = struct.unpack_from("<II", encoded, 12)
    if json_kind != 0x4E4F534A:
        raise ValueError(f"{path.name}: first chunk is not JSON")
    document = json.loads(encoded[20 : 20 + json_length].decode("utf-8").rstrip("\0 \t\r\n"))
    return document, encoded[20 + json_length :]


def _write_glb(path: Path, document: dict, remaining_chunks: bytes) -> None:
    payload = json.dumps(document, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    payload += b" " * (-len(payload) % 4)
    total = 12 + 8 + len(payload) + len(remaining_chunks)
    encoded = struct.pack("<III", 0x46546C67, 2, total)
    encoded += struct.pack("<II", len(payload), 0x4E4F534A) + payload + remaining_chunks
    path.write_bytes(encoded)


def repair(report: dict, manifest: dict, asset_root: Path) -> int:
    expected = {}
    for row in report["materials"]:
        expected[(row["outputFile"], row["exportedMaterial"])] = row
    changed = 0
    for artifact in manifest["artifacts"]:
        relative = artifact.get("path")
        if not relative or artifact.get("assetClass") != "StaticMesh":
            continue
        path = asset_root / relative
        document, chunks = _read_glb(path)
        dirty = False
        for material in document.get("materials", []):
            row = expected.get((relative, material.get("name", "")))
            if not row:
                continue
            alpha = row["alphaMode"]["ueExpected"]
            if material.get("alphaMode", "OPAQUE") != alpha:
                material["alphaMode"] = alpha
                dirty = True
            if alpha == "MASK":
                cutoff = row["alphaCutoff"]["ue"]
                if abs(float(material.get("alphaCutoff", 0.5)) - cutoff) >= 1e-9:
                    material["alphaCutoff"] = cutoff
                    dirty = True
            elif "alphaCutoff" in material:
                del material["alphaCutoff"]
                dirty = True
            double_sided = row["doubleSided"]["ue"]
            if bool(material.get("doubleSided", False)) != double_sided:
                if double_sided:
                    material["doubleSided"] = True
                else:
                    material.pop("doubleSided", None)
                dirty = True
        if dirty:
            _write_glb(path, document, chunks)
            artifact["bytes"] = path.stat().st_size
            artifact["sha256"] = hashlib.sha256(path.read_bytes()).hexdigest()
            changed += 1
    return changed


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    args = parser.parse_args()
    report = json.loads(args.report.read_text(encoding="utf-8"))
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    changed = repair(report, manifest, args.manifest.parent)
    args.manifest.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"repairedGlbFiles={changed}")


if __name__ == "__main__":
    import sys

    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
    from scripts import utf8_stdio

    utf8_stdio.enable_utf8_stdio()
    main()
