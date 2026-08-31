"""Runs inside Unreal Editor and exports a validated request to glTF/GLB."""

from __future__ import annotations

import json
import os
import traceback
from pathlib import Path

import unreal


REQUEST_SCHEMA = "eve.unreal-asset-export-request/1"
RESULT_SCHEMA = "eve.unreal-asset-export-result/1"
_TOP_LEVEL_FIELDS = {"schema", "outputDirectory", "resultFile", "format", "assets"}
_ASSET_FIELDS = {"sourceAsset", "outputFile"}


def _write_result(path: Path, result: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    os.replace(temporary, path)


def _load_request(path: Path) -> dict:
    request = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(request, dict) or set(request) != _TOP_LEVEL_FIELDS:
        raise ValueError("request fields do not match eve.unreal-asset-export-request/1")
    if request.get("schema") != REQUEST_SCHEMA:
        raise ValueError(f"unsupported request schema: {request.get('schema')!r}")
    if request.get("format") not in ("glb", "gltf"):
        raise ValueError("format must be glb or gltf")
    request_root = path.parent.resolve()
    if Path(request.get("outputDirectory", "")).resolve() != request_root / "payload":
        raise ValueError("outputDirectory must be the request's sibling payload directory")
    if Path(request.get("resultFile", "")).resolve() != request_root / "result.json":
        raise ValueError("resultFile must be the request's sibling result.json")
    assets = request.get("assets")
    if not isinstance(assets, list) or not assets:
        raise ValueError("assets must be a non-empty array")
    for entry in assets:
        if not isinstance(entry, dict) or set(entry) != _ASSET_FIELDS:
            raise ValueError("asset request has invalid fields")
        output_file = entry.get("outputFile")
        if not isinstance(output_file, str) or Path(output_file).name != output_file:
            raise ValueError("outputFile must be a basename")
        if Path(output_file).suffix.lower() != "." + request["format"]:
            raise ValueError("outputFile extension does not match format")
    return request


def _message_list(messages, property_name: str) -> list[str]:
    if messages is None:
        return []
    try:
        values = messages.get_editor_property(property_name)
    except Exception:
        values = getattr(messages, property_name, [])
    return [str(value) for value in values]


def _export(request: dict) -> dict:
    output_directory = Path(request["outputDirectory"]).resolve()
    output_directory.mkdir(parents=True, exist_ok=True)
    options = unreal.GLTFExportOptions()
    options.set_editor_property("export_vertex_skin_weights", True)
    options.set_editor_property("export_preview_mesh", True)
    options.set_editor_property("export_animation_sequences", True)
    artifacts = []
    diagnostics = []
    for entry in request["assets"]:
        source_asset = entry["sourceAsset"]
        asset = unreal.load_asset(source_asset)
        if asset is None:
            raise ValueError(f"asset does not exist or could not be loaded: {source_asset}")
        asset_class = asset.get_class().get_name()
        if asset_class not in ("AnimSequence", "SkeletalMesh"):
            raise ValueError(
                f"unsupported asset class {asset_class} for {source_asset}; "
                "expected AnimSequence or SkeletalMesh"
            )
        output_path = output_directory / entry["outputFile"]
        messages = unreal.GLTFExporter.export_to_gltf(asset, str(output_path), options, set())
        errors = _message_list(messages, "errors")
        warnings = _message_list(messages, "warnings")
        diagnostics.extend(f"{source_asset}: warning: {message}" for message in warnings)
        if errors:
            raise RuntimeError(f"{source_asset}: " + "; ".join(errors))
        if not output_path.is_file():
            raise RuntimeError(f"GLTFExporter did not create {output_path}")
        artifacts.append(
            {
                "sourceAsset": source_asset,
                "outputFile": entry["outputFile"],
                "assetClass": asset_class,
            }
        )
    return {
        "schema": RESULT_SCHEMA,
        "status": "success",
        "engineVersion": unreal.SystemLibrary.get_engine_version(),
        "artifacts": artifacts,
        "diagnostics": diagnostics,
    }


def main() -> None:
    request_value = os.environ.get("EVENGINE_UE_EXPORT_REQUEST")
    if not request_value:
        raise RuntimeError("EVENGINE_UE_EXPORT_REQUEST is not set")
    request_path = Path(request_value).resolve()
    result_path = request_path.with_name("result.json")
    try:
        request = _load_request(request_path)
        result_path = Path(request["resultFile"]).resolve()
        result = _export(request)
    except Exception as error:
        result = {
            "schema": RESULT_SCHEMA,
            "status": "failed",
            "engineVersion": unreal.SystemLibrary.get_engine_version(),
            "artifacts": [],
            "diagnostics": [str(error), traceback.format_exc()],
        }
    _write_result(result_path, result)


main()
