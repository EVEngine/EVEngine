"""Converter dispatch: runs a `script` or a `nodegroup` converter against a mesh."""

from __future__ import annotations

import json
import os
from typing import Dict, Optional

from .. import mesh_ops


class ConverterError(RuntimeError):
    pass


def load_manifest(converter_dir: str, converter_id: str) -> Dict[str, object]:
    entry = os.path.join(converter_dir, converter_id)
    manifest_path = os.path.join(entry, "manifest.json")
    if not os.path.isfile(manifest_path):
        raise ConverterError(f"converter '{converter_id}': manifest.json not found in '{entry}'")
    with open(manifest_path, "r", encoding="utf-8") as fh:
        manifest = json.load(fh)
    manifest["_dir"] = entry
    return manifest


def _merge_defaults(manifest: Dict[str, object], params: Dict[str, object]) -> Dict[str, object]:
    merged: Dict[str, object] = dict(params)
    for spec in manifest.get("params", []) or []:
        key = spec.get("key", "")
        if key and key not in merged:
            merged[key] = spec.get("default", "")
    return merged


class ConvertContext:
    """Passed to `convert(ctx)` for script converters."""

    def __init__(self, obj, manifest: Dict[str, object], params: Dict[str, object]) -> None:
        self.obj = obj
        self.manifest = manifest
        self.params = params
        self.mesh_ops = mesh_ops

    def run_py(self, source: str, globals_dict: Optional[Dict[str, object]] = None) -> None:
        """Execute an inline Python snippet in the converter's namespace."""
        scope = {"bpy": mesh_ops.bpy, "ctx": self, "obj": self.obj, "params": self.params,
                 "mesh_ops": mesh_ops}
        if globals_dict:
            scope.update(globals_dict)
        exec(source, scope)  # noqa: S102 - explicitly scripting converters

    def apply_nodegroup(self, blend_path: str, group_name: str) -> None:
        """Append a Geometry Node group from a .blend and bind it to self.obj."""
        import bpy
        if not os.path.isfile(blend_path):
            raise ConverterError(f"node group blend not found: {blend_path}")
        if not group_name:
            raise ConverterError("apply_nodegroup: empty group_name")

        if bpy.context.mode != "OBJECT":
            bpy.ops.object.mode_set(mode="OBJECT")
        bpy.ops.wm.append(
            filename=group_name,
            directory=os.path.join(blend_path, "NodeTree"),
            link=False,
        )
        group = bpy.data.node_groups.get(group_name)
        if group is None:
            raise ConverterError(f"node group '{group_name}' not found in '{blend_path}'")

        modifier = self.obj.modifiers.new(name="EVE_GeometryNodes", type="NODES")
        modifier.node_group = group


def run_converter(manifest: Dict[str, object], obj, params: Dict[str, object]) -> None:
    kind = manifest.get("kind", "script")
    entry_dir = str(manifest.get("_dir", ""))
    merged = _merge_defaults(manifest, params)
    ctx = ConvertContext(obj, manifest, merged)

    if kind == "script":
        entry = str(manifest.get("entry", "convert.py"))
        script_path = os.path.join(entry_dir, entry)
        if not os.path.isfile(script_path):
            raise ConverterError(f"converter script not found: {script_path}")
        scope = {"bpy": mesh_ops.bpy, "ctx": ctx, "obj": obj,
                 "params": merged, "mesh_ops": mesh_ops}
        with open(script_path, "r", encoding="utf-8") as fh:
            source = fh.read()
        exec(compile(source, script_path, "exec"), scope)  # noqa: S102
        convert = scope.get("convert")
        if convert is None:
            raise ConverterError(f"script '{script_path}' must define convert(ctx)")
        convert(ctx)
    elif kind == "nodegroup":
        blend = str(manifest.get("blend", ""))
        group = str(manifest.get("group", ""))
        blend_path = os.path.join(entry_dir, blend)
        ctx.apply_nodegroup(blend_path, group)
    else:
        raise ConverterError(f"unknown converter kind '{kind}' (use script|nodegroup)")
