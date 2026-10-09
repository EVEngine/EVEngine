"""Runs inside Unreal Editor and exports selected Texture2D assets as PNG files."""

from __future__ import annotations

import json
import os
import traceback
from pathlib import Path

import unreal


def main() -> None:
    raw = os.environ.get("EVENGINE_UE_TEXTURE_EXPORTS")
    report_output = os.environ.get("EVENGINE_UE_TEXTURE_REPORT")
    if not raw or not report_output:
        raise RuntimeError("texture export mapping and report path are required")
    exports = json.loads(raw)
    if not isinstance(exports, dict) or not exports:
        raise RuntimeError("texture export mapping must be a non-empty JSON object")
    records = []
    for asset_path, output_text in sorted(exports.items()):
        asset = unreal.EditorAssetLibrary.load_asset(asset_path)
        if asset is None or not isinstance(asset, unreal.Texture2D):
            raise RuntimeError(f"Texture2D asset could not be loaded: {asset_path}")
        output = Path(output_text).resolve()
        output.parent.mkdir(parents=True, exist_ok=True)
        task = unreal.AssetExportTask()
        task.object = asset
        task.filename = str(output)
        task.automated = True
        task.prompt = False
        task.replace_identical = True
        if not unreal.Exporter.run_asset_export_task(task) or not output.is_file():
            raise RuntimeError(f"Texture2D PNG export failed: {asset_path}")
        records.append(
            {
                "asset": asset_path,
                "path": str(output),
                "bytes": output.stat().st_size,
                "width": int(asset.blueprint_get_size_x()),
                "height": int(asset.blueprint_get_size_y()),
            }
        )
    report = {
        "schema": "eve.unreal-texture-export-audit/1",
        "engineVersion": unreal.SystemLibrary.get_engine_version(),
        "textures": records,
    }
    report_path = Path(report_output).resolve()
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )


try:
    main()
except Exception:
    unreal.log_error(traceback.format_exc())
    raise
