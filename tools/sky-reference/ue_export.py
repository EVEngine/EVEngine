"""Unreal Editor entry point: source sky assets to an unpublished staging folder."""

from __future__ import annotations

import hashlib
import json
import os
import traceback
from pathlib import Path

import unreal


REQUEST_SCHEMA = "eve.sky-source-export-request/1"
RESULT_SCHEMA = "eve.sky-source-export-result/1"
TEXT_CLASSES = {"Material", "MaterialFunction", "MaterialInstanceConstant",
                "MaterialParameterCollection", "CurveFloat", "CurveLinearColor", "CurveVector"}
TEXTURE_CLASSES = {"Texture2D", "VolumeTexture", "TextureCube"}


def export_object(asset, output: Path, exporter) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    task = unreal.AssetExportTask()
    task.object, task.filename, task.exporter = asset, str(output), exporter
    task.automated, task.prompt, task.replace_identical = True, False, True
    if not unreal.Exporter.run_asset_export_task(task) or not output.is_file():
        raise RuntimeError(f"export failed for {asset.get_path_name()}: {list(task.errors)}")


def export(request: dict, staging: Path) -> dict:
    if set(request) != {"schema", "mount", "blueprints"} or request["schema"] != REQUEST_SCHEMA:
        raise ValueError("unrecognized sky export request")
    mount = request["mount"]
    if not isinstance(mount, str) or not mount.startswith("/Game/") or ".." in mount:
        raise ValueError("mount must be a /Game content directory")
    if not isinstance(request["blueprints"], list) or any(
        not isinstance(p, str) or not p.startswith(mount + "/") or ".." in p
        for p in request["blueprints"]
    ):
        raise ValueError("blueprints must be content paths within mount")
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.scan_paths_synchronous([mount], force_rescan=True)
    assets = sorted(registry.get_assets_by_path(mount, recursive=True), key=lambda a: str(a.package_name))
    payload = staging / "payload"
    payload.mkdir()
    artifacts, inventory = [], []
    for data in assets:
        source = str(data.package_name)
        kind = str(data.asset_class_path.asset_name)
        inventory.append({"source": source, "class": kind})
        if kind not in TEXTURE_CLASSES | TEXT_CLASSES and source not in request["blueprints"]:
            continue
        asset = unreal.load_asset(source)
        if asset is None:
            raise RuntimeError("could not load " + source)
        relative = source.removeprefix(mount + "/")
        if source == relative or any(part in ("", ".", "..") for part in relative.split("/")):
            raise ValueError("asset path escaped mount")
        extension = ".dds" if kind in TEXTURE_CLASSES else ".t3d"
        target = payload / (relative + extension)
        exporter = unreal.TextureExporterDDS() if extension == ".dds" else unreal.ObjectExporterT3D()
        export_object(asset, target, exporter)
        properties = {}
        if kind in TEXTURE_CLASSES:
            # These are optional reflected fields; omissions are recorded explicitly.
            for name in ("srgb", "compression_settings", "filter", "address_x", "address_y", "address_z"):
                try:
                    value = asset.get_editor_property(name)
                    properties[name] = value if isinstance(value, (bool, int, float, str)) else str(value)
                except Exception:
                    properties[name] = None
        content = target.read_bytes()
        artifacts.append({"source": source, "class": kind, "file": target.relative_to(payload).as_posix(),
                          "sha256": hashlib.sha256(content).hexdigest(), "byteLength": len(content),
                          "properties": properties})
        if source in request["blueprints"]:
            generated = unreal.load_class(None, source + "." + source.rsplit("/", 1)[-1] + "_C")
            if generated is None:
                raise RuntimeError("missing generated class for " + source)
            target = payload / (relative + ".defaults.t3d")
            export_object(unreal.get_default_object(generated), target, unreal.ObjectExporterT3D())
            content = target.read_bytes()
            artifacts.append({"source": source, "class": "ClassDefaults", "file": target.relative_to(payload).as_posix(),
                              "sha256": hashlib.sha256(content).hexdigest(), "byteLength": len(content), "properties": {}})
        unreal.log("SKY_EXPORT " + source)
    if not artifacts:
        raise RuntimeError("no supported assets found under " + mount)
    return {"schema": RESULT_SCHEMA, "status": "success", "engine": unreal.SystemLibrary.get_engine_version(),
            "mount": mount, "inventory": inventory, "artifacts": artifacts, "diagnostics": []}


def main() -> None:
    request_path = Path(os.environ["EVENGINE_SKY_EXPORT_REQUEST"]).resolve()
    try:
        request = json.loads(request_path.read_text(encoding="utf-8"))
        result = export(request, request_path.parent)
    except Exception:
        result = {"schema": RESULT_SCHEMA, "status": "failed", "diagnostics": [traceback.format_exc()]}
    temporary = request_path.with_name("result.json.tmp")
    temporary.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    os.replace(temporary, request_path.with_name("result.json"))
    if result["status"] != "success":
        raise RuntimeError(result["diagnostics"][0])


if __name__ == "__main__":
    main()
