#!/usr/bin/env python3
"""Prepare and render genuine EasyRoads/EVEngine road surface parity fixtures."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import shutil
import subprocess
import tarfile
import tempfile
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath

import utf8_stdio

REPO = Path(__file__).resolve().parent.parent
TOOLS = REPO / "scripts" / "road_parity"
DEFAULT_ROOT = REPO / "build" / "road-parity"


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")


def ev_adapter_hashes() -> dict[str, str]:
    return {"renderAdapterSha256": digest(TOOLS / "EvRender.nut"),
            "captureAdapterSha256": digest(REPO / "scripts/capture_example.ps1"),
            "startupSha256": digest(REPO / "src/scripts/load.nut"),
            "captureHookSha256": digest(REPO / "scripts/capture_root.nut")}


def ev_game_hashes(root: Path, case_id: str) -> dict[str, dict[str, str]]:
    return {view: {name: digest(root / "games" / f"{case_id}-{view}" / name)
                   for name in ("main.nut", "config.nut", "mesh_data.nut")}
            for view in ("perspective", "top")}


def load_fixture(path: Path) -> dict:
    value = json.loads(path.read_text(encoding="utf-8"))
    if value.get("schema") != "eve.tests.road-parity" or value.get("schemaVersion") != 1:
        raise ValueError(f"Unsupported fixture schema: {path}")
    if value.get("scope") != "surface-only" or value.get("units") != "metres" or value.get("axis") != "Y-up":
        raise ValueError(f"Unsupported fixture scope/coordinates: {path}")
    if value["caseId"] != path.parent.name:
        raise ValueError("Fixture identity does not match its directory")
    if not value["caseId"].replace("-", "").isalnum():
        raise ValueError("Fixture identity must be a simple directory name")
    for road in value.get("roads", []):
        for key in ("referenceZeroBumpiness", "referenceCreatedDefaults"):
            if key in road and not isinstance(road[key], bool):
                raise ValueError(f"{key} must be an explicit boolean")
    mesh = value["mesh"]
    vertices = len(mesh["positions"]) // 3
    if (
        not vertices
        or len(mesh["positions"]) != vertices * 3
        or len(mesh["normals"]) != vertices * 3
        or len(mesh["uvs"]) != vertices * 2
        or not mesh["indices"]
        or len(mesh["indices"]) % 3
    ):
        raise ValueError("Fixture mesh streams are inconsistent")
    if any(not isinstance(i, int) or not 0 <= i < vertices for i in mesh["indices"]):
        raise ValueError("Fixture mesh index is invalid")
    if any(not math.isfinite(n) for channel in ("positions", "normals", "uvs") for n in mesh[channel]):
        raise ValueError("Fixture mesh is not finite")
    return value


def unity_vector(value: list) -> dict:
    # EVEngine view matrices are right-handed; Unity transforms are left-handed.
    x, y, z = value
    return {"x": x, "y": y, "z": -z}


def prepare(args: argparse.Namespace) -> None:
    root = args.root.resolve()
    fixtures = root / "fixtures"
    paths = sorted(fixtures.glob("*/fixture.json"))
    if not paths:
        raise ValueError("Generate the native fixtures first")
    for path in paths:
        value = load_fixture(path)
        projection = {k: v for k, v in value.items() if k != "mesh"}
        projection["sourceFixtureSha256"] = digest(path)
        for key in ("perspectiveEye", "topEye", "target"):
            projection[key] = unity_vector(projection[key])
        projection["nodes"] = [
            {"id": node["id"], "position": unity_vector(node["position"])} for node in value["nodes"]
        ]
        projection["roads"] = [
            {**road, "markers": [unity_vector(point) for point in road["markers"]]} for road in value["roads"]
        ]
        write_json(path.parent / "unity-fixture.json", projection)
        for view, eye, up in (
            ("perspective", value["perspectiveEye"], [0, 1, 0]),
            ("top", value["topEye"], [0, 0, -1]),
        ):
            game = root / "games" / f"{value['caseId']}-{view}"
            game.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(TOOLS / "EvRender.nut", game / "main.nut")
            (game / "config.nut").write_text(
                'config <- { width = 1280 height = 720 title = "Road Surface Parity" '
                "debug = false hotReload = false };",
                encoding="utf-8",
            )
            channels = "\n".join(f"{key} = {json.dumps(items)}," for key, items in value["mesh"].items())
            (game / "mesh_data.nut").write_text(
                f"parity_data <- {{\n{channels}\n}};\n"
                f"parity_view <- {{ eye = {json.dumps(eye)}, target = {json.dumps(value['target'])}, "
                f"up = {json.dumps(up)}, fov = {value['fovY']} }};\n",
                encoding="utf-8",
            )
    if args.package:
        project = root / "unity-project"
        project.mkdir(parents=True, exist_ok=True)
        with tarfile.open(args.package, "r:gz") as package:
            for member in package.getmembers():
                if not member.name.endswith("/pathname"):
                    continue
                source = package.extractfile(member)
                if source is None:
                    raise ValueError("Package pathname is not a regular file")
                pathname = source.read().decode("utf-8").splitlines()[0]
                relative = PurePosixPath(pathname)
                if relative.is_absolute() or ".." in relative.parts or relative.parts[0] != "Assets":
                    raise ValueError(f"Unsafe Unity asset path: {pathname}")
                # The outer version is the reference. Do not import embedded beta/SRP overlays.
                if relative.suffix.lower() == ".unitypackage":
                    continue
                destination = (project / Path(*relative.parts)).resolve()
                if not destination.is_relative_to(project.resolve()):
                    raise ValueError(f"Unity asset escapes reference project: {pathname}")
                prefix = member.name.rsplit("/", 1)[0]
                try:
                    asset = package.getmember(prefix + "/asset")
                except KeyError:
                    destination.mkdir(parents=True, exist_ok=True)
                else:
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    content = package.extractfile(asset)
                    if content is None:
                        raise ValueError(f"Asset is not a file: {pathname}")
                    destination.write_bytes(content.read())
                try:
                    meta = package.getmember(prefix + "/asset.meta")
                except KeyError:
                    pass
                else:
                    content = package.extractfile(meta)
                    if content is None:
                        raise ValueError(f"Asset metadata is not a file: {pathname}")
                    Path(str(destination) + ".meta").write_bytes(content.read())
        editor = project / "Assets" / "Editor"
        editor.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(TOOLS / "UnityCapture.cs", editor / "RoadParityCapture.cs")
        write_json(
            root / "reference-package.json",
            {"schema": "eve.tests.road-parity.reference", "schemaVersion": 1,
             "packageSha256": digest(args.package),
             "runtimeDllSha256": digest(project / "Assets/EasyRoads3D/lib/EasyRoads3Dv3.dll"),
             "versionScope": "outer-package-only"},
        )
    print(f"Prepared {len(paths)} fixtures, {len(paths) * 2} EVEngine views")


def renderer_runtime_hashes(executable: Path) -> dict[str, str]:
    """Hash the executable and adjacent DLL search roots used by capture_example.ps1."""
    executable = executable.resolve()
    files = {executable.name.casefold(): executable}
    for directory in (executable.parent, executable.parent.parent.parent):
        for path in sorted(directory.glob("*.dll")):
            files.setdefault(path.name.casefold(), path)
    return {str(path): digest(path) for path in files.values()}


def render_ev(args: argparse.Namespace) -> None:
    root = args.root.resolve()
    if not args.eve.is_file():
        raise ValueError("The EVEngine renderer executable does not exist")
    runtime_hashes = renderer_runtime_hashes(args.eve)
    for path in sorted((root / "fixtures").glob("*/fixture.json")):
        value = load_fixture(path)
        if args.case and value["caseId"] not in args.case:
            continue
        for view in ("perspective", "top"):
            output = path.parent / f"ev-{view}.png"
            game = root / "games" / f"{value['caseId']}-{view}"
            if (game / "main.nut").read_bytes() != (TOOLS / "EvRender.nut").read_bytes():
                raise ValueError(f"Stale EVEngine render adapter for {value['caseId']}; run prepare")
            subprocess.run(
                ["pwsh", "-NoProfile", "-File", str(REPO / "scripts" / "capture_example.ps1"),
                 "-Game", str(root / "games" / f"{value['caseId']}-{view}"),
                 "-Out", str(output), "-Frames", "12", "-Eve", str(args.eve.resolve())],
                check=True, cwd=REPO,
            )
        if renderer_runtime_hashes(args.eve) != runtime_hashes:
            raise ValueError("EVEngine executable or runtime DLL changed during capture")
        write_json(
            path.parent / "ev-evidence.json",
            {"schema": "eve.tests.road-parity.evidence", "schemaVersion": 2,
             "rendererExecutable": str(args.eve.resolve()), "rendererRuntimeSha256": runtime_hashes,
             "fixtureSha256": digest(path), "rendererExeSha256": digest(args.eve),
             "meshOrigin": "current-source-native-fixture",
             **ev_adapter_hashes(), "gameInputSha256": ev_game_hashes(root, value["caseId"]),
             "images": {view: digest(path.parent / f"ev-{view}.png") for view in ("perspective", "top")},
             "renderedUtc": datetime.now(timezone.utc).isoformat()},
        )


def render_unity(args: argparse.Namespace) -> None:
    root = args.root.resolve()
    project = root / "unity-project"
    if not (project / "Assets" / "Editor" / "RoadParityCapture.cs").is_file():
        raise ValueError("Prepare the reference package first")
    shutil.copyfile(TOOLS / "UnityCapture.cs", project / "Assets" / "Editor" / "RoadParityCapture.cs")
    log = root / "unity-capture.log"
    command = [str(args.unity.resolve()), "-batchmode", "-quit", "-force-d3d11",
               "-projectPath", str(project), "-executeMethod", "RoadParityCapture.Run",
               "-roadParityRoot", str(root / "fixtures"), "-logFile", str(log)]
    completed = subprocess.run(command, cwd=REPO, check=False)
    text = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
    if completed.returncode != 0 or "ROAD_PARITY_REFERENCE_COMPLETE" not in text:
        if "No valid Unity Editor license found" in text:
            raise RuntimeError(f"Unity license activation is required; reference frames unavailable. Log: {log}")
        raise RuntimeError(f"Unity reference capture failed (exit {completed.returncode}); inspect {log}")
    for path in sorted((root / "fixtures").glob("*/fixture.json")):
        evidence = path.parent / "unity-evidence.json"
        record = json.loads(evidence.read_text(encoding="utf-8"))
        if record.get("fixtureSha256") != digest(path):
            raise ValueError(f"Unity capture used stale fixture data: {path}")
        record["images"] = {view: digest(path.parent / f"unity-{view}.png") for view in ("perspective", "top")}
        record["adapterSha256"] = digest(TOOLS / "UnityCapture.cs")
        record["projectionSha256"] = digest(path.parent / "unity-fixture.json")
        record["positionsSha256"] = digest(path.parent / "unity-positions.json")
        record["runtimeDllSha256"] = digest(project / "Assets/EasyRoads3D/lib/EasyRoads3Dv3.dll")
        write_json(evidence, record)


def surface_mask(path: Path, size: tuple[int, int]) -> list[bool]:
    from PIL import Image

    with Image.open(path) as image:
        if image.size != size:
            raise ValueError(f"Image dimensions differ from fixture: {path}")
        pixels = image.convert("RGB").tobytes()
        mask = [pixels[i] > 160 and pixels[i + 1] < 96 and pixels[i + 2] > 160
                for i in range(0, len(pixels), 3)]
    covered = sum(mask)
    if covered < 100 or covered >= len(mask) * 0.9:
        raise ValueError(f"Capture has an empty or implausible road surface mask: {path}")
    return mask


def compare(args: argparse.Namespace) -> int:
    from PIL import Image

    root = args.root.resolve()
    findings = []
    for path in sorted((root / "fixtures").glob("*/fixture.json")):
        spec = load_fixture(path)
        row = {"caseId": spec["caseId"], "fixtureSha256": digest(path), "views": []}
        for side in ("ev", "unity"):
            evidence = path.parent / f"{side}-evidence.json"
            if not evidence.is_file():
                row["status"] = "blocked"
                row["reason"] = f"Missing genuine {side} render evidence"
                break
            record = json.loads(evidence.read_text(encoding="utf-8"))
            if record.get("fixtureSha256") != row["fixtureSha256"]:
                raise ValueError(f"Stale {side} frame evidence for {spec['caseId']}")
            if side == "ev":
                version = record.get("schemaVersion", 1)
                if type(version) is not int or version not in (1, 2):
                    raise ValueError(f"Unsupported EVEngine evidence version for {spec['caseId']}")
                if version == 2:
                    executable = Path(record.get("rendererExecutable", ""))
                    if not executable.is_file() or record.get("rendererRuntimeSha256") != renderer_runtime_hashes(executable):
                        raise ValueError(f"Changed EVEngine runtime dependency for {spec['caseId']}")
                if any(record.get(key) != value for key, value in ev_adapter_hashes().items()):
                    raise ValueError(f"Stale EVEngine capture or render adapter for {spec['caseId']}")
                try:
                    game_hashes = ev_game_hashes(root, spec["caseId"])
                except OSError as error:
                    raise ValueError(f"Missing EVEngine game input for {spec['caseId']}") from error
                if record.get("gameInputSha256") != game_hashes:
                    raise ValueError(f"Stale EVEngine game input for {spec['caseId']}")
            if side == "unity" and (path.parent / "unity-fixture.json").is_file():
                if (record.get("adapterSha256") != digest(TOOLS / "UnityCapture.cs") or
                        record.get("projectionSha256") != digest(path.parent / "unity-fixture.json")):
                    raise ValueError(f"Stale Unity coordinate projection or adapter for {spec['caseId']}")
            if side == "unity" and (path.parent / "unity-positions.json").is_file():
                if record.get("positionsSha256") != digest(path.parent / "unity-positions.json"):
                    raise ValueError(f"Missing or changed Unity reference positions for {spec['caseId']}")
            runtime_dll = root / "unity-project/Assets/EasyRoads3D/lib/EasyRoads3Dv3.dll"
            if side == "unity" and runtime_dll.is_file() and record.get("runtimeDllSha256") != digest(runtime_dll):
                raise ValueError(f"Changed Unity reference runtime for {spec['caseId']}")
            for view in ("perspective", "top"):
                image = path.parent / f"{side}-{view}.png"
                if not image.is_file() or record.get("images", {}).get(view) != digest(image):
                    raise ValueError(f"Missing or changed {side} frame for {spec['caseId']} ({view})")
        else:
            row["status"] = "passed"
            size = (spec["width"], spec["height"])
            for view in ("perspective", "top"):
                ev = surface_mask(path.parent / f"ev-{view}.png", size)
                unity = surface_mask(path.parent / f"unity-{view}.png", size)
                intersection = sum(a and b for a, b in zip(ev, unity, strict=True))
                union = sum(a or b for a, b in zip(ev, unity, strict=True))
                iou = intersection / union
                pixels = [(210, 210, 210) if a and b else (30, 170, 240) if a else
                          (240, 140, 30) if b else (32, 32, 32)
                          for a, b in zip(ev, unity, strict=True)]
                overlay = Image.new("RGB", size)
                overlay.putdata(pixels)
                overlay.save(path.parent / f"difference-{view}.png")
                row["views"].append({"view": view, "surfaceIoU": iou,
                                     "candidatePixels": sum(ev), "referencePixels": sum(unity)})
                if iou < args.minimum_iou:
                    row["status"] = "different"
        findings.append(row)
    if not findings:
        raise ValueError("No fixtures to compare")
    result = {"schema": "eve.tests.road-parity.comparison", "schemaVersion": 1,
              "scope": "surface silhouette only; not terrain, traffic, UV or material parity",
              "minimumIoU": args.minimum_iou,
              "overlayLegend": {"shared": "grey", "EVEngineOnly": "blue", "EasyRoadsOnly": "orange"},
              "cases": findings}
    write_json(root / "comparison.json", result)
    for row in findings:
        print(f"{row['caseId']}: {row['status']} {row.get('reason', row['views'])}")
    return 0 if all(row["status"] == "passed" for row in findings) else 2


def probe_unity_contracts(args: argparse.Namespace) -> None:
    """Collect real reference API/mesh facts without changing silhouette capture evidence."""
    root = args.root.resolve()
    project = root / "unity-project"
    editor = project / "Assets/Editor"
    runtime = project / "Assets/EasyRoads3D/lib/EasyRoads3Dv3.dll"
    if not editor.is_dir() or not runtime.is_file():
        raise ValueError("Prepare the reference package first")
    adapter = TOOLS / "UnityRoadContractProbe.cs"
    shutil.copyfile(adapter, editor / adapter.name)
    log = root / "unity-road-contracts.log"
    with tempfile.TemporaryDirectory(prefix="road-contract-", dir=root) as staging:
        output = Path(staging) / "contracts.json"
        command = [str(args.unity.resolve()), "-batchmode", "-quit", "-force-d3d11",
                   "-projectPath", str(project), "-executeMethod", "UnityRoadContractProbe.Run",
                   "-roadContractOutput", str(output), "-logFile", str(log)]
        completed = subprocess.run(command, cwd=REPO, check=False)
        text = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
        if completed.returncode != 0 or "ROAD_CONTRACT_PROBE_COMPLETE" not in text or not output.is_file():
            raise RuntimeError(f"Unity road contract probe failed (exit {completed.returncode}); inspect {log}")
        report = json.loads(output.read_text(encoding="utf-8"))
        if report.get("schema") != "eve.tests.easyroads-path-contracts" or report.get("schemaVersion") != 1:
            raise ValueError("Unsupported Unity road contract report")
        cases = report.get("cases", [])
        identities = {case["caseId"] for case in cases}
        required = {"control-Spline", "control-StraightXZ", "control-StraightXZY", "control-Circular",
                    "default-spline", "variable-bank", "bank-pivot-left", "bank-pivot-right"}
        if not required <= identities or len(identities) != len(cases):
            raise ValueError("Missing or duplicate Unity road contract cases")
        if any(not case.get("meshes") or len(case.get("centerline", [])) != 145 for case in cases):
            raise ValueError("Incomplete Unity road contract geometry")
        destination = root / "unity-road-contracts.json"
        shutil.copyfile(output, destination)
        write_json(root / "unity-road-contracts-evidence.json", {
            "schema": "eve.tests.easyroads-path-contracts.evidence", "schemaVersion": 1,
            "adapterSha256": digest(adapter), "reportSha256": digest(destination),
            "runtimeDllSha256": digest(runtime), "unityExeSha256": digest(args.unity.resolve()),
            "cases": sorted(identities), "scope": "actual API facts and generated geometry; no rendering parity claim",
        })
        print(f"Unity road contracts: {len(cases)} real cases exported to {destination}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=DEFAULT_ROOT)
    commands = parser.add_subparsers(dest="command", required=True)
    setup = commands.add_parser("prepare")
    setup.add_argument("--package", type=Path)
    ev = commands.add_parser("render-ev")
    ev.add_argument("--eve", type=Path, required=True)
    ev.add_argument("--case", action="append", choices=("straight", "curve", "slope", "bridge", "tee", "cross",
                                                       "width-profile", "banked", "s-curve", "crest", "valley",
                                                       "curved-uphill", "variable-bank", "negative-bank", "full-roll-positive", "full-roll-negative", "bank-pivot-left", "bank-pivot-right", "bank-pivot-varying",
                                                       "curve-width", "curve-width-bank", "strength-quarter", "strength-full", "control-xz", "control-xyz", "control-circular", "control-circular-mirror", "control-circular-zero-bump", "control-circular-mirror-zero-bump", "sampling-resolution-1", "sampling-resolution-10", "sampling-angle-1", "sampling-angle-20", "sampling-transverse-3", "sampling-created-default"))
    unity = commands.add_parser("render-unity")
    unity.add_argument("--unity", type=Path, required=True)
    probe = commands.add_parser("probe-unity-contracts")
    probe.add_argument("--unity", type=Path, required=True)
    comparison = commands.add_parser("compare")
    comparison.add_argument("--minimum-iou", type=float, default=0.98)
    args = parser.parse_args()
    if args.command == "compare" and not 0 < args.minimum_iou <= 1:
        parser.error("--minimum-iou must be in (0,1]")
    if args.command == "prepare":
        prepare(args)
    elif args.command == "render-ev":
        render_ev(args)
    elif args.command == "render-unity":
        render_unity(args)
    elif args.command == "probe-unity-contracts":
        probe_unity_contracts(args)
    else:
        return compare(args)
    return 0


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    try:
        raise SystemExit(main())
    except (ValueError, RuntimeError, OSError, subprocess.CalledProcessError) as error:
        print(f"road parity: {error}")
        raise SystemExit(1) from error
