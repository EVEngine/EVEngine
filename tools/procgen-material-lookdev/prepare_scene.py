from pathlib import Path
import argparse
from PIL import Image
import math
import json
import struct
import shutil

parser = argparse.ArgumentParser(
    description="Pack exported PBR maps into the NVIDIA material comparison scene"
)
parser.add_argument(
    "directory", type=Path, help="Game folder containing assets exported by export.cpp"
)
root = parser.parse_args().directory.resolve()
assets = root / "assets"
assets.mkdir(parents=True, exist_ok=True)
names = [
    "wood",
    "metal",
    "rubber",
    "cloth",
    "plastic",
    "steel",
    "glass",
    "paper",
    "aluminum",
    "cast_iron",
    "cement",
    "paint",
    "marble",
]
for name in names:
    for role in ["albedo", "normal", "roughness", "metallic", "height", "ao"]:
        Image.open(assets / f"{name}_{role}.ppm").save(assets / f"{name}_{role}.png")
    Image.merge(
        "RGB",
        tuple(
            Image.open(assets / f"{name}_{role}.ppm").getchannel("R")
            for role in ["ao", "roughness", "metallic"]
        ),
    ).save(assets / f"{name}_orm.png")
verts = []
norms = []
uv = []
tang = []
indices = []
W = 128
H = 64
for y in range(H + 1):
    t = math.pi * y / H
    for x in range(W + 1):
        p = 2 * math.pi * x / W
        n = (math.sin(t) * math.cos(p), math.cos(t), math.sin(t) * math.sin(p))
        verts.extend(n)
        norms.extend(n)
        uv.extend((x / W, y / H))
        tang.extend((-math.sin(p), 0, math.cos(p), 1))
for y in range(H):
    for x in range(W):
        a = y * (W + 1) + x
        b = a + W + 1
        indices.extend((a, b + 1, b, a, a + 1, b + 1))
binary = bytearray()
views = []
access = []
for data, count, typ in [
    (verts, 3, "VEC3"),
    (norms, 3, "VEC3"),
    (uv, 2, "VEC2"),
    (tang, 4, "VEC4"),
    (indices, 1, "SCALAR"),
]:
    start = len(binary)
    binary += struct.pack("<" + ("I" if count == 1 else "f") * len(data), *data)
    views.append(dict(buffer=0, byteOffset=start, byteLength=len(binary) - start))
    acc = dict(
        bufferView=len(views) - 1,
        componentType=5125 if count == 1 else 5126,
        count=len(data) // count,
        type=typ,
    )
    if not access:
        acc.update(min=[-1, -1, -1], max=[1, 1, 1])
    access.append(acc)
(assets / "sphere.bin").write_bytes(binary)
for i, name in enumerate(names):
    mat = dict(
        name=name,
        pbrMetallicRoughness=dict(
            baseColorTexture=dict(index=0),
            metallicRoughnessTexture=dict(index=2),
            metallicFactor=1,
            roughnessFactor=1,
        ),
        normalTexture=dict(index=1),
        occlusionTexture=dict(index=2),
    )
    ext = {}
    if i in [1, 5, 8]:
        ext["KHR_materials_anisotropy"] = dict(
            anisotropyStrength=0.65 if i != 5 else 0.15, anisotropyRotation=math.pi / 2
        )
    coat = {0: 0.0, 4: 0.3, 11: 0.8, 12: 0.5}.get(i, 0)
    if coat:
        ext["KHR_materials_clearcoat"] = dict(
            clearcoatFactor=coat,
            clearcoatRoughnessFactor=0.14 if i == 11 else (0.12 if i == 0 else 0.07),
        )
    if i == 11:
        ext["KHR_materials_clearcoat"]["clearcoatNormalTexture"] = dict(index=1, scale=1.0)
    # Sphere longitude spans twice the physical distance of latitude at the equator.
    # Keep grains and yarns round instead of stretching them horizontally.
    if i not in [0, 6, 12]:
        transform = dict(extensions={"KHR_texture_transform": dict(scale=[2, 1])})
        for info in [
            mat["pbrMetallicRoughness"]["baseColorTexture"],
            mat["pbrMetallicRoughness"]["metallicRoughnessTexture"],
            mat["normalTexture"],
            mat["occlusionTexture"],
        ]:
            info.update(transform)
        if i == 11:
            ext["KHR_materials_clearcoat"]["clearcoatNormalTexture"].update(transform)
    if i in [2, 3]:
        ext["KHR_materials_specular"] = dict(specularFactor=0.22 if i == 2 else 0.15)
    if i == 6:
        mat.update(alphaMode="BLEND")
        mat["pbrMetallicRoughness"]["baseColorFactor"] = [1, 1, 1, 0.16]
    if ext:
        mat["extensions"] = ext
    doc = dict(
        asset=dict(version="2.0"),
        scene=0,
        scenes=[dict(nodes=[0])],
        nodes=[dict(mesh=0)],
        buffers=[dict(uri="sphere.bin", byteLength=len(binary))],
        bufferViews=views,
        accessors=access,
        images=[dict(uri=f"{name}_{r}.png") for r in ["albedo", "normal", "orm"]],
        samplers=[dict(magFilter=9729, minFilter=9987, wrapS=10497, wrapT=10497)],
        textures=[dict(sampler=0, source=k) for k in range(3)],
        materials=[mat],
        meshes=[
            dict(
                primitives=[
                    dict(
                        attributes=dict(POSITION=0, NORMAL=1, TEXCOORD_0=2, TANGENT=3),
                        indices=4,
                        material=0,
                    )
                ]
            )
        ],
    )
    if ext:
        doc["extensionsUsed"] = list(ext) + (
            ["KHR_texture_transform"] if i not in [0, 6, 12] else []
        )
    (assets / f"{name}.gltf").write_text(json.dumps(doc), encoding="utf-8")


source = Path(__file__).resolve().parent
shutil.copytree(source / "assets", assets, dirs_exist_ok=True)
for filename in ["main.nut", "config.nut"]:
    shutil.copy2(source / filename, root / filename) if (source / filename).resolve() != (
        root / filename
    ).resolve() else None
shutil.copytree(source / "shaders", root / "shaders", dirs_exist_ok=True)
