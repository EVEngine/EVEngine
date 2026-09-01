#!/usr/bin/env python3
"""Export project-owned Unreal assets through Unreal Editor's glTF exporter."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
import urllib.parse
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Sequence


REQUEST_SCHEMA = "eve.unreal-asset-export-request/1"
RESULT_SCHEMA = "eve.unreal-asset-export-result/1"
MANIFEST_SCHEMA = "eve.unreal-animation-conversion/1"
TOOL_VERSION = "1.0.0"
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


def _artifact_name(asset: str, output_format: str) -> str:
    package_name = asset.rsplit("/", 1)[-1].split(".", 1)[0]
    safe_name = re.sub(r"[^A-Za-z0-9_.-]", "_", package_name)
    return f"{safe_name}.{output_format}"


def build_request(config: ConversionConfig, result_file: Path, staging_output: Path) -> dict:
    entries = []
    names: set[str] = set()
    for asset in config.assets:
        output_name = _artifact_name(asset, config.output_format)
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
        "-nullrhi",
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


def _validate_animation_content(document: dict, asset_class: str, path: Path) -> dict:
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
    if not collections["meshes"] or not collections["skins"]:
        raise ConversionError(f"{path.name}: exported {asset_class} has no skinned preview mesh")
    if asset_class == "AnimSequence" and not collections["animations"]:
        raise ConversionError(f"{path.name}: exported AnimSequence has no glTF animation")
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
            document = _validate_glb(artifact_path)
            dependencies: list[Path] = []
        else:
            document, dependencies = _validate_gltf(artifact_path, staging_output)
        content = _validate_animation_content(document, item["assetClass"], artifact_path)
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
    with tempfile.TemporaryDirectory(
        prefix=f".{config.output.name}.staging-", dir=config.output.parent
    ) as temporary:
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
        manifest = {
            "schema": MANIFEST_SCHEMA,
            "tool": {"name": "EVEngine Unreal uasset converter", "version": TOOL_VERSION},
            "source": {
                "project": project.name,
                "engineVersion": result.get("engineVersion", "unknown"),
            },
            "format": config.output_format,
            "unknownFieldPolicy": "ignore",
            "artifacts": artifacts,
        }
        (staging_output / "conversion.manifest.json").write_text(
            json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
        )
        os.replace(staging_output, config.output)
    return config.output / "conversion.manifest.json"


def _config_from_args(args: argparse.Namespace) -> ConversionConfig:
    project = args.project.expanduser().resolve()
    assets = [normalize_asset_reference(value) for value in args.asset]
    assets.extend(uasset_to_reference(project, value) for value in args.uasset)
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
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Convert project-owned Unreal animation assets to glTF through Unreal Editor"
    )
    parser.add_argument("--project", required=True, type=Path, help="source .uproject")
    parser.add_argument(
        "--asset", action="append", default=[], help="Unreal asset reference such as /Game/Anim/Run"
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
