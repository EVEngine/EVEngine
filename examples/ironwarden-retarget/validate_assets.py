"""Offline GLB, rigid-weight and provenance checks. Python standard library only."""

import argparse
import hashlib
import json
import math
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parent


def glb(path):
    raw = path.read_bytes()
    assert struct.unpack_from("<4sII", raw) == (b"glTF", 2, len(raw)), path
    size, kind = struct.unpack_from("<I4s", raw, 12)
    assert kind == b"JSON"
    doc = json.loads(raw[20 : 20 + size])
    binary = raw[28 + size :]
    assert len(doc["buffers"]) == 1 and "uri" not in doc["buffers"][0]
    assert all("bufferView" in image and "uri" not in image for image in doc.get("images", []))
    return doc, binary


def values(doc, binary, index):
    a = doc["accessors"][index]
    v = doc["bufferViews"][a["bufferView"]]
    assert "sparse" not in a
    fmt = {5121: "B", 5123: "H", 5125: "I", 5126: "f"}[a["componentType"]]
    n = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}[a["type"]]
    stride = v.get("byteStride", struct.calcsize("<" + fmt * n))
    start = v.get("byteOffset", 0) + a.get("byteOffset", 0)
    for i in range(a["count"]):
        yield struct.unpack_from("<" + fmt * n, binary, start + i * stride)


def world_matrices(doc):
    parents = {c: i for i, n in enumerate(doc["nodes"]) for c in n.get("children", [])}
    result = {}

    def world(i):
        if i in result:
            return result[i]
        node = doc["nodes"][i]
        if "matrix" in node:
            m = node["matrix"]
            local = [[m[c * 4 + r] for c in range(4)] for r in range(4)]
        else:
            x, y, z, w = node.get("rotation", [0, 0, 0, 1])
            sx, sy, sz = node.get("scale", [1, 1, 1])
            px, py, pz = node.get("translation", [0, 0, 0])
            local = [
                [
                    (1 - 2 * y * y - 2 * z * z) * sx,
                    (2 * x * y - 2 * z * w) * sy,
                    (2 * x * z + 2 * y * w) * sz,
                    px,
                ],
                [
                    (2 * x * y + 2 * z * w) * sx,
                    (1 - 2 * x * x - 2 * z * z) * sy,
                    (2 * y * z - 2 * x * w) * sz,
                    py,
                ],
                [
                    (2 * x * z - 2 * y * w) * sx,
                    (2 * y * z + 2 * x * w) * sy,
                    (1 - 2 * x * x - 2 * y * y) * sz,
                    pz,
                ],
                [0, 0, 0, 1],
            ]
        if i in parents:
            parent = world(parents[i])
            local = multiply(parent, local)
        result[i] = local
        return local

    return [world(i) for i in range(len(doc["nodes"]))]


def multiply(a, b):
    return [[sum(a[r][k] * b[k][c] for k in range(4)) for c in range(4)] for r in range(4)]


def main(refresh_hashes=False):
    manifest_path = ROOT / "assets/sha256.json"
    if refresh_hashes:
        assets = ROOT / "assets"
        manifest = {
            p.relative_to(assets).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(assets.rglob("*"))
            if p.is_file() and p != manifest_path and "__pycache__" not in p.parts
        }
    else:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    for name, expected in manifest.items():
        assert hashlib.sha256((ROOT / "assets" / name).read_bytes()).hexdigest() == expected, name
    rig = json.loads((ROOT / "assets/ironwarden/rig.json").read_text(encoding="utf-8"))
    geometry = json.loads((ROOT / "assets/ironwarden/scene.json").read_text(encoding="utf-8"))
    assert geometry["schema"] == "ironwarden-geometry" and geometry["version"] == 2
    part_count = len(geometry["parts"])
    assert rig["schema"] == "ironwarden-rig" and rig["version"] == 1
    assert len(rig["bones"]) == 23 and len(rig["attachments"]) == part_count
    names = {r["target"] for r in rig["bones"]}
    assert len(names) == 23 and all(a["bone"] in names for a in rig["attachments"])
    assert {a["part"] for a in rig["attachments"]} == set(range(part_count))
    assert all(a["bone"] == geometry["parts"][a["part"]]["bone"] for a in rig["attachments"])
    doc, binary = glb(ROOT / "assets/ironwarden/ironwarden.glb")
    source, _ = glb(ROOT / "assets/quaternius/AnimationLibrary.glb")
    assert {r["source"] for r in rig["bones"]} <= {n.get("name") for n in source["nodes"]}
    actions = {
        "Idle_Loop",
        "Walk_Loop",
        "Jog_Fwd_Loop",
        "Sprint_Loop",
        "Crouch_Fwd_Loop",
        "Dance_Loop",
        "Punch_Jab",
        "Sword_Attack",
    }
    assert actions <= {a["name"] for a in source["animations"]}
    assert not doc.get("animations"), "Target motions must be retargeted at runtime"
    assert len(doc["meshes"]) == len(geometry["palette"]) and len(doc["skins"]) == 1
    skin = doc["skins"][0]
    assert {doc["nodes"][j]["name"] for j in skin["joints"]} == names
    assert doc["accessors"][skin["inverseBindMatrices"]]["count"] == 23
    source_world = world_matrices(source)
    target_world = world_matrices(doc)
    source_names = {n.get("name"): i for i, n in enumerate(source["nodes"])}
    bone_sources = {r["target"]: r["source"] for r in rig["bones"]}
    inverses = list(values(doc, binary, skin["inverseBindMatrices"]))
    for j, node in enumerate(skin["joints"]):
        target = target_world[node]
        original = source_world[source_names[bone_sources[doc["nodes"][node]["name"]]]]
        for c in range(3):
            length = math.sqrt(sum(original[r][c] ** 2 for r in range(3)))
            assert max(abs(target[r][c] - original[r][c] / length) for r in range(3)) < 1e-4
        inverse = [[inverses[j][c * 4 + r] for c in range(4)] for r in range(4)]
        identity = multiply(target, inverse)
        assert (
            max(abs(identity[r][c] - (1 if r == c else 0)) for r in range(4) for c in range(4))
            < 1e-4
        )
    count = 0
    for mesh in doc["meshes"]:
        for primitive in mesh["primitives"]:
            a = primitive["attributes"]
            assert {"POSITION", "NORMAL", "TEXCOORD_0", "JOINTS_0", "WEIGHTS_0"} <= a.keys()
            assert primitive.get("mode", 4) == 4
            positions = list(values(doc, binary, a["POSITION"]))
            joints = list(values(doc, binary, a["JOINTS_0"]))
            weights = list(values(doc, binary, a["WEIGHTS_0"]))
            assert len(positions) == len(joints) == len(weights)
            for p, j, w in zip(positions, joints, weights):
                assert all(math.isfinite(v) and abs(v) < 4 for v in p)
                assert abs(sum(w) - 1) < 1e-6 and sum(v > 0.0001 for v in w) == 1
                assert all(0 <= ji < 23 for ji, wi in zip(j, w) if wi > 0)
            count += len(positions)
    if refresh_hashes:
        manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(
        f"IRONWARDEN_ASSETS_PASS hashes={len(manifest)} joints=23 parts={part_count} vertices={count} clips=8 rigid_weights=1"
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--refresh-hashes",
        action="store_true",
        help="Accept rebuilt payload hashes after structural validation",
    )
    main(parser.parse_args().refresh_hashes)
