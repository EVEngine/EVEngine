#!/usr/bin/env python3
"""EVEngine AI asset-kit catalog: list / get / validate.

Source of truth: cmake/sdk/asset-kits/catalog.json (installed as
share/eve/ai/asset-kits/). Agents preparing art for card or visual-novel games
must read these pixel contracts instead of inventing Unity/Godot sizes.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CATALOG = ROOT / "cmake" / "sdk" / "asset-kits" / "catalog.json"
DEFAULT_SCHEMA = ROOT / "cmake" / "sdk" / "asset-kits" / "schema.json"

SCHEMA_ID = "eve.asset-kit.catalog"
SCHEMA_VERSION = 1


def catalog_path_from_env() -> Path | None:
    import os

    raw = os.environ.get("EVE_ASSET_KITS_DIR", "").strip()
    if not raw:
        return None
    path = Path(raw)
    if path.is_dir():
        candidate = path / "catalog.json"
        return candidate if candidate.is_file() else None
    if path.is_file():
        return path
    return None


def resolve_catalog(explicit: Path | None = None) -> Path:
    if explicit is not None:
        return explicit
    env = catalog_path_from_env()
    if env is not None:
        return env
    return DEFAULT_CATALOG


def load_catalog(path: Path | None = None) -> dict[str, Any]:
    catalog_file = resolve_catalog(path)
    text = catalog_file.read_text(encoding="utf-8")
    data = json.loads(text)
    if not isinstance(data, dict):
        raise ValueError(f"catalog root must be an object: {catalog_file}")
    return data


def kit_index(catalog: dict[str, Any]) -> dict[str, dict[str, Any]]:
    kits = catalog.get("kits")
    if not isinstance(kits, list):
        raise ValueError("catalog.kits must be an array")
    out: dict[str, dict[str, Any]] = {}
    for kit in kits:
        if not isinstance(kit, dict) or "id" not in kit:
            raise ValueError("each kit needs an id string")
        kid = kit["id"]
        if not isinstance(kid, str) or not kid:
            raise ValueError("kit id must be a non-empty string")
        if kid in out:
            raise ValueError(f"duplicate kit id: {kid}")
        out[kid] = kit
    return out


def summarize_kit(kit: dict[str, Any]) -> dict[str, Any]:
    slots = kit.get("slots") if isinstance(kit.get("slots"), list) else []
    return {
        "id": kit.get("id"),
        "genre": kit.get("genre"),
        "title": kit.get("title"),
        "summary": kit.get("summary"),
        "modules": kit.get("modules"),
        "referenceExample": kit.get("referenceExample"),
        "slotCount": len(slots),
        "requiredSlots": [
            slot.get("id")
            for slot in slots
            if isinstance(slot, dict) and slot.get("required") is True
        ],
    }


def validate_catalog(catalog: dict[str, Any], schema_path: Path | None = None) -> list[str]:
    """Return human-readable finding strings; empty means ok.

    Uses a structural check always. When jsonschema is installed, also run the
    draft-07 schema. CI does not require the optional dependency.
    """
    findings: list[str] = []
    if catalog.get("schema") != SCHEMA_ID:
        findings.append(f"schema must be {SCHEMA_ID!r}")
    if catalog.get("schemaVersion") != SCHEMA_VERSION:
        findings.append(f"schemaVersion must be {SCHEMA_VERSION}")
    try:
        index = kit_index(catalog)
    except ValueError as exc:
        findings.append(str(exc))
        return findings
    if not index:
        findings.append("catalog.kits is empty")

    for kid, kit in index.items():
        for key in ("genre", "title", "summary", "modules", "window", "slots"):
            if key not in kit:
                findings.append(f"{kid}: missing {key}")
        window = kit.get("window")
        if isinstance(window, dict):
            for dim in ("width", "height"):
                value = window.get(dim)
                if not isinstance(value, int) or value < 1:
                    findings.append(f"{kid}: window.{dim} must be a positive int")
        slots = kit.get("slots")
        if not isinstance(slots, list) or not slots:
            findings.append(f"{kid}: slots must be a non-empty array")
            continue
        seen: set[str] = set()
        for slot in slots:
            if not isinstance(slot, dict):
                findings.append(f"{kid}: slot is not an object")
                continue
            sid = slot.get("id")
            if not isinstance(sid, str) or not sid:
                findings.append(f"{kid}: slot missing id")
                continue
            if sid in seen:
                findings.append(f"{kid}: duplicate slot id {sid}")
            seen.add(sid)
            size = slot.get("pixelSize")
            if not isinstance(size, dict):
                findings.append(f"{kid}.{sid}: pixelSize required")
            else:
                for dim in ("width", "height"):
                    value = size.get(dim)
                    if not isinstance(value, int) or value < 1:
                        findings.append(f"{kid}.{sid}: pixelSize.{dim} must be a positive int")
            if not isinstance(slot.get("aspectRatio"), str) or not slot.get("aspectRatio"):
                findings.append(f"{kid}.{sid}: aspectRatio required")
            if slot.get("format") not in ("png", "webp", "jpg"):
                findings.append(f"{kid}.{sid}: format must be png|webp|jpg")
            if slot.get("colorType") not in ("rgba", "rgb", "gray"):
                findings.append(f"{kid}.{sid}: colorType must be rgba|rgb|gray")
            if slot.get("alpha") not in ("required", "optional", "forbidden"):
                findings.append(f"{kid}.{sid}: alpha must be required|optional|forbidden")
            constraints = slot.get("constraints")
            if not isinstance(constraints, list) or not constraints:
                findings.append(f"{kid}.{sid}: constraints must be a non-empty array")
            if not isinstance(slot.get("pathPattern"), str) or not slot.get("pathPattern"):
                findings.append(f"{kid}.{sid}: pathPattern required")

    schema_file = schema_path or DEFAULT_SCHEMA
    if schema_file.is_file():
        try:
            import jsonschema  # type: ignore
        except ImportError:
            pass
        else:
            schema = json.loads(schema_file.read_text(encoding="utf-8"))
            validator = jsonschema.Draft7Validator(schema)
            for error in sorted(validator.iter_errors(catalog), key=lambda e: list(e.path)):
                path = ".".join(str(p) for p in error.path) or "<root>"
                findings.append(f"schema:{path}: {error.message}")

    return findings


def cmd_list(catalog: dict[str, Any], as_json: bool) -> int:
    summaries = [summarize_kit(kit) for kit in catalog.get("kits", [])]
    if as_json:
        print(json.dumps({"ok": True, "count": len(summaries), "kits": summaries}, indent=2, ensure_ascii=False))
        return 0
    print(f"{len(summaries)} asset kit(s):")
    for item in summaries:
        req = ",".join(item.get("requiredSlots") or []) or "-"
        print(f"  {item['id']:28}  {item.get('genre')}/{item.get('title')}  required=[{req}]")
    return 0


def cmd_get(catalog: dict[str, Any], kit_id: str, as_json: bool) -> int:
    index = kit_index(catalog)
    kit = index.get(kit_id)
    if kit is None:
        known = ", ".join(sorted(index))
        if as_json:
            print(
                json.dumps(
                    {
                        "ok": False,
                        "error": f"unknown kit id: {kit_id}",
                        "hint": f"known ids: {known}",
                    },
                    indent=2,
                    ensure_ascii=False,
                )
            )
        else:
            print(f"unknown kit id: {kit_id}", file=sys.stderr)
            print(f"known ids: {known}", file=sys.stderr)
        return 1
    if as_json:
        print(json.dumps({"ok": True, "kit": kit}, indent=2, ensure_ascii=False))
    else:
        print(json.dumps(kit, indent=2, ensure_ascii=False))
    return 0


def cmd_validate(catalog: dict[str, Any], catalog_file: Path) -> int:
    findings = validate_catalog(catalog)
    if findings:
        print(f"INVALID {catalog_file}", file=sys.stderr)
        for finding in findings:
            print(f"  - {finding}", file=sys.stderr)
        return 1
    print(f"OK {catalog_file} ({len(kit_index(catalog))} kits)")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--catalog",
        type=Path,
        default=None,
        help="Path to catalog.json (default: cmake/sdk/asset-kits/catalog.json)",
    )
    parser.add_argument("--json", action="store_true", help="Machine-readable output")
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("list", help="List kit ids and required slots")
    get_p = sub.add_parser("get", help="Print one kit document")
    get_p.add_argument("id", help="Kit id (e.g. visual-novel.dialogue)")
    sub.add_parser("validate", help="Validate catalog structure / schema")
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    catalog_file = resolve_catalog(args.catalog)
    catalog = load_catalog(catalog_file)
    if args.command == "list":
        return cmd_list(catalog, args.json)
    if args.command == "get":
        return cmd_get(catalog, args.id, args.json)
    if args.command == "validate":
        return cmd_validate(catalog, catalog_file)
    parser.error(f"unknown command {args.command}")
    return 2


if __name__ == "__main__":
    import utf8_stdio

    utf8_stdio.enable_utf8_stdio()
    raise SystemExit(main())
