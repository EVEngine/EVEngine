"""Read the triangulated ASCII FBX subset exported by Unreal for sky domes.

This deliberately rejects other FBX layouts. Polygon-corner UVs and colors must
not be welded by position: UDS uses UV channel 3 and painted vertex red for wisps.
The returned data stays in Unreal local centimetres and texture coordinates.
"""

from __future__ import annotations

import math
import re


def _array(source: str, name: str, integer: bool = False) -> list:
    matches = list(re.finditer(r"\b" + re.escape(name) + r":\s*\*(\d+)\s*\{\s*a:([^}]+)\}", source))
    if len(matches) != 1:
        raise ValueError("expected one array: " + name)
    match = matches[0]
    count = int(match[1])
    if count > 4_000_000:
        raise ValueError("mesh array exceeds limit")
    values = [(int if integer else float)(x.strip()) for x in match[2].split(",")]
    if len(values) != count or not all(math.isfinite(v) for v in values):
        raise ValueError("invalid array: " + name)
    return values


def _layer(source: str, kind: str, index: int) -> str:
    matches = list(re.finditer(r"\bLayerElement" + kind + r":\s*" + str(index) + r"\s*\{", source))
    if len(matches) != 1:
        raise ValueError("missing or duplicate mesh layer")
    begin = matches[0].end()
    depth = 1
    for end in range(begin, len(source)):
        depth += (source[end] == "{") - (source[end] == "}")
        if depth == 0:
            result = source[begin:end]
            if not re.search(r'MappingInformationType:\s*"ByPolygonVertex"', result) or not re.search(
                r'ReferenceInformationType:\s*"IndexToDirect"', result
            ):
                raise ValueError("unsupported mesh layer mapping")
            return result
    raise ValueError("unterminated mesh layer")


def decode_sky_mesh(source: bytes, uv_channel: int = 3) -> dict:
    """Validate all corners before returning owned mesh data; never mutate input."""
    if type(uv_channel) is not int or not 0 <= uv_channel <= 7:
        raise ValueError("invalid UV channel")
    if len(source) > 64 * 1024 * 1024 or source.startswith(b"Kaydara FBX Binary"):
        raise ValueError("expected ASCII sky mesh under 64 MiB")
    text = source.decode("utf-8-sig")
    if len(re.findall(r'\bGeometry:\s*\d+,\s*"[^"]*",\s*"Mesh"', text)) != 1:
        raise ValueError("expected one mesh geometry")
    positions = _array(text, "Vertices")
    corners = _array(text, "PolygonVertexIndex", True)
    if not positions or len(positions) % 3 or not corners or len(corners) % 3:
        raise ValueError("invalid mesh dimensions")
    if any((value < 0) != (i % 3 == 2) for i, value in enumerate(corners)):
        raise ValueError("sky mesh must be triangulated")
    uv_layer = _layer(text, "UV", uv_channel)
    color_layer = _layer(text, "Color", 0)
    uvs, uv_indices = _array(uv_layer, "UV"), _array(uv_layer, "UVIndex", True)
    colors, color_indices = _array(color_layer, "Colors"), _array(color_layer, "ColorIndex", True)
    if len(uvs) % 2 or len(colors) % 4 or len(uv_indices) != len(corners) or len(color_indices) != len(corners):
        raise ValueError("invalid corner attribute dimensions")
    if any(not 0 <= c <= 1 for c in colors):
        raise ValueError("vertex colors must be linear UNORM values")
    vertices = []
    for corner, uv_index, color_index in zip(corners, uv_indices, color_indices):
        position_index = corner if corner >= 0 else -corner - 1
        if not (0 <= position_index < len(positions) // 3 and
                0 <= uv_index < len(uvs) // 2 and 0 <= color_index < len(colors) // 4):
            raise ValueError("mesh attribute index out of range")
        x, y, z = positions[position_index * 3:position_index * 3 + 3]
        u, v = uvs[uv_index * 2:uv_index * 2 + 2]
        # Undo UE FbxMainExport: position (X,-Y,Z), UV (U,1-V).
        vertices.append([x, -y, z, u, 1 - v, *colors[color_index * 4:color_index * 4 + 4]])
    return {"schema": "eve.sky-mesh/1", "space": "unreal-local-centimetres",
            "uvChannel": uv_channel, "layout": ["x", "y", "z", "u", "v", "r", "g", "b", "a"],
            "topology": "triangle-list", "vertices": vertices}


def main() -> None:
    """Publish a decoded source mesh only after complete validation."""
    import argparse
    import hashlib
    import json
    from pathlib import Path

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--uv-channel", type=int, default=3)
    args = parser.parse_args()
    if args.source.stat().st_size > 64 * 1024 * 1024:
        raise ValueError("sky mesh exceeds 64 MiB")
    source = args.source.read_bytes()
    mesh = decode_sky_mesh(source, args.uv_channel)
    mesh["sourceSha256"] = hashlib.sha256(source).hexdigest()
    encoded = json.dumps(mesh, separators=(",", ":"), allow_nan=False) + "\n"
    # Exclusive creation prevents accidentally replacing source or prior evidence.
    with args.output.open("x", encoding="utf-8") as output:
        output.write(encoded)


if __name__ == "__main__":
    main()
