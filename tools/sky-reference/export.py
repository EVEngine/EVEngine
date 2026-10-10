#!/usr/bin/env python3
"""Export sky source assets with Unreal, validate them, then publish atomically."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
import uuid
from dataclasses import asdict
from pathlib import Path

from dds import decode_dds


MANIFEST_SCHEMA = "eve.sky-source-package/1"
RESULT_SCHEMA = "eve.sky-source-export-result/1"


def validate_payload(payload: Path, result: dict) -> dict:
    """Check all files before making this source package visible to consumers."""
    if result.get("schema") != RESULT_SCHEMA or result.get("status") != "success":
        raise ValueError("Unreal export failed: " + str(result.get("diagnostics", [])))
    entries = result.get("artifacts")
    if not isinstance(entries, list) or not entries:
        raise ValueError("export returned no artifacts")
    root, seen, textures = payload.resolve(), set(), []
    for entry in entries:
        name = entry.get("file")
        if not isinstance(name, str) or "\\" in name or ":" in name or any(
            part in ("", ".", "..") for part in name.split("/")
        ):
            raise ValueError("invalid artifact file name")
        if any(parent.is_symlink() for parent in (root / name).parents if parent != root and parent.is_relative_to(root)) or (root / name).is_symlink():
            raise ValueError("artifact symlinks are not allowed")
        candidate = (root / name).resolve()
        if not candidate.is_relative_to(root) or candidate == root or candidate.is_symlink():
            raise ValueError("artifact path escapes payload")
        key = name.casefold()
        if key in seen:
            raise ValueError("duplicate artifact file name")
        seen.add(key)
        if not candidate.is_file() or candidate.stat().st_size != entry.get("byteLength"):
            raise ValueError("missing artifact or wrong length: " + name)
        if candidate.stat().st_size > 512 * 1024 * 1024:
            raise ValueError("artifact exceeds 512 MiB limit: " + name)
        source = candidate.read_bytes()
        if hashlib.sha256(source).hexdigest() != entry.get("sha256"):
            raise ValueError("artifact hash mismatch: " + name)
        if candidate.suffix == ".dds":
            texture = decode_dds(source)
            metadata = asdict(texture)
            metadata.pop("pixels")
            # DDS stores source samples. Unreal's sRGB flag, not a filename,
            # determines their interpretation by the imported sampler.
            source_srgb = entry.get("properties", {}).get("srgb")
            if not isinstance(source_srgb, bool):
                raise ValueError("texture is missing explicit sRGB metadata: " + name)
            if texture.encoding in {"rgba16f", "rgba32f"} and source_srgb:
                raise ValueError("HDR source must be linear: " + name)
            metadata["srgb"] = source_srgb
            metadata["source"] = entry["source"]
            metadata["sourceFile"] = name
            metadata["sampler"] = {k: v for k, v in entry["properties"].items()
                                   if k in {"filter", "address_x", "address_y", "address_z"}}
            raw_relative = "decoded/" + name.removesuffix(".dds") + "." + texture.encoding
            raw_path = root / raw_relative
            raw_path.parent.mkdir(parents=True, exist_ok=True)
            raw_path.write_bytes(texture.pixels)
            metadata["file"] = raw_relative
            metadata["sha256"] = hashlib.sha256(texture.pixels).hexdigest()
            metadata["byteLength"] = len(texture.pixels)
            textures.append(metadata)
        elif candidate.suffix != ".t3d":
            raise ValueError("unexpected exported file type: " + name)
    manifest = {"schema": MANIFEST_SCHEMA, "engine": result["engine"], "sourceMount": result["mount"],
                "artifacts": entries, "textures": textures, "inventory": result["inventory"]}
    (root / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return manifest


def export_package(project: Path, editor: Path, output: Path, timeout: int, mount: str) -> Path:
    """Keep failed staging and its log for diagnosis; never overwrite a package."""
    project, editor, output = project.resolve(), editor.resolve(), output.resolve()
    if not project.is_file() or project.suffix != ".uproject" or not editor.is_file():
        raise ValueError("project and Unreal editor must exist")
    if output.exists() or timeout <= 0:
        raise ValueError("output must not exist and timeout must be positive")
    output.parent.mkdir(parents=True, exist_ok=True)
    staging = output.parent / ("." + output.name + ".staging-" + uuid.uuid4().hex)
    staging.mkdir()
    request = {"schema": "eve.sky-source-export-request/1", "mount": mount,
               "blueprints": [mount + "/Blueprints/Ultra_Dynamic_Sky",
                              mount + "/Blueprints/Ultra_Dynamic_Weather"]}
    request_path = staging / "request.json"
    request_path.write_text(json.dumps(request, indent=2) + "\n", encoding="utf-8")
    env = os.environ.copy()
    env["EVENGINE_SKY_EXPORT_REQUEST"] = str(request_path)
    # Both caches are project-local: no writes into the user's shared UE cache.
    cache = project.parent / "DerivedDataCache"
    shader_work = project.parent / "ShaderWorking"
    command = [str(editor), str(project), "-run=pythonscript",
               "-script=" + str(Path(__file__).with_name("ue_export.py").resolve()),
               "-unattended", "-nop4", "-nosplash", "-NullRHI", "-NoSound",
               "-DDC=(Local(Path=" + str(cache) + "))", "-shaderworkingdir=" + str(shader_work),
               "-abslog=" + str(staging / "unreal.log")]
    with (staging / "process.log").open("w", encoding="utf-8") as log:
        process = subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT,
                                 timeout=timeout, check=False,
                                 creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
    if process.returncode:
        raise RuntimeError(f"Unreal exited {process.returncode}; inspect {staging / 'unreal.log'}")
    result = json.loads((staging / "result.json").read_text(encoding="utf-8"))
    validate_payload(staging / "payload", result)
    if output.exists():
        raise ValueError("output appeared during export; publication refused")
    (staging / "payload").rename(output)
    return output / "manifest.json"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--editor", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--mount", default="/Game/UltraDynamicSky")
    parser.add_argument("--timeout", type=int, default=1800)
    args = parser.parse_args()
    try:
        manifest = export_package(args.project, args.editor, args.output, args.timeout, args.mount)
    except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as error:
        print(f"sky-reference export: {error}", file=sys.stderr)
        return 1
    print(manifest)
    return 0


if __name__ == "__main__":
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8")
    raise SystemExit(main())
