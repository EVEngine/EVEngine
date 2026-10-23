#!/usr/bin/env python3
"""Export project-owned Unreal assets through Unreal Editor's glTF exporter."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import secrets
import shutil
import struct
import subprocess
import sys
import urllib.parse
from contextlib import contextmanager
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Sequence


REQUEST_SCHEMA = "eve.unreal-asset-export-request/1"
RESULT_SCHEMA = "eve.unreal-asset-export-result/1"
MANIFEST_SCHEMA = "eve.unreal-animation-conversion/1"
ASSET_MANIFEST_SCHEMA = "eve.unreal-asset-conversion/1"
TOOL_VERSION = "1.2.0"
_ASSET_RE = re.compile(r"^/[A-Za-z0-9_.-]+(?:/[A-Za-z0-9_.-]+)+$")


class ConversionError(RuntimeError):
    """A user-facing conversion failure with no partially published output."""


@dataclass(frozen=True)
class ConversionConfig:
    project: Path
    assets: tuple[str, ...]
    output: Path
    output_format: str
    unreal_editor: Path
    rights_confirmed: bool
    timeout_seconds: int
    texture_size: int = 1024
    preserve_path_names: bool = False


def _version_key(path: Path) -> tuple[int, ...]:
    numbers = re.findall(r"\d+", path.as_posix())
    return tuple(int(value) for value in numbers[-3:])


def find_unreal_editor(explicit: Path | None = None) -> Path:
    """Locate UnrealEditor-Cmd without changing the user's project or engine."""
    candidates: list[Path] = []
    if explicit is not None:
        candidates.append(explicit.expanduser())
    for variable in ("UNREAL_EDITOR_CMD", "UE_EDITOR_CMD"):
        value = os.environ.get(variable)
        if value:
            candidates.append(Path(value).expanduser())
    ue_root = os.environ.get("UE_ROOT")
    if ue_root:
        root = Path(ue_root).expanduser()
        candidates.extend(
            [
                root / "Engine/Binaries/Win64/UnrealEditor-Cmd.exe",
                root / "Engine/Binaries/Linux/UnrealEditor-Cmd",
                root / "Engine/Binaries/Mac/UnrealEditor-Cmd",
            ]
        )
    if os.name == "nt":
        install_root = Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "Epic Games"
        candidates.extend(
            sorted(
                install_root.glob("UE_*/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"),
                key=_version_key,
                reverse=True,
            )
        )
    else:
        candidates.extend(
            [
                Path("/opt/UnrealEngine/Engine/Binaries/Linux/UnrealEditor-Cmd"),
                Path("/Applications/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd"),
            ]
        )
    for candidate in candidates:
        resolved = candidate.resolve()
        if resolved.is_file():
            return resolved
    raise ConversionError(
        "UnrealEditor-Cmd was not found; pass --unreal-editor or set UNREAL_EDITOR_CMD"
    )


def normalize_asset_reference(value: str) -> str:
    normalized = value.strip().replace("\\", "/")
    if not _ASSET_RE.fullmatch(normalized) or ".." in normalized.split("/"):
        raise ConversionError(
            f"invalid asset reference '{value}'; expected /Game/Characters/Run "
            "or another mounted content root"
        )
    return normalized


def uasset_to_reference(project: Path, uasset: Path) -> str:
    content = (project.parent / "Content").resolve()
    source = uasset.expanduser().resolve()
    if source.suffix.lower() != ".uasset":
        raise ConversionError(f"{uasset}: expected a .uasset file")
    if not source.is_file():
        raise ConversionError(f"{uasset}: file does not exist")
    try:
        relative = source.relative_to(content)
    except ValueError as error:
        raise ConversionError(
            f"{uasset}: file must be under the project's Content directory; "
            "use --asset for plugin mounts"
        ) from error
    return normalize_asset_reference("/Game/" + relative.with_suffix("").as_posix())


def _artifact_name(asset: str, output_format: str, preserve_path: bool = False) -> str:
    if preserve_path:
        package = asset.split(".", 1)[0].strip("/")
        safe_path = re.sub(r"[^A-Za-z0-9_.-]", "_", package.replace("/", "__"))
        return f"{safe_path}.{output_format}"
    package_name = asset.rsplit("/", 1)[-1].split(".", 1)[0]
    safe_name = re.sub(r"[^A-Za-z0-9_.-]", "_", package_name)
    return f"{safe_name}.{output_format}"


def build_request(config: ConversionConfig, result_file: Path, staging_output: Path) -> dict:
    entries = []
    names: set[str] = set()
    for asset in config.assets:
        output_name = _artifact_name(asset, config.output_format, config.preserve_path_names)
        folded = output_name.casefold()
        if folded in names:
            raise ConversionError(
                f"multiple assets map to '{output_name}'; "
                "convert colliding package names separately"
            )
        names.add(folded)
        entries.append({"sourceAsset": asset, "outputFile": output_name})
    return {
        "schema": REQUEST_SCHEMA,
        "outputDirectory": str(staging_output.resolve()),
        "resultFile": str(result_file.resolve()),
        "format": config.output_format,
        "textureSize": config.texture_size,
        "assets": entries,
    }


def build_command(config: ConversionConfig, exporter_script: Path) -> list[str]:
    return [
        str(config.unreal_editor),
        str(config.project),
        "-run=pythonscript",
        f"-script={exporter_script.resolve()}",
        "-unattended",
        "-nop4",
        "-nosplash",
        "-AllowCommandletRendering",
        "-NoSound",
        "-DDC-ForceMemoryCache",
    ]


def _read_json_object(path: Path, description: str) -> dict:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ConversionError(f"cannot read {description} {path}: {error}") from error
    if not isinstance(value, dict):
        raise ConversionError(f"{description} {path} must contain a JSON object")
    return value


def normalize_glb_weights(path: Path) -> int:
    """Expand normalized integer weights for the current Model3D decoder.

    Operates only on unpublished staging artifacts. Preserves all influence sets
    and original buffer bytes; adds float streams before manifest hashing.
    """
    document = _validate_glb(path)
    indices = {index for mesh in document.get("meshes", [])
               for primitive in mesh.get("primitives", [])
               for name, index in primitive.get("attributes", {}).items()
               if name.startswith("WEIGHTS_")}
    accessors = document.get("accessors", [])
    if any(type(index) is not int or index < 0 or index >= len(accessors) for index in indices):
        raise ConversionError("weight accessor index is out of range")
    converted = [index for index in indices
                 if accessors[index].get("componentType") in (5121, 5123)]
    if not converted:
        return 0
    raw = path.read_bytes()
    chunks = []
    offset = 12
    while offset < len(raw):
        if offset + 8 > len(raw):
            raise ConversionError("truncated GLB chunk header")
        size, kind = struct.unpack_from("<I4s", raw, offset)
        if size % 4 or offset + 8 + size > len(raw):
            raise ConversionError("invalid GLB chunk length")
        chunks.append((kind, raw[offset + 8:offset + 8 + size]))
        offset += 8 + size
    if len(chunks) != 2 or chunks[0][0] != b"JSON" or chunks[1][0] != b"BIN\0":
        raise ConversionError("weight expansion requires a single embedded GLB buffer")
    buffers = document.get("buffers", [])
    if len(buffers) != 1 or "uri" in buffers[0]:
        raise ConversionError("weight expansion requires an embedded buffer")
    binary = bytearray(chunks[1][1])
    views = document.get("bufferViews", [])
    for index in sorted(converted):
        accessor = accessors[index]
        if accessor.get("type") != "VEC4" or accessor.get("normalized") is not True or "sparse" in accessor:
            raise ConversionError("weight expansion requires dense normalized VEC4 accessors")
        view_index = accessor.get("bufferView")
        if type(view_index) is not int or view_index < 0 or view_index >= len(views):
            raise ConversionError("weight buffer view index is out of range")
        view = views[view_index]
        width = 1 if accessor["componentType"] == 5121 else 2
        count = accessor["count"]
        stride = view.get("byteStride", 4 * width)
        start = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
        end = start + (count - 1) * stride + 4 * width
        if (view.get("buffer", 0) != 0 or count < 1 or stride < 4 * width
                or start < 0 or end > len(chunks[1][1])
                or end > view.get("byteOffset", 0) + view["byteLength"]):
            raise ConversionError("weight accessor exceeds its buffer view")
        expanded = bytearray()
        for vertex in range(count):
            values = struct.unpack_from("<4B" if width == 1 else "<4H", binary, start + vertex * stride)
            expanded.extend(struct.pack("<4f", *(value / (255 if width == 1 else 65535) for value in values)))
        binary.extend(b"\0" * (-len(binary) % 4))
        views.append({"buffer": 0, "byteOffset": len(binary), "byteLength": len(expanded)})
        binary.extend(expanded)
        accessor["bufferView"] = len(views) - 1
        accessor["byteOffset"] = 0
        accessor["componentType"] = 5126
        accessor.pop("normalized", None)
        # Quantized extrema describe integer storage, not the new float stream.
        accessor.pop("min", None)
        accessor.pop("max", None)
    buffers[0]["byteLength"] = len(binary)
    encoded = json.dumps(document, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    encoded += b" " * (-len(encoded) % 4)
    payload = (struct.pack("<I4s", len(encoded), b"JSON") + encoded
               + struct.pack("<I4s", len(binary), b"BIN\0") + binary)
    path.write_bytes(b"glTF" + struct.pack("<II", 2, 12 + len(payload)) + payload)
    return len(converted)


def _validate_glb(path: Path) -> dict:
    data = path.read_bytes()
    if len(data) < 12 or data[:4] != b"glTF":
        raise ConversionError(f"{path.name}: Unreal did not produce a valid GLB header")
    version = int.from_bytes(data[4:8], "little")
    declared_size = int.from_bytes(data[8:12], "little")
    if version != 2 or declared_size != len(data):
        raise ConversionError(
            f"{path.name}: invalid GLB version/length (version={version}, declared={declared_size})"
        )
    if len(data) < 20:
        raise ConversionError(f"{path.name}: GLB has no JSON chunk")
    chunk_size = int.from_bytes(data[12:16], "little")
    if data[16:20] != b"JSON" or 20 + chunk_size > len(data):
        raise ConversionError(f"{path.name}: invalid GLB JSON chunk")
    try:
        document = json.loads(data[20 : 20 + chunk_size].decode("utf-8").rstrip(" \t\r\n\x00"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ConversionError(f"{path.name}: invalid GLB JSON document: {error}") from error
    if not isinstance(document, dict):
        raise ConversionError(f"{path.name}: GLB JSON root must be an object")
    return document


def _validate_gltf(path: Path, staging_output: Path) -> tuple[dict, list[Path]]:
    document = _read_json_object(path, "glTF artifact")
    version = (
        document.get("asset", {}).get("version")
        if isinstance(document.get("asset"), dict)
        else None
    )
    if not isinstance(version, str) or not version.startswith("2"):
        raise ConversionError(f"{path.name}: expected glTF 2.x asset metadata")
    dependencies: list[Path] = []
    for collection_name in ("buffers", "images"):
        collection = document.get(collection_name, [])
        if not isinstance(collection, list):
            raise ConversionError(f"{path.name}: {collection_name} must be an array")
        for entry in collection:
            if not isinstance(entry, dict):
                raise ConversionError(f"{path.name}: invalid {collection_name} entry")
            uri = entry.get("uri")
            if uri is None or (isinstance(uri, str) and uri.startswith("data:")):
                continue
            if not isinstance(uri, str) or "\\" in uri:
                raise ConversionError(f"{path.name}: invalid sidecar URI {uri!r}")
            parsed = urllib.parse.urlsplit(uri)
            if parsed.scheme or parsed.netloc or parsed.query or parsed.fragment:
                raise ConversionError(f"{path.name}: external sidecar URI is not allowed: {uri}")
            relative = Path(urllib.parse.unquote(parsed.path))
            dependency = (path.parent / relative).resolve()
            try:
                dependency.relative_to(staging_output)
            except ValueError as error:
                raise ConversionError(
                    f"{path.name}: sidecar escapes output directory: {uri}"
                ) from error
            if not dependency.is_file() or dependency.is_symlink():
                raise ConversionError(f"{path.name}: missing or unsafe sidecar: {uri}")
            dependencies.append(dependency)
    return document, dependencies


def _validate_asset_content(document: dict, asset_class: str, path: Path) -> dict:
    asset = document.get("asset")
    version = asset.get("version") if isinstance(asset, dict) else None
    if not isinstance(version, str) or not version.startswith("2"):
        raise ConversionError(f"{path.name}: expected glTF 2.x asset metadata")
    collections: dict[str, list] = {}
    for name in ("meshes", "skins", "animations"):
        value = document.get(name, [])
        if not isinstance(value, list):
            raise ConversionError(f"{path.name}: {name} must be an array")
        collections[name] = value
    if not collections["meshes"]:
        raise ConversionError(f"{path.name}: exported {asset_class} has no mesh")
    if asset_class != "StaticMesh" and not collections["skins"]:
        raise ConversionError(f"{path.name}: exported {asset_class} has no skinned preview mesh")
    if asset_class == "AnimSequence" and not collections["animations"]:
        raise ConversionError(f"{path.name}: exported AnimSequence has no glTF animation")
    materials = document.get("materials", [])
    images = document.get("images", [])
    textures = document.get("textures", [])
    if not isinstance(materials, list) or not isinstance(images, list) or not isinstance(textures, list):
        raise ConversionError(f"{path.name}: materials, images, and textures must be arrays")
    primitive_count = 0
    primitives_without_material = 0
    referenced_materials: set[int] = set()
    for mesh in collections["meshes"]:
        if not isinstance(mesh, dict) or not isinstance(mesh.get("primitives", []), list):
            raise ConversionError(f"{path.name}: mesh primitives must be an array")
        for primitive in mesh.get("primitives", []):
            if not isinstance(primitive, dict):
                raise ConversionError(f"{path.name}: invalid mesh primitive")
            primitive_count += 1
            material_index = primitive.get("material")
            if material_index is None:
                primitives_without_material += 1
            elif type(material_index) is not int or not 0 <= material_index < len(materials):
                raise ConversionError(f"{path.name}: primitive material index is out of range")
            else:
                referenced_materials.add(material_index)
    texture_usage = {
        "baseColor": 0,
        "metallicRoughness": 0,
        "normal": 0,
        "occlusion": 0,
        "emissive": 0,
    }
    for material in materials:
        if not isinstance(material, dict):
            raise ConversionError(f"{path.name}: invalid material entry")
        pbr = material.get("pbrMetallicRoughness", {})
        if not isinstance(pbr, dict):
            raise ConversionError(f"{path.name}: pbrMetallicRoughness must be an object")
        texture_usage["baseColor"] += int("baseColorTexture" in pbr)
        texture_usage["metallicRoughness"] += int("metallicRoughnessTexture" in pbr)
        texture_usage["normal"] += int("normalTexture" in material)
        texture_usage["occlusion"] += int("occlusionTexture" in material)
        texture_usage["emissive"] += int("emissiveTexture" in material)
    animation_names = [
        entry.get("name", "")
        for entry in collections["animations"]
        if isinstance(entry, dict) and isinstance(entry.get("name", ""), str)
    ]
    return {
        "meshCount": len(collections["meshes"]),
        "skinCount": len(collections["skins"]),
        "animationCount": len(collections["animations"]),
        "animationNames": animation_names,
        "primitiveCount": primitive_count,
        "primitivesWithoutMaterial": primitives_without_material,
        "materialCount": len(materials),
        "referencedMaterialCount": len(referenced_materials),
        "textureCount": len(textures),
        "imageCount": len(images),
        "materialTextureUsage": texture_usage,
    }


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _validate_result(result: dict, request: dict, staging_output: Path) -> list[dict]:
    allowed = {"schema", "status", "engineVersion", "artifacts", "diagnostics"}
    unknown = sorted(set(result) - allowed)
    if unknown:
        raise ConversionError(f"Unreal result contains unknown fields: {', '.join(unknown)}")
    if result.get("schema") != RESULT_SCHEMA:
        raise ConversionError(f"unsupported Unreal result schema: {result.get('schema')!r}")
    if result.get("status") != "success":
        diagnostics = result.get("diagnostics", [])
        detail = "; ".join(str(item) for item in diagnostics) or "no diagnostic was returned"
        raise ConversionError(f"Unreal export failed: {detail}")
    raw_artifacts = result.get("artifacts")
    if not isinstance(raw_artifacts, list):
        raise ConversionError("Unreal result artifacts must be an array")
    expected = {entry["outputFile"]: entry["sourceAsset"] for entry in request["assets"]}
    observed: set[str] = set()
    claimed_files: set[Path] = set()
    validated: list[dict] = []
    for item in raw_artifacts:
        if not isinstance(item, dict) or set(item) != {"sourceAsset", "outputFile", "assetClass"}:
            raise ConversionError("Unreal returned an invalid artifact entry")
        output_name = item["outputFile"]
        if output_name not in expected or item["sourceAsset"] != expected[output_name]:
            raise ConversionError(f"Unreal returned an unexpected artifact '{output_name}'")
        if output_name in observed:
            raise ConversionError(f"Unreal returned duplicate artifact '{output_name}'")
        observed.add(output_name)
        artifact_path = staging_output / output_name
        if not artifact_path.is_file() or artifact_path.parent != staging_output:
            raise ConversionError(f"expected artifact was not created: {output_name}")
        if artifact_path.suffix.lower() == ".glb":
            expanded = normalize_glb_weights(artifact_path)
            if expanded:
                result.setdefault("diagnostics", []).append(
                    f"{output_name}: expanded {expanded} normalized integer weight streams to FLOAT for Model3D")
            document = _validate_glb(artifact_path)
            dependencies: list[Path] = []
        else:
            document, dependencies = _validate_gltf(artifact_path, staging_output)
        content = _validate_asset_content(document, item["assetClass"], artifact_path)
        claimed_files.add(artifact_path.resolve())
        claimed_files.update(dependencies)
        validated.append(
            {
                **item,
                "path": output_name,
                "bytes": artifact_path.stat().st_size,
                "sha256": _sha256(artifact_path),
                "content": content,
                "dependencies": [
                    {
                        "path": dependency.relative_to(staging_output).as_posix(),
                        "bytes": dependency.stat().st_size,
                        "sha256": _sha256(dependency),
                    }
                    for dependency in dependencies
                ],
            }
        )
    missing = sorted(set(expected) - observed)
    if missing:
        raise ConversionError(f"Unreal did not return requested artifacts: {', '.join(missing)}")
    actual_files = {path.resolve() for path in staging_output.rglob("*") if path.is_file()}
    unexpected = sorted(
        path.relative_to(staging_output).as_posix()
        for path in actual_files - claimed_files
    )
    if unexpected:
        raise ConversionError(f"Unreal created unreferenced output files: {', '.join(unexpected)}")
    return validated


def _tail(text: str, lines: int = 40) -> str:
    return "\n".join(text.splitlines()[-lines:])


@contextmanager
def publication_staging(parent: Path, label: str):
    """Stage on the destination volume with its inherited access permissions.

    Python 3.13+ makes Windows mode-0700 temporary directories creator-only.
    Renaming such a payload would preserve that ACL and make the exported game
    assets unreadable by the user's desktop account after sandbox conversion.
    Exclusive mkdir with a random name keeps collision handling atomic while
    using the same parent ACL/umask as an ordinary destination directory.
    """
    parent = parent.resolve()
    for _ in range(32):
        root = parent / f".{label}.staging-{secrets.token_hex(16)}"
        try:
            root.mkdir(mode=0o777)
            break
        except FileExistsError:
            continue
    else:
        raise ConversionError("could not allocate publication staging directory")
    try:
        yield root
    finally:
        if root.parent != parent or root.is_symlink():
            raise ConversionError("publication staging path changed during conversion")
        shutil.rmtree(root)


def convert(
    config: ConversionConfig,
    runner: Callable[..., subprocess.CompletedProcess[str]] = subprocess.run,
) -> Path:
    if not config.rights_confirmed:
        raise ConversionError(
            "conversion requires --rights-confirmed to confirm cross-engine conversion rights"
        )
    project = config.project.resolve()
    if not project.is_file() or project.suffix.lower() != ".uproject":
        raise ConversionError(f"{project}: expected an existing .uproject")
    if config.output.exists():
        raise ConversionError(f"output directory already exists: {config.output}")
    config.output.parent.mkdir(parents=True, exist_ok=True)
    exporter_script = Path(__file__).with_name("ue_export_assets.py")
    with publication_staging(config.output.parent, config.output.name) as temporary:
        root = Path(temporary)
        staging_output = root / "payload"
        staging_output.mkdir()
        result_file = root / "result.json"
        request_file = root / "request.json"
        request = build_request(config, result_file, staging_output)
        request_file.write_text(
            json.dumps(request, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
        )
        environment = os.environ.copy()
        environment["EVENGINE_UE_EXPORT_REQUEST"] = str(request_file)
        command = build_command(config, exporter_script)
        try:
            process = runner(
                command,
                check=False,
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace",
                env=environment,
                timeout=config.timeout_seconds,
            )
        except subprocess.TimeoutExpired as error:
            raise ConversionError(
                f"Unreal Editor timed out after {config.timeout_seconds} seconds"
            ) from error
        if not result_file.is_file():
            log = _tail((process.stdout or "") + "\n" + (process.stderr or ""))
            raise ConversionError(
                f"Unreal did not write a conversion result (exit={process.returncode}):\n{log}"
            )
        result = _read_json_object(result_file, "Unreal result")
        artifacts = _validate_result(result, request, staging_output)
        contains_static_mesh = any(item["assetClass"] == "StaticMesh" for item in artifacts)
        manifest = {
            "schema": ASSET_MANIFEST_SCHEMA if contains_static_mesh else MANIFEST_SCHEMA,
            "tool": {"name": "EVEngine Unreal uasset converter", "version": TOOL_VERSION},
            "source": {
                "project": project.name,
                "engineVersion": result.get("engineVersion", "unknown"),
            },
            "format": config.output_format,
            "unknownFieldPolicy": "ignore",
            "artifacts": artifacts,
            "diagnostics": result.get("diagnostics", []),
        }
        (staging_output / "conversion.manifest.json").write_text(
            json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
        )
        os.replace(staging_output, config.output)
    return config.output / "conversion.manifest.json"


def _config_from_args(args: argparse.Namespace) -> ConversionConfig:
    project = args.project.expanduser().resolve()
    assets = [normalize_asset_reference(value) for value in args.asset]
    for asset_list in args.asset_list:
        try:
            lines = asset_list.expanduser().read_text(encoding="utf-8").splitlines()
        except OSError as error:
            raise ConversionError(f"cannot read asset list {asset_list}: {error}") from error
        assets.extend(
            normalize_asset_reference(line)
            for line in lines
            if line.strip() and not line.lstrip().startswith("#")
        )
    for audit_path in args.asset_audit:
        audit = _read_json_object(audit_path.expanduser(), "Unreal asset audit")
        if audit.get("schema") != "eve.unreal-asset-audit/1" or not isinstance(audit.get("assets"), list):
            raise ConversionError(f"Unreal asset audit {audit_path} has an unsupported schema")
        assets.extend(
            normalize_asset_reference(entry["package"])
            for entry in audit["assets"]
            if isinstance(entry, dict) and entry.get("class") == "StaticMesh"
            and isinstance(entry.get("package"), str)
        )
    assets.extend(uasset_to_reference(project, value) for value in args.uasset)
    excluded = {normalize_asset_reference(value) for value in args.exclude_asset}
    assets = [asset for asset in assets if asset not in excluded]
    if not assets:
        raise ConversionError("at least one --asset or --uasset is required")
    if len(set(assets)) != len(assets):
        raise ConversionError("the same Unreal asset was requested more than once")
    editor = find_unreal_editor(args.unreal_editor)
    return ConversionConfig(
        project=project,
        assets=tuple(assets),
        output=args.output.expanduser().resolve(),
        output_format=args.format,
        unreal_editor=editor,
        rights_confirmed=args.rights_confirmed,
        timeout_seconds=args.timeout_seconds,
        texture_size=args.texture_size,
        preserve_path_names=args.preserve_path_names,
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Convert project-owned Unreal static, skeletal, and animation assets to glTF through Unreal Editor"
    )
    parser.add_argument("--project", required=True, type=Path, help="source .uproject")
    parser.add_argument(
        "--asset", action="append", default=[], help="Unreal asset reference such as /Game/Anim/Run"
    )
    parser.add_argument(
        "--asset-list", action="append", default=[], type=Path,
        help="UTF-8 file containing one Unreal asset reference per line",
    )
    parser.add_argument(
        "--asset-audit", action="append", default=[], type=Path,
        help="eve.unreal-asset-audit/1 JSON whose StaticMesh packages should be converted",
    )
    parser.add_argument(
        "--exclude-asset", action="append", default=[],
        help="asset reference to omit from a list or audit after a documented export failure",
    )
    parser.add_argument(
        "--uasset",
        action="append",
        default=[],
        type=Path,
        help=".uasset below the project's Content folder",
    )
    parser.add_argument("--output", required=True, type=Path, help="new output directory")
    parser.add_argument("--format", choices=("glb", "gltf"), default="glb")
    parser.add_argument("--unreal-editor", type=Path, help="path to UnrealEditor-Cmd")
    parser.add_argument("--timeout-seconds", type=int, default=900)
    parser.add_argument(
        "--texture-size", type=int, default=1024,
        help="square material bake size in pixels (64-4096)",
    )
    parser.add_argument(
        "--preserve-path-names", action="store_true",
        help="encode the mounted package path in output names to avoid basename collisions",
    )
    parser.add_argument(
        "--rights-confirmed",
        action="store_true",
        help="confirm that every input may be converted for use outside Unreal Engine",
    )
    parser.add_argument("--dry-run", action="store_true", help="print the request and command only")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        config = _config_from_args(args)
        if args.timeout_seconds <= 0:
            raise ConversionError("--timeout-seconds must be positive")
        if not 64 <= args.texture_size <= 4096:
            raise ConversionError("--texture-size must be from 64 to 4096")
        if args.dry_run:
            preview_root = config.output.parent / f".{config.output.name}.staging-preview"
            request = build_request(config, preview_root / "result.json", preview_root / "payload")
            preview = {
                "command": build_command(
                    config, Path(__file__).with_name("ue_export_assets.py")
                ),
                "request": request,
            }
            print(json.dumps(preview, ensure_ascii=False, indent=2))
            return 0
        manifest = convert(config)
    except ConversionError as error:
        print(f"unreal-uasset-converter: {error}", file=sys.stderr)
        return 1
    print(f"manifest: {manifest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
