"""Prepare and audit genuine paired captures for the explicit road material contract."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

import road_parity
import utf8_stdio

REPO = Path(__file__).resolve().parent.parent
MATERIAL = REPO / "scripts/road_parity/material"


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def prepare(args: argparse.Namespace) -> None:
    root, project = args.root.resolve(), args.project.resolve()
    contract = json.loads((MATERIAL / "contract.json").read_text(encoding="utf-8"))
    if contract["textureInterior"] != [0.05, 0.95] or contract["lightRadiance"] != [1.0, 1.0, 1.0]:
        raise ValueError("Contract v1 fixes the sampling interior and unit light radiance")
    parameters = {
        "ROAD_PERIOD": contract["periodMeters"],
        "ROAD_ROUGHNESS": contract["roughness"],
        "ROAD_NORMAL_STRENGTH": contract["normalStrength"],
        "ROAD_AMBIENT": contract["ambient"],
    }
    text = "".join(f"#define {key} {float(value)}\n" for key, value in parameters.items())
    direction = ",".join(str(float(x)) for x in contract["lightDirection"])
    text += f"#define ROAD_LIGHT_DIRECTION float3({direction})\n"
    (root / "RoadMaterialParams.inc").write_text(text, encoding="utf-8")
    subprocess.run(
        [
            args.glslc,
            "-fshader-stage=frag",
            "-I",
            str(root),
            str(MATERIAL / "RoadAsphalt.frag"),
            "-o",
            str(root / "road-asphalt.frag.spv"),
        ],
        check=True,
    )
    for fixture in sorted((root / "fixtures").glob("*/fixture.json")):
        spec = json.loads(fixture.read_text(encoding="utf-8"))
        spec["backgroundColor"] = [contract["backgroundDisplaySRGB"]] * 3
        write_json(fixture, spec)
    road_parity.prepare(argparse.Namespace(root=root, package=None))
    textures = project / "Assets/EasyRoads3D/textures/roads"
    unity_assets = project / "Assets/RoadMaterialParity"
    unity_assets.mkdir(parents=True, exist_ok=True)
    for name in ("RoadAsphalt.shader", "RoadAsphaltCommon.inc"):
        shutil.copyfile(MATERIAL / name, unity_assets / name)
    shutil.copyfile(root / "RoadMaterialParams.inc", unity_assets / "RoadMaterialParams.inc")
    for source, name in (("roadsample_d.jpg", "road-color.jpg"), ("road_b.jpg", "road-normal.jpg")):
        shutil.copyfile(textures / source, unity_assets / name)
    shutil.copyfile(
        REPO / "scripts/road_parity/UnityCapture.cs", project / "Assets/Editor/RoadParityCapture.cs"
    )
    for game in sorted((root / "games").iterdir()):
        shutil.copyfile(MATERIAL / "EvMaterialRender.nut", game / "main.nut")
        shutil.copyfile(root / "road-asphalt.frag.spv", game / "road-asphalt.frag.spv")
        for name in ("road-color.jpg", "road-normal.jpg"):
            shutil.copyfile(unity_assets / name, game / name)
    print("Prepared material inputs; capture both engines before running audit")


def audit(args: argparse.Namespace) -> None:
    import numpy as np
    from PIL import Image, ImageFilter

    root, project = args.root.resolve(), args.project.resolve()
    log = args.unity_log.read_text(encoding="utf-8", errors="replace")
    if "ROAD_MATERIAL_REFERENCE_COMPLETE" not in log or "return code 0" not in log:
        raise ValueError("Unity capture did not complete successfully")
    fixtures = sorted((root / "fixtures").glob("*/fixture.json"))
    if len(fixtures) != 35:
        raise ValueError("The requested scene set must contain exactly 35 fixtures")
    report = {
        "schema": "eve.tests.road-material-comparison",
        "version": 1,
        "scope": "Shared explicit forward material, current surface-only scene set; no default deferred parity claim",
        "contract": json.loads((MATERIAL / "contract.json").read_text(encoding="utf-8")),
        "metrics": "Raw RGB errors on two-pixel-eroded common surface; silhouette threshold 0.98. Difference image is absolute RGB error x4.",
        "cases": [],
        "hashes": {},
    }
    for fixture in fixtures:
        directory = fixture.parent
        spec = json.loads(fixture.read_text(encoding="utf-8"))
        reference = json.loads((directory / "unity-evidence.json").read_text(encoding="utf-8"))
        if reference["fixtureSha256"] != sha(fixture):
            raise ValueError(f"Stale Unity geometry: {directory.name}")
        row = {"caseId": directory.name, "views": []}
        for view in ("perspective", "top"):
            capture = (directory / f"ev-{view}.log").read_text(encoding="utf-8", errors="replace")
            if "[capture] OK (engine frame)" not in capture:
                raise ValueError(f"Native frame capture missing: {directory.name} {view}")
            images = []
            for side in ("ev", "unity"):
                path = directory / f"{side}-{view}.png"
                image = Image.open(path).convert("RGB")
                if image.size != (spec["width"], spec["height"]):
                    raise ValueError(f"Wrong dimensions: {path}")
                images.append(np.asarray(image, dtype=np.int16))
                report["hashes"][str(path)] = sha(path)
            native, unity = images
            masks = [np.max(np.abs(image - image[0, 0]), axis=2) > 2 for image in images]
            union, intersection = masks[0] | masks[1], masks[0] & masks[1]
            core = (
                np.asarray(
                    Image.fromarray((intersection * 255).astype("uint8")).filter(
                        ImageFilter.MinFilter(5)
                    )
                )
                > 0
            )
            if not core.any() or not union.any():
                raise ValueError(f"Empty captured surface: {directory.name} {view}")
            difference = np.abs(native - unity)
            metrics = {
                "view": view,
                "surfaceIoU": float(intersection.sum() / union.sum()),
                "interiorMeanAbsoluteRgbError": float(difference[core].mean()),
                "interiorP95RgbError": float(np.percentile(difference[core], 95)),
                "interiorMeanRgbBias": float((native - unity)[core].mean()),
                "backgroundRgbError": int(np.abs(native[0, 0] - unity[0, 0]).max()),
                "interiorPixels": int(core.sum()),
            }
            metrics["withinThresholds"] = (
                metrics["surfaceIoU"] >= 0.98
                and metrics["interiorMeanAbsoluteRgbError"] <= 2
                and metrics["interiorP95RgbError"] <= 5
                and metrics["backgroundRgbError"] <= 1
            )
            row["views"].append(metrics)
            Image.fromarray(np.clip(difference * 4, 0, 255).astype("uint8")).save(
                directory / f"difference-material-{view}.png"
            )
            game = root / "games" / f"{directory.name}-{view}"
            for name in (
                "main.nut",
                "mesh_data.nut",
                "road-asphalt.frag.spv",
                "road-color.jpg",
                "road-normal.jpg",
            ):
                report["hashes"][str(game / name)] = sha(game / name)
        row["status"] = (
            "passed" if all(view["withinThresholds"] for view in row["views"]) else "different"
        )
        report["cases"].append(row)
    for path in [
        *MATERIAL.glob("*"),
        project / "Assets/Editor/RoadParityCapture.cs",
        project / "Assets/RoadMaterialParity/RoadAsphalt.shader",
        project / "Assets/RoadMaterialParity/RoadAsphaltCommon.inc",
        args.unity_log,
    ]:
        if path.is_file():
            report["hashes"][str(path.resolve())] = sha(path)
    report["runtime"] = road_parity.renderer_runtime_hashes(args.eve)
    report["referenceRuntimeSha256"] = sha(project / "Assets/EasyRoads3D/lib/EasyRoads3Dv3.dll")
    report["passedCases"] = sum(case["status"] == "passed" for case in report["cases"])
    write_json(root / "material-comparison.json", report)
    print(
        f"Captured 35 scenes / 140 frames; {report['passedCases']} cases within declared thresholds"
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("prepare", "audit"))
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--glslc", default="glslc")
    parser.add_argument("--unity-log", type=Path)
    parser.add_argument("--eve", type=Path)
    args = parser.parse_args()
    if args.command == "prepare":
        prepare(args)
    else:
        if not args.unity_log or not args.eve:
            parser.error("audit requires --unity-log and --eve")
        audit(args)


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    main()
