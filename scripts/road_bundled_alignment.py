"""Prepare native author bakes and separate original-mesh rendering calibration."""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import shutil
import struct
import subprocess
from pathlib import Path

import numpy as np
from PIL import Image

import utf8_stdio


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def json_read(path: Path):
    return json.loads(path.read_text(encoding="utf-8"))


def canonical_network(road: dict, template: dict) -> dict:
    if len(road["shape"]) != len(road["shapeMaterials"]) or len(road["shape"]) != len(
        road["shapeUV"]
    ):
        raise ValueError("Incomplete original cross-section resource mapping")
    for marker in road["markers"]:
        if not 0.01 <= marker["strength"] <= 1 or marker["control"] not in range(4):
            raise ValueError("Unsupported original spline controller/strength")
        if marker["width"] != 0 or marker["shape"] != road["shape"]:
            raise ValueError("Variable original width/profile requires an explicit native adapter")
    if any(tag not in (0, 1) for tag in road["shapeMaterials"]):
        raise ValueError("Unmapped original cross-section material")
    network = copy.deepcopy(template)
    network["edges"] = network["edges"][:1]
    network["nodes"] = network["nodes"][:2]
    edge = network["edges"][0]
    style = edge["style"]
    edge["from"], edge["to"], edge["id"] = 1, 2, 1
    points = road["markers"]
    if road["closed"]:
        points = points + [copy.deepcopy(points[0])]
    edge["points"] = [[m["position"]["x"], m["position"]["y"], -m["position"]["z"]] for m in points]
    profile_template = copy.deepcopy(edge["pointProfiles"][0])
    profiles = []
    for marker in points:
        p = copy.deepcopy(profile_template)
        p.update(
            splineStrength=marker["strength"],
            rollDegrees=marker["roll"],
            widthScale=1.0,
            pathControl=marker["control"],
            rollCenterOffset=marker["pivot"] - 0.5,
        )
        profiles.append(p)
    edge["pointProfiles"] = profiles
    style.update(
        automaticStructures=False,
        facilities=[],
        structures=[],
        uvMeters=5.0,
        samplingSpacingMeters=road["resolution"],
        samplingAngleDegrees=45.0,
        crossSectionClosed=False,
        laneWidth=3.0,
    )
    section = []
    for i, point in enumerate(road["shape"]):
        candidate = {
            "side": -point["x"],
            "up": point["y"],
            "material": 0 if road["shapeMaterials"][i] == 0 else 2,
        }
        if section and all(abs(section[-1][key] - candidate[key]) < 1e-6 for key in ("side", "up")):
            section[-1] = candidate
        else:
            section.append(candidate)
    style["crossSection"] = section
    for i, point in enumerate((edge["points"][0], edge["points"][-1])):
        node = network["nodes"][i]
        node["id"] = i + 1
        node.update(zip(("x", "y", "z"), point, strict=True))
    return network


def vector(value: dict, reflect=False) -> list:
    return [value["x"], value["y"], -value["z"] if reflect else value["z"]]


def glsl_vec(value) -> str:
    return "vec3(" + ",".join(str(float(v)) for v in value) + ")"


def shader_ambient(scene: dict) -> list:
    if scene["colorSpace"] != "Gamma" or scene["ambientMode"] != "Flat":
        raise ValueError("The bundled Standard port currently covers Gamma/Flat ambient only")
    # UnityCG ShadeSH9 / StandardUtils ShadeSHPerPixel re-encode SH in Gamma projects.
    return [
        max(0.0, 1.055 * max(0.0, scene["ambientIrradiance"][k]) ** (1 / 2.4) - 0.055)
        for k in "rgb"
    ]


def materials(scene: dict, project: Path, directory: Path) -> list:
    materials = copy.deepcopy(scene["materials"])
    for terrain in scene["terrains"]:
        if len(terrain["layers"]) != 2:
            raise ValueError("Terrain port requires the original two-layer example")
        weights = np.asarray(terrain["weights"]).reshape(
            terrain["alphaHeight"], terrain["alphaWidth"], 2
        )
        pixels = np.zeros((terrain["alphaHeight"], terrain["alphaWidth"], 4), dtype=np.uint8)
        pixels[:, :, :2] = np.rint(weights[::-1] * 255).astype(np.uint8)
        pixels[:, :, 3] = 255
        Image.fromarray(pixels).save(directory / (terrain["material"] + "-weights.png"))
        materials.append(
            {
                "id": terrain["material"],
                "shader": terrain["shaderName"],
                "terrain": terrain,
                "color": dict.fromkeys("rgba", 1),
                "metallic": 0,
                "smoothness": 0,
                "normalScale": 0,
                "occlusion": 1,
                "textures": [
                    {
                        "property": "_MainTex",
                        "path": terrain["layers"][0]["diffuse"],
                        "scale": {"x": 1, "y": 1},
                        "offset": {"x": 0, "y": 0},
                    }
                ],
            }
        )
    light = next(item for item in scene["lights"] if item["type"] == 1)
    direction = vector(light["forward"], True)
    direction = [-v for v in direction]
    for material in materials:
        mid = material["id"]
        material["colorFile"] = mid + "-albedo.png"
        material["normalFile"] = mid + "-normal.png"
        textures = {t["property"]: t for t in material["textures"]}
        main = textures.get("_MainTex")
        if main:
            Image.open(
                project / main["path"] if not Path(main["path"]).is_absolute() else main["path"]
            ).convert("RGBA").save(directory / material["colorFile"])
        else:
            Image.new("RGB", (4, 4), (255, 255, 255)).save(directory / material["colorFile"])
        normal = textures.get("_BumpMap")
        if normal:
            Image.open(project / normal["path"]).convert("RGB").save(
                directory / material["normalFile"]
            )
        else:
            Image.new("RGB", (4, 4), (128, 128, 255)).save(directory / material["normalFile"])
        terrain = material.get("terrain")
        if terrain:
            Image.open(project / terrain["layers"][1]["diffuse"]).convert("RGBA").save(
                directory / material["normalFile"]
            )
            material["weightsFile"] = mid + "-weights.png"
        smoothness = material["smoothness"]
        packed = textures.get("_Metallic") if material["shader"] != "Standard" else None
        if packed:
            image = (
                np.asarray(Image.open(project / packed["path"]).convert("RGBA"), dtype=float) / 255
            )
            if np.max(image, axis=(0, 1)).tolist() != np.min(image, axis=(0, 1)).tolist():
                raise ValueError("Varying packed material requires a dedicated sampler")
            smoothness *= float(image[0, 0, 3])
        constants = {
            "LAMBERT": "1"
            if terrain and terrain["shaderName"] == "Nature/Terrain/Diffuse"
            else "0",
            "SHADOWS": "1" if light["shadows"] else "0",
            "TERRAIN": "1" if terrain else "0",
            "UV_SCALE": "vec2("
            + str(main["scale"]["x"] if main else 1)
            + ","
            + str(main["scale"]["y"] if main else 1)
            + ")",
            "UV_OFFSET": "vec2("
            + str(main["offset"]["x"] if main else 0)
            + ","
            + str(main["offset"]["y"] if main else 0)
            + ")",
            "TINT": glsl_vec([material["color"][k] for k in "rgb"]),
            "NORMAL_SCALE": str(material["normalScale"] if normal else 0.0),
            "LIGHT_DIRECTION": glsl_vec(direction),
            "LIGHT_COLOR": glsl_vec([light["color"][k] * light["intensity"] for k in "rgb"]),
            "AMBIENT": glsl_vec(shader_ambient(scene)),
            "METALLIC": str(float(material["metallic"])),
            "SMOOTHNESS": str(float(smoothness)),
            "OCCLUSION": str(float(material["occlusion"])),
        }
        if terrain:
            for i, layer in enumerate(terrain["layers"]):
                constants["TERRAIN_SCALE" + str(i)] = (
                    f"vec2({terrain['size']['x'] / layer['tileSize']['x']},{terrain['size']['z'] / layer['tileSize']['y']})"
                )
                constants["TERRAIN_OFFSET" + str(i)] = (
                    f"vec2({layer['tileOffset']['x'] / layer['tileSize']['x']},{layer['tileOffset']['y'] / layer['tileSize']['y']})"
                )
        (directory / "BundledParams.inc").write_text(
            "\n".join("#define " + k + " " + v for k, v in constants.items()) + "\n",
            encoding="utf-8",
        )
        subprocess.run(
            [
                "glslc",
                "-I",
                str(directory),
                "scripts/road_parity/material/BundledMaterial.frag",
                "-o",
                str(directory / (mid + ".spv")),
            ],
            check=True,
        )
    return materials


def write_glb(path: Path, parts: list) -> None:
    document = {
        "asset": {"version": "2.0", "generator": "EVEngine bundled reference interchange"},
        "buffers": [{"byteLength": 0}],
        "bufferViews": [],
        "accessors": [],
        "meshes": [],
        "nodes": [],
        "materials": [],
        "scenes": [{"nodes": list(range(len(parts)))}],
        "scene": 0,
    }
    blob = bytearray()

    def accessor(values, component, kind):
        array = np.asarray(values, dtype="<u4" if component == 5125 else "<f4")
        while len(blob) % 4:
            blob.append(0)
        view = len(document["bufferViews"])
        document["bufferViews"].append(
            {"buffer": 0, "byteOffset": len(blob), "byteLength": array.nbytes}
        )
        blob.extend(array.tobytes())
        item = {"bufferView": view, "componentType": component, "count": len(array), "type": kind}
        if kind == "VEC3":
            item.update(min=array.min(axis=0).tolist(), max=array.max(axis=0).tolist())
        document["accessors"].append(item)
        return len(document["accessors"]) - 1

    for index, part in enumerate(parts):
        pos = accessor(np.asarray(part["positions"]).reshape(-1, 3), 5126, "VEC3")
        normal = accessor(np.asarray(part["normals"]).reshape(-1, 3), 5126, "VEC3")
        uv = accessor(np.asarray(part["uvs"]).reshape(-1, 2), 5126, "VEC2")
        indices = accessor(part["indices"], 5125, "SCALAR")
        tangent_data = part.get("tangents", [])
        if tangent_data:
            tangents = np.asarray(tangent_data).reshape(-1, 4)
            colors = np.column_stack((tangents[:, :3] * 0.5 + 0.5, (tangents[:, 3] + 1) * 0.5))
        else:
            colors = np.full((len(part["positions"]) // 3, 4), 0.5)
        color = accessor(colors, 5126, "VEC4")
        document["materials"].append({"name": "part-" + str(index)})
        document["meshes"].append(
            {
                "name": "part-" + str(index),
                "primitives": [
                    {
                        "attributes": {
                            "POSITION": pos,
                            "NORMAL": normal,
                            "TEXCOORD_0": uv,
                            "COLOR_0": color,
                        },
                        "indices": indices,
                        "material": index,
                    }
                ],
            }
        )
        document["nodes"].append({"mesh": index})
    document["buffers"][0]["byteLength"] = len(blob)
    encoded = json.dumps(document, separators=(",", ":"), allow_nan=False).encode("utf-8")
    encoded += b" " * ((-len(encoded)) % 4)
    blob.extend(b"\0" * ((-len(blob)) % 4))
    path.write_bytes(
        struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(encoded) + 8 + len(blob))
        + struct.pack("<II", len(encoded), 0x4E4F534A)
        + encoded
        + struct.pack("<II", len(blob), 0x004E4942)
        + blob
    )


def write_game(
    game: Path,
    scene: dict,
    parts: list,
    material_list: list,
    resources: Path,
    view: dict,
    mask=False,
) -> None:
    game.mkdir(parents=True, exist_ok=True)
    for m in material_list:
        for file in (m["colorFile"], m["normalFile"], m["id"] + ".spv"):
            shutil.copyfile(resources / file, game / file)
        if m.get("weightsFile"):
            shutil.copyfile(resources / m["weightsFile"], game / m["weightsFile"])
    write_glb(game / "scene.glb", parts)
    script = [
        "persist sun=null;persist model=null;persist objects=[];persist meshes=[];persist mats=[];persist textures=[];persist images=[];persist shaders=[];persist camera=null;",
        f'function eve_init(){{local rc=gfx.getRenderControl();rc.disable("gbuffer");rc.disable("gpuDriven");rc.disable("aa");gfx.setMsaaSamples({scene["msaaSamples"]});rc.compile();local tone=gfx.setSceneToneMapping("none");if(!tone.ok)throw "tone failed";',
    ]
    lookup = {m["id"]: i for i, m in enumerate(material_list)}
    script.append(
        f'model=model3d.newModelDataFromFile("scene.glb");if(model==null||model.getMeshCount()!={len(parts)})throw "model part mismatch";'
    )
    for m in material_list:
        main = next((t for t in m["textures"] if t["property"] == "_MainTex"), {})
        mip = "true" if main.get("mipmaps", True) else "false"
        aniso = float(max(1, main.get("anisotropy", 1)))
        extra = ""
        if m.get("weightsFile"):
            extra = f'local weights=gfx.newTextureFromFileRepeated("{m["weightsFile"]}",false,false);if(weights==null)throw "weights missing";textures.append(weights);material.setHeightTexture(weights);'
        script.append(
            f'{{local shader=gfx.loadMeshShaderSpv("","{m["id"]}.spv");if(!shader.ok)throw "shader failed";shaders.append(shader.value);local mode=gfx.configureMeshShaderSurface(shader.value,"opaque",true,false);if(!mode.ok)throw "surface failed";local raster=gfx.configureMeshShaderRaster(shader.value,"lessEqual",{float(m.get("offsetUnits", 0))},{float(m.get("offsetFactor", 0))},15);if(!raster.ok)throw "raster failed";local material=gfx.newMaterial();material.setShader(shader.value);local image=eve.Image();local colorImage=image.newImageDataFromFile("{m["colorFile"]}");local normalImage=image.newImageDataFromFile("{m["normalFile"]}");images.append(colorImage);images.append(normalImage);local color=gfx.newTextureWithSampler(colorImage,true,true,{mip},{aniso},"linear","linear",0.0);local normal=gfx.newTextureWithSampler(normalImage,true,true,{mip},{aniso},"linear","linear",0.0);if(color==null||normal==null)throw "texture failed";textures.append(color);textures.append(normal);material.setAlbedoTexture(color);material.setNormalTexture(normal);{extra}material.setTint(1.0,1.0,1.0,1.0);mats.append(material);}}'
        )
    light = next(lamp for lamp in scene["lights"] if lamp["type"] == 1)
    cast = "true" if light["shadows"] and not mask else "false"
    if not mask:
        direction = vector(light["forward"], True)
        radiance = [light["color"][k] * light["intensity"] for k in "rgb"]
        script.append(
            'sun=eve.Light3D();sun.setType("dir");sun.setDirection('
            + ",".join(map(str, direction))
            + ");sun.setColor("
            + ",".join(map(str, radiance))
            + f",1.0);sun.setCastShadow({cast});"
        )
    for i, p in enumerate(parts):
        script.append(
            f'{{local object=model3d.createRenderable(gfx,model,{i});if(object==null)throw "mesh failed";object.setMaterial(mats[{lookup[p["material"]]}]);object.setCastShadow({cast});object.setReceiveShadow({cast});objects.append(object);}}'
        )
    eye, target = vector(view["eye"], True), vector(view["target"], True)
    script.append(
        "camera=eve.Camera3D();camera.setEye("
        + ",".join(map(str, eye))
        + ");camera.setTarget("
        + ",".join(map(str, target))
        + f');camera.setUp(0.0,1.0,0.0);camera.setFov({float(view["fov"])});camera.setClipPlanes({float(view["near"])},{float(view["far"])});camera.setAutoExposure(false,-8.0,8.0);camera.setExposure(0.0);camera.setActive(true);gfx.setBackgroundColor(0.14,0.12,0.11,1.0);print("BUNDLED_ALIGNMENT_READY\\n");}}\nfunction eve_update(dt){{}}\nfunction eve_render(){{gfx.clear();gfx.render3D();}}'
    )
    if mask:
        script = [
            s.replace(
                "gfx.setBackgroundColor(0.14,0.12,0.11,1.0)",
                "gfx.setBackgroundColor(0.0,0.0,0.0,1.0)",
            )
            for s in script
        ]
    (game / "main.nut").write_text("\n".join(script), encoding="utf-8")
    (game / "config.nut").write_text(
        'config <- {width=1280 height=720 title="Bundled EasyRoads correspondence" debug=false hotReload=false};\n',
        encoding="utf-8",
    )


def prepare(args: argparse.Namespace) -> None:
    template = json_read(
        Path("build/road-parity/options-parity/source/option-median/recipe_network.json")
    )
    for source in sorted(args.root.glob("*/scene.json")):
        directory = source.parent
        scene = json_read(source)
        netdir = directory / "networks"
        netdir.mkdir(exist_ok=True)
        mapping = {}
        for i, road in enumerate(scene["roads"]):
            key = f"road-{i:03d}"
            mapping[key] = road["id"]
            (netdir / (key + ".json")).write_text(
                json.dumps(canonical_network(road, template)), encoding="utf-8"
            )
        subprocess.run([str(args.generator.resolve()), str(directory.resolve())], check=True)
        resources = directory / "resources"
        resources.mkdir(exist_ok=True)
        material_list = materials(scene, args.project, resources)
        material_ids = {
            m["id"] for m in material_list if m["shader"] == "EasyRoads3D/ER Edit Mode Surface"
        }
        context = [
            p
            for p in scene["parts"]
            if p["role"] != "road-output" and p["material"] not in material_ids
        ]
        calibration = [p for p in scene["parts"] if p["material"] not in material_ids]
        native = []
        for result in json_read(directory / "native-roads.json")["roads"]:
            road_id = mapping[result["id"]]
            road = next(r for r in scene["roads"] if r["id"] == road_id)
            refs = [
                p for p in scene["parts"] if p["road"] == road_id and p["role"] == "road-output"
            ]
            for part in result["parts"]:
                if part["group"] not in ("asphalt", "sidewalk", "curb"):
                    continue
                part["material"] = refs[0 if part["group"] == "asphalt" else min(1, len(refs) - 1)][
                    "material"
                ]
                # Resource binding adapter: native metric UVs to the original profile atlas.
                uv = np.asarray(part["uvs"]).reshape(-1, 2)
                original = uv.copy()
                shape = road["shape"]
                sides = np.array([-p["x"] for p in shape])
                profile_uv = np.array(road["shapeUV"])
                order = np.argsort(sides, kind="stable")
                if part["group"] == "asphalt":
                    asphalt_sides = [-p["x"] for p in shape if abs(p["y"]) < 1e-4]
                    half = (max(asphalt_sides) - min(asphalt_sides)) / 2
                    uv[:, 0] = 0.5 - original[:, 1] * 5 / (2 * half)
                    if len(shape) == 2:
                        uv[:, 0] = 1 - uv[:, 0]
                else:
                    uv[:, 0] = np.interp(original[:, 1] * 5, sides[order], profile_uv[order])
                uv[:, 1] = original[:, 0] * road["uvTiling"]
                part["uvs"] = uv.reshape(-1).tolist()
                part["road"] = road_id
                part["role"] = "native-road"
                native.append(part)
        (directory / "binding-evidence.json").write_text(
            json.dumps(
                {
                    "schema": "eve.tests.bundled-bindings",
                    "version": 1,
                    "roads": mapping,
                    "sceneSha256": sha(source),
                    "nativeRoadsSha256": sha(directory / "native-roads.json"),
                    "materialResources": {
                        str(p.name): sha(p) for p in resources.iterdir() if p.is_file()
                    },
                    "limits": [
                        "Roads generated independently; junction meshes and terrain are shared reference context, not native junction generation proof",
                        "UV atlas mapping is a fixture resource-binding adapter, not a production UV API fix",
                        "Standard/Gamma BRDF port is provisional: shadow, reflection, terrain LOD and sky parity not yet proven",
                    ],
                },
                indent=2,
            ),
            encoding="utf-8",
        )
        for view in scene["views"]:
            for mode, parts in (("calibration", calibration), ("generated", context + native)):
                write_game(
                    args.root / "games" / (directory.name + "-" + mode + "-" + view["name"]),
                    scene,
                    parts,
                    material_list,
                    resources,
                    view,
                )
        mask_material = {
            "id": "mask",
            "colorFile": "mask-color.png",
            "normalFile": "mask-normal.png",
            "textures": [],
        }
        Image.new("RGB", (4, 4), (255, 255, 255)).save(resources / "mask-color.png")
        Image.new("RGB", (4, 4), (128, 128, 255)).save(resources / "mask-normal.png")
        mask_shader = resources / "mask.frag"
        mask_shader.write_text(
            "#version 450\nlayout(location=0) out vec4 color;void main(){color=vec4(1);}\n",
            encoding="utf-8",
        )
        subprocess.run(["glslc", str(mask_shader), "-o", str(resources / "mask.spv")], check=True)
        for view in scene["views"]:
            for mode, source_parts in (("calibration", calibration), ("generated", native)):
                masked = [
                    dict(p, material="mask")
                    for p in source_parts
                    if p["role"] in ("road-output", "native-road")
                ]
                write_game(
                    args.root
                    / "games"
                    / (directory.name + "-" + mode + "-" + view["name"] + "-mask"),
                    scene,
                    masked,
                    [mask_material],
                    resources,
                    view,
                    True,
                )
        print(
            directory.name,
            "independent roads",
            len(mapping),
            "native parts",
            len(native),
            flush=True,
        )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path("build/road-parity/bundled-alignment"))
    parser.add_argument("--project", type=Path, default=Path("build/road-parity/unity-project"))
    parser.add_argument(
        "--generator",
        type=Path,
        default=Path("build/road-parity/native-verified/road_bundled_fixture.exe"),
    )
    prepare(parser.parse_args())


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    main()
