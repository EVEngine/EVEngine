"""Adapt the downloaded pixiv mesh: add facial joints and bind FK clothing weights.

No character geometry or textures are generated. NumPy is required only offline.
The source file remains unchanged. Run from any working directory.
"""
import hashlib
import json
from pathlib import Path
import struct

import numpy as np

ROOT = Path(__file__).resolve().parent
ASSETS = ROOT / "assets/pixiv"
# id, label, world bind pivot, ellipsoid radii (metres), opposite control
CONTROLS = [
    ("chin", "Chin", [0, 1.360, .049], [.045, .030, .070], ""),
    ("jawL", "Left jaw", [.075, 1.385, .018], [.043, .045, .080], "jawR"),
    ("jawR", "Right jaw", [-.075, 1.385, .018], [.043, .045, .080], "jawL"),
    ("cheekL", "Left cheek", [.069, 1.415, .055], [.040, .036, .045], "cheekR"),
    ("cheekR", "Right cheek", [-.069, 1.415, .055], [.040, .036, .045], "cheekL"),
    ("nose", "Nose tip", [0, 1.414, .087], [.021, .026, .035], ""),
    ("bridge", "Nose bridge", [0, 1.450, .068], [.019, .033, .035], ""),
    ("mouth", "Mouth", [0, 1.388, .070], [.035, .020, .045], ""),
    ("eyeL", "Left eye", [.042, 1.444, .058], [.034, .025, .042], "eyeR"),
    ("eyeR", "Right eye", [-.042, 1.444, .058], [.034, .025, .042], "eyeL"),
    ("browL", "Left eyebrow", [.042, 1.480, .067], [.038, .018, .035], "browR"),
    ("browR", "Right eyebrow", [-.042, 1.480, .067], [.038, .018, .035], "browL"),
    ("templeL", "Left temple", [.096, 1.470, .003], [.030, .052, .070], "templeR"),
    ("templeR", "Right temple", [-.096, 1.470, .003], [.030, .052, .070], "templeL"),
    ("forehead", "Forehead", [0, 1.522, .056], [.080, .041, .060], ""),
]
DTYPES = {5121: "u1", 5123: "<u2", 5125: "<u4", 5126: "<f4"}
WIDTHS = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


def main():
    source = (ASSETS / "female.vrm").read_bytes()
    manifest = json.loads((ASSETS / "manifest.json").read_text(encoding="utf-8"))
    assert hashlib.sha256(source).hexdigest() == manifest["sha256"]
    size = struct.unpack_from("<I", source, 12)[0]
    doc = json.loads(source[20:20 + size])
    data = bytearray(source[28 + size:])

    def read(index):
        a = doc["accessors"][index]
        v = doc["bufferViews"][a["bufferView"]]
        dtype = np.dtype(DTYPES[a["componentType"]])
        width = WIDTHS[a["type"]]
        stride = v.get("byteStride", width * dtype.itemsize)
        offset = v.get("byteOffset", 0) + a.get("byteOffset", 0)
        return np.ndarray((a["count"], width), dtype=dtype, buffer=data,
                          offset=offset, strides=(stride, dtype.itemsize)).copy()

    def append(values, kind, component=5126):
        values = np.asarray(values, dtype=DTYPES[component])
        data.extend(b"\0" * (-len(data) % 4))
        view = len(doc["bufferViews"])
        raw = values.tobytes()
        doc["bufferViews"].append({"buffer": 0, "byteOffset": len(data), "byteLength": len(raw)})
        data.extend(raw)
        index = len(doc["accessors"])
        doc["accessors"].append({"bufferView": view, "componentType": component,
                                 "count": len(values), "type": kind})
        return index

    parents = {c: i for i, node in enumerate(doc["nodes"]) for c in node.get("children", [])}

    def local(node):
        if "matrix" in node:
            return np.array(node["matrix"]).reshape(4, 4).T
        x, y, z, w = node.get("rotation", [0, 0, 0, 1])
        m = np.eye(4)
        m[:3, :3] = [[1-2*y*y-2*z*z, 2*x*y-2*z*w, 2*x*z+2*y*w],
                     [2*x*y+2*z*w, 1-2*x*x-2*z*z, 2*y*z-2*x*w],
                     [2*x*z-2*y*w, 2*y*z+2*x*w, 1-2*x*x-2*y*y]]
        m[:3, :3] *= node.get("scale", [1, 1, 1])
        m[:3, 3] = node.get("translation", [0, 0, 0])
        return m

    worlds = {}

    def world(i):
        if i not in worlds:
            worlds[i] = (world(parents[i]) if i in parents else np.eye(4)) @ local(doc["nodes"][i])
        return worlds[i]

    vrm = doc["extensions"]["VRMC_vrm"]
    humanoid = {name: v["node"] for name, v in vrm["humanoid"]["humanBones"].items()}
    head = humanoid["head"]
    new_nodes = []
    controls = []
    for key, label, point, radii, mirror in CONTROLS:
        index = len(doc["nodes"])
        relative = np.linalg.inv(world(head)) @ np.array([*point, 1.0])
        doc["nodes"].append({"name": "Atelier_" + key, "translation": relative[:3].tolist()})
        doc["nodes"][head].setdefault("children", []).append(index)
        parents[index] = head
        new_nodes.append(index)
        controls.append({"id": key, "label": label, "bone": "Atelier_" + key,
                         "point": point, "mirror": mirror})

    # Convert constraint helper skin influences to ordinary FK joints at the same bind pose.
    # Otherwise the authored sleeve aim bones stay in T-pose when the arm rotates.
    def fk_target(node):
        name = doc["nodes"][node].get("name", "")
        if not name.startswith(("J_Aim_", "J_Roll_")):
            # Secondary sleeve/skirt nodes inherit a constrained helper in the
            # source. Rebind their influences too, otherwise sleeves flare out.
            ancestor = parents.get(node)
            while ancestor is not None:
                if doc["nodes"][ancestor].get("name", "").startswith(("J_Aim_", "J_Roll_")):
                    return fk_target(ancestor)
                ancestor = parents.get(ancestor)
            return node
        side = "left" if "_L_" in name else "right" if "_R_" in name else ""
        if not side:
            return node
        suffix = name.rsplit("_", 1)[-1]
        part = {"UpperLeg": "UpperLeg", "LowerLeg": "LowerLeg", "Knee": "LowerLeg",
                "UpperArm": "UpperArm", "TopsUpperArm": "UpperArm", "Shoulder": "UpperArm",
                "LowerArm": "LowerArm", "Elbow": "LowerArm", "Hand": "Hand"}.get(suffix)
        return humanoid.get(side + part, node) if part else node

    counts = {c[0]: 0 for c in CONTROLS}
    bind_error = 0.0
    for ni, node in enumerate(doc["nodes"][:len(doc["nodes"]) - len(CONTROLS)]):
        if "skin" not in node:
            continue
        skin = doc["skins"][node["skin"]]
        old_joints = list(skin["joints"])
        joints = list(dict.fromkeys([fk_target(n) for n in old_joints] + new_nodes))
        lookup = {n: i for i, n in enumerate(joints)}
        skin["joints"] = joints
        inverse = np.array([np.linalg.inv(world(n)).T.flatten() for n in joints])
        skin["inverseBindMatrices"] = append(inverse, "MAT4")
        for primitive in doc["meshes"][node["mesh"]]["primitives"]:
            a = primitive["attributes"]
            pos = read(a["POSITION"])
            old_indices, old_weights = read(a["JOINTS_0"]), read(a["WEIGHTS_0"])
            mapped = np.array([[lookup[fk_target(old_joints[int(i)])] for i in row] for row in old_indices])
            mat_name = doc["materials"][primitive["material"]]["name"]
            facial = node["mesh"] == 1
            influences = np.zeros((len(pos), len(CONTROLS)))
            if facial:
                for ci, (key, _, point, radii, _) in enumerate(CONTROLS):
                    d = (pos - point) / radii
                    influences[:, ci] = np.exp(-2.0 * np.sum(d*d, axis=1))
                if any(t in mat_name for t in ["EyeIris", "EyeWhite", "EyeHighlight", "FaceEyeline"]):
                    influences[:] = 0
                    influences[np.arange(len(pos)), np.where(pos[:, 0] >= 0, 8, 9)] = .98
                elif "FaceBrow" in mat_name:
                    influences[:] = 0
                    influences[np.arange(len(pos)), np.where(pos[:, 0] >= 0, 10, 11)] = .98
                elif "FaceMouth" in mat_name:
                    influences[:] = 0
                    influences[:, 7] = .98
            out_j, out_w = np.zeros((len(pos), 4), dtype=np.uint16), np.zeros((len(pos), 4), dtype=np.float32)
            for vi in range(len(pos)):
                top = np.argsort(influences[vi])[-2:]
                face_weights = influences[vi, top]
                strength = min(.98, float(np.sum(face_weights)))
                merged = {}
                for j, w in zip(mapped[vi], old_weights[vi]):
                    merged[int(j)] = merged.get(int(j), 0) + float(w) * (1-strength)
                if strength > .001:
                    for ci, w in zip(top, face_weights):
                        if w > .001:
                            joint = lookup[new_nodes[int(ci)]]
                            merged[joint] = float(w) * strength / float(np.sum(face_weights))
                            counts[CONTROLS[int(ci)][0]] += 1
                chosen = sorted(merged.items(), key=lambda v: v[1], reverse=True)[:4]
                total = sum(v for _, v in chosen)
                assert total > 0
                for k, (j, w) in enumerate(chosen):
                    out_j[vi, k], out_w[vi, k] = j, w/total
            assert np.isfinite(out_w).all() and np.allclose(out_w.sum(axis=1), 1, atol=1e-6)
            # Every new binding must reconstruct exactly the unchanged source position at rest.
            matrices = [world(n) @ np.linalg.inv(world(n)) for n in joints]
            homogeneous = np.c_[pos, np.ones(len(pos))]
            reconstructed = np.zeros_like(homogeneous)
            for k in range(4):
                transformed = np.einsum('nij,nj->ni', np.array(matrices)[out_j[:, k]], homogeneous)
                reconstructed += transformed * out_w[:, k:k+1]
            bind_error = max(bind_error, float(np.max(np.abs(reconstructed[:, :3]-pos))))
            a["JOINTS_0"] = append(out_j, "VEC4", 5123)
            a["WEIGHTS_0"] = append(out_w, "VEC4")
    assert all(v > 0 for v in counts.values()), counts
    assert bind_error < 1e-6, bind_error
    # Preserve authored expressions in a small script descriptor; render with YSA, not MToon.
    expressions = {}
    for name, expression in vrm["expressions"]["preset"].items():
        binds = []
        for bind in expression.get("morphTargetBinds", []):
            mesh = doc["meshes"][doc["nodes"][bind["node"]]["mesh"]]
            binds.append([mesh["extras"]["targetNames"][bind["index"]], bind["weight"]])
        expressions[name] = {"binary": expression.get("isBinary", False), "binds": binds}
    # Standard glTF: unsupported node constraints are explicitly replaced by the FK rebind above.
    doc.pop("extensions", None)
    doc["extensionsRequired"] = []
    doc["extensionsUsed"] = [e for e in doc.get("extensionsUsed", []) if not e.startswith("VRMC_")]
    for node in doc["nodes"]:
        node.pop("extensions", None)
    for mat in doc["materials"]:
        mat.pop("extensions", None)
    doc["asset"]["generator"] = "EVEngine Character Atelier FK/facial rig adapter v1"
    doc["asset"]["copyright"] = "(c) 2022 pixiv Inc. Adapted facial skin bindings; VRM Public License 1.0."
    doc["buffers"][0]["byteLength"] = len(data)
    raw = json.dumps(doc, separators=(",", ":")).encode()
    raw += b" " * (-len(raw) % 4)
    data.extend(b"\0" * (-len(data) % 4))
    glb = struct.pack("<III", 0x46546c67, 2, 28+len(raw)+len(data))
    glb += struct.pack("<II", len(raw), 0x4e4f534a)+raw
    glb += struct.pack("<II", len(data), 0x004e4942)+data
    (ASSETS / "female-rig.glb").write_bytes(glb)
    material_info = {}
    for mat in doc["materials"]:
        pbr = mat["pbrMetallicRoughness"]
        texture = doc["textures"][pbr["baseColorTexture"]["index"]]
        image = doc["images"][texture["source"]]
        view = doc["bufferViews"][image["bufferView"]]
        start = view.get("byteOffset", 0)
        raw_image = bytes(data[start:start+view["byteLength"]])
        assert raw_image[:8] == b"\x89PNG\r\n\x1a\n"
        width, height = struct.unpack_from(">II", raw_image, 16)
        material_info[mat["name"]] = {"width": width, "height": height,
            "alphaMode": mat.get("alphaMode", "OPAQUE"), "tint": pbr.get("baseColorFactor", [1, 1, 1, 1]), "sha256": hashlib.sha256(raw_image).hexdigest()}
    descriptor = {"materials": material_info, "controls": controls, "humanoid": {k: doc["nodes"][v]["name"] for k, v in humanoid.items()},
                  "expressions": expressions, "weightedVertices": counts,
                  "maxBindError": bind_error, "sourceSha256": manifest["sha256"]}
    (ASSETS / "rig.json").write_text(json.dumps(descriptor, indent=2)+"\n", encoding="utf-8")
    # Squirrel accepts JSON object syntax via the engine's compile-time parser only with = fields;
    # emit an explicit table literal instead of relying on a second runtime JSON loader.
    def nut(v):
        if isinstance(v, dict):
            return "{" + ",".join("["+json.dumps(k)+"]="+nut(x) for k, x in v.items()) + "}"
        if isinstance(v, list):
            return "["+",".join(nut(x) for x in v)+"]"
        if isinstance(v, float):
            return repr(v)
        return json.dumps(v)
    (ROOT / "rig-data.nut").write_text("// Generated by prepare_rig.py; downloaded geometry is unchanged.\npersist atelierRigData = "+nut(descriptor)+";\n", encoding="utf-8")
    print(json.dumps({"facialJoints": len(CONTROLS), "maxBindError": bind_error, "weightedVertices": counts}))


if __name__ == "__main__":
    main()
