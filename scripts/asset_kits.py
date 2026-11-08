#!/usr/bin/env python3
"""EVEngine AI asset-kit API — universal catalog access for internal systems.

Stable operations (see cmake/sdk/asset-kits/protocol.json):
  list | get | resolve | validate | sources

Source of truth: cmake/sdk/asset-kits/catalog.json (SDK: share/eve/ai/asset-kits/).
Overlays: EVE_ASSET_KITS_EXTRA / --extra (same schema; later kits replace by id).

Import as a library::

    import asset_kits
    store = asset_kits.CatalogStore.open()
    print(store.list(genre="card"))
    print(store.resolve("card.playing", "face", {"suit": "hearts", "rank": "A"}))
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CATALOG = ROOT / "cmake" / "sdk" / "asset-kits" / "catalog.json"
DEFAULT_SCHEMA = ROOT / "cmake" / "sdk" / "asset-kits" / "schema.json"

SCHEMA_ID = "eve.asset-kit.catalog"
SCHEMA_VERSION = 1
API_ID = "eve.asset-kit.api"
API_VERSION = 1

_PLACEHOLDER_RE = re.compile(r"\{([A-Za-z_][A-Za-z0-9_]*)\}")


def catalog_path_from_env() -> Path | None:
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


def _split_extra_env() -> list[Path]:
    raw = os.environ.get("EVE_ASSET_KITS_EXTRA", "").strip()
    if not raw:
        return []
    parts = re.split(r"[:;]", raw) if os.name != "nt" else raw.split(";")
    return [Path(p) for p in parts if p.strip()]


def _expand_catalog_path(path: Path) -> Path:
    if path.is_dir():
        candidate = path / "catalog.json"
        if not candidate.is_file():
            raise FileNotFoundError(f"no catalog.json under {path}")
        return candidate
    if not path.is_file():
        raise FileNotFoundError(f"catalog not found: {path}")
    return path


def load_catalog_file(path: Path) -> dict[str, Any]:
    catalog_file = _expand_catalog_path(path)
    text = catalog_file.read_text(encoding="utf-8")
    data = json.loads(text)
    if not isinstance(data, dict):
        raise ValueError(f"catalog root must be an object: {catalog_file}")
    return data


def resolve_catalog(explicit: Path | None = None) -> Path:
    """Backward-compatible single-file resolve (base only)."""
    if explicit is not None:
        return _expand_catalog_path(explicit)
    env = catalog_path_from_env()
    if env is not None:
        return env
    return DEFAULT_CATALOG


def load_catalog(path: Path | None = None) -> dict[str, Any]:
    """Load one catalog document (no overlays). Prefer CatalogStore.open()."""
    return load_catalog_file(resolve_catalog(path))


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


def summarize_kit(kit: dict[str, Any], *, source: str | None = None) -> dict[str, Any]:
    slots = kit.get("slots") if isinstance(kit.get("slots"), list) else []
    out: dict[str, Any] = {
        "id": kit.get("id"),
        "genre": kit.get("genre"),
        "title": kit.get("title"),
        "summary": kit.get("summary"),
        "modules": kit.get("modules"),
        "tags": kit.get("tags") if isinstance(kit.get("tags"), list) else [],
        "referenceExample": kit.get("referenceExample"),
        "slotCount": len(slots),
        "requiredSlots": [
            slot.get("id")
            for slot in slots
            if isinstance(slot, dict) and slot.get("required") is True
        ],
    }
    if source is not None:
        out["source"] = source
    return out


def envelope(**payload: Any) -> dict[str, Any]:
    body = {
        "api": API_ID,
        "apiVersion": API_VERSION,
        "schema": SCHEMA_ID,
        "schemaVersion": SCHEMA_VERSION,
    }
    body.update(payload)
    return body


def fill_template(template: str, values: dict[str, Any]) -> tuple[str, list[str]]:
    """Substitute {placeholders}. Returns (text, unresolved_names)."""
    unresolved: list[str] = []

    def repl(match: re.Match[str]) -> str:
        key = match.group(1)
        if key not in values or values[key] is None:
            unresolved.append(key)
            return match.group(0)
        return str(values[key])

    return _PLACEHOLDER_RE.sub(repl, template), unresolved


@dataclass
class CatalogSource:
    id: str
    path: str
    kit_ids: list[str] = field(default_factory=list)
    role: str = "base"  # base | overlay


@dataclass
class CatalogStore:
    """Merged view of the engine catalog plus optional overlays."""

    catalog: dict[str, Any]
    sources: list[CatalogSource]
    ownership: dict[str, str] = field(default_factory=dict)

    @classmethod
    def open(
        cls,
        base: Path | None = None,
        extras: list[Path] | None = None,
        *,
        include_env_extras: bool = True,
    ) -> CatalogStore:
        base_path = resolve_catalog(base)
        docs: list[tuple[str, str, Path, dict[str, Any]]] = []
        base_doc = load_catalog_file(base_path)
        docs.append(("engine", "base", base_path, base_doc))

        overlay_paths: list[Path] = []
        if include_env_extras:
            overlay_paths.extend(_split_extra_env())
        if extras:
            overlay_paths.extend(extras)

        for index, raw in enumerate(overlay_paths):
            path = _expand_catalog_path(Path(raw))
            docs.append((f"overlay-{index + 1}", "overlay", path, load_catalog_file(path)))

        merged_kits: dict[str, dict[str, Any]] = {}
        ownership: dict[str, str] = {}
        sources: list[CatalogSource] = []
        for source_id, role, path, doc in docs:
            index = kit_index(doc)
            sources.append(
                CatalogSource(
                    id=source_id,
                    path=str(path),
                    kit_ids=sorted(index),
                    role=role,
                )
            )
            for kid, kit in index.items():
                merged_kits[kid] = kit
                ownership[kid] = source_id

        catalog = {
            "schema": SCHEMA_ID,
            "schemaVersion": SCHEMA_VERSION,
            "kits": [merged_kits[k] for k in sorted(merged_kits)],
        }
        return cls(catalog=catalog, sources=sources, ownership=ownership)

    def kit_index(self) -> dict[str, dict[str, Any]]:
        return kit_index(self.catalog)

    def source_dicts(self) -> list[dict[str, Any]]:
        return [
            {
                "id": s.id,
                "role": s.role,
                "path": s.path,
                "kitIds": list(s.kit_ids),
            }
            for s in self.sources
        ]

    def list(
        self,
        *,
        genre: str | None = None,
        module: str | None = None,
        tag: str | None = None,
        q: str | None = None,
    ) -> dict[str, Any]:
        summaries: list[dict[str, Any]] = []
        q_norm = q.lower().strip() if q else None
        for kit in self.catalog.get("kits", []):
            if not isinstance(kit, dict):
                continue
            if genre and kit.get("genre") != genre:
                continue
            modules = kit.get("modules") if isinstance(kit.get("modules"), list) else []
            if module and module not in modules:
                continue
            tags = kit.get("tags") if isinstance(kit.get("tags"), list) else []
            if tag and tag not in tags:
                continue
            if q_norm:
                blob = " ".join(
                    str(kit.get(k, "")) for k in ("id", "title", "summary", "genre")
                ).lower()
                tag_blob = " ".join(str(t) for t in tags).lower()
                if q_norm not in blob and q_norm not in tag_blob:
                    continue
            kid = str(kit.get("id", ""))
            summaries.append(summarize_kit(kit, source=self.ownership.get(kid)))
        return envelope(
            ok=True,
            count=len(summaries),
            kits=summaries,
            sources=self.source_dicts(),
            hint="Call get/resolve with a kit id before generating art; do not invent Unity/Godot sizes",
        )

    def get(self, kit_id: str) -> dict[str, Any]:
        index = self.kit_index()
        kit = index.get(kit_id)
        if kit is None:
            known = ", ".join(sorted(index))
            return envelope(
                ok=False,
                error=f"unknown kit id: {kit_id}",
                hint=f"known ids: {known}",
            )
        return envelope(
            ok=True,
            kit=kit,
            source=self.ownership.get(kit_id),
            hint="Generate each required slot at pixelSize; write pathPattern; bind via consumer/bindings",
        )

    def resolve(
        self,
        kit_id: str,
        slot_id: str,
        vars: dict[str, Any] | None = None,
    ) -> dict[str, Any]:
        got = self.get(kit_id)
        if not got.get("ok"):
            return got
        kit = got["kit"]
        slots = kit.get("slots") if isinstance(kit.get("slots"), list) else []
        slot = next(
            (s for s in slots if isinstance(s, dict) and s.get("id") == slot_id),
            None,
        )
        if slot is None:
            known = ", ".join(
                str(s.get("id")) for s in slots if isinstance(s, dict) and s.get("id")
            )
            return envelope(
                ok=False,
                error=f"unknown slot id: {slot_id}",
                hint=f"kit {kit_id} slots: {known}",
            )

        size = slot.get("pixelSize") if isinstance(slot.get("pixelSize"), dict) else {}
        values: dict[str, Any] = {
            "width": size.get("width"),
            "height": size.get("height"),
            "aspectRatio": slot.get("aspectRatio"),
            "kitId": kit_id,
            "slotId": slot_id,
            "format": slot.get("format"),
        }
        if vars:
            values.update(vars)

        path, path_unresolved = fill_template(str(slot.get("pathPattern") or ""), values)
        prompt = ""
        prompt_unresolved: list[str] = []
        if isinstance(slot.get("promptTemplate"), str) and slot["promptTemplate"]:
            prompt, prompt_unresolved = fill_template(slot["promptTemplate"], values)

        unresolved = sorted(set(path_unresolved + prompt_unresolved))
        bindings: list[Any] = []
        if isinstance(kit.get("bindings"), list):
            bindings.extend(kit["bindings"])
        if isinstance(slot.get("bindings"), list):
            bindings.extend(slot["bindings"])

        job = {
            "kitId": kit_id,
            "slotId": slot_id,
            "role": slot.get("role"),
            "required": bool(slot.get("required")),
            "pixelSize": size,
            "displaySize": slot.get("displaySize"),
            "aspectRatio": slot.get("aspectRatio"),
            "format": slot.get("format"),
            "colorType": slot.get("colorType"),
            "alpha": slot.get("alpha"),
            "safeMargin": slot.get("safeMargin"),
            "path": path,
            "pathPattern": slot.get("pathPattern"),
            "prompt": prompt,
            "promptTemplate": slot.get("promptTemplate"),
            "constraints": slot.get("constraints"),
            "consumer": slot.get("consumer"),
            "bindings": bindings,
            "vars": values,
            "unresolved": unresolved,
            "source": self.ownership.get(kit_id),
        }
        return envelope(
            ok=True,
            job=job,
            hint=(
                "Fill unresolved placeholders then generate at pixelSize; "
                "write job.path; bind via consumer/bindings"
                if unresolved
                else "Generate at pixelSize; write job.path; bind via consumer/bindings"
            ),
        )

    def validate(self, schema_path: Path | None = None) -> dict[str, Any]:
        findings = validate_catalog(self.catalog, schema_path)
        if findings:
            return envelope(ok=False, error="catalog invalid", findings=findings, sources=self.source_dicts())
        return envelope(
            ok=True,
            kitCount=len(self.kit_index()),
            sources=self.source_dicts(),
        )


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


def _print_json(payload: dict[str, Any]) -> int:
    print(json.dumps(payload, indent=2, ensure_ascii=False))
    return 0 if payload.get("ok") else 1


def cmd_list(store: CatalogStore, as_json: bool, **filters: str | None) -> int:
    payload = store.list(**filters)
    if as_json:
        return _print_json(payload)
    print(f"{payload['count']} asset kit(s):")
    for item in payload["kits"]:
        req = ",".join(item.get("requiredSlots") or []) or "-"
        tags = ",".join(item.get("tags") or []) or "-"
        print(
            f"  {item['id']:28}  {item.get('genre')}/{item.get('title')}  "
            f"required=[{req}] tags=[{tags}]"
        )
    return 0


def cmd_get(store: CatalogStore, kit_id: str, as_json: bool) -> int:
    payload = store.get(kit_id)
    if as_json or not payload.get("ok"):
        return _print_json(payload)
    print(json.dumps(payload["kit"], indent=2, ensure_ascii=False))
    return 0


def cmd_resolve(
    store: CatalogStore,
    kit_id: str,
    slot_id: str,
    vars_json: str | None,
    as_json: bool,
) -> int:
    vars_obj: dict[str, Any] = {}
    if vars_json:
        parsed = json.loads(vars_json)
        if not isinstance(parsed, dict):
            return _print_json(envelope(ok=False, error="--vars must be a JSON object"))
        vars_obj = parsed
    payload = store.resolve(kit_id, slot_id, vars_obj)
    if as_json or not payload.get("ok"):
        return _print_json(payload)
    print(json.dumps(payload["job"], indent=2, ensure_ascii=False))
    return 0


def cmd_validate(store: CatalogStore, as_json: bool) -> int:
    payload = store.validate()
    if as_json:
        return _print_json(payload)
    if not payload.get("ok"):
        print("INVALID catalog", file=sys.stderr)
        for finding in payload.get("findings") or []:
            print(f"  - {finding}", file=sys.stderr)
        return 1
    print(f"OK ({payload.get('kitCount')} kits, {len(payload.get('sources') or [])} sources)")
    return 0


def cmd_sources(store: CatalogStore, as_json: bool) -> int:
    payload = envelope(ok=True, sources=store.source_dicts())
    if as_json:
        return _print_json(payload)
    for src in payload["sources"]:
        print(f"  {src['id']:12}  {src['role']:7}  {src['path']}  kits={len(src['kitIds'])}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--catalog",
        type=Path,
        default=None,
        help="Base catalog.json (default: cmake/sdk/asset-kits/catalog.json)",
    )
    parser.add_argument(
        "--extra",
        type=Path,
        action="append",
        default=[],
        help="Overlay catalog path or directory (repeatable; later wins on kit id)",
    )
    parser.add_argument("--json", action="store_true", help="Machine-readable eve.asset-kit.api envelope")
    sub = parser.add_subparsers(dest="command", required=True)

    list_p = sub.add_parser("list", help="List kit ids (optional genre/module/tag/q filters)")
    list_p.add_argument("--genre", default=None)
    list_p.add_argument("--module", default=None)
    list_p.add_argument("--tag", default=None)
    list_p.add_argument("--q", default=None, help="Substring match on id/title/summary/tags")

    get_p = sub.add_parser("get", help="Print one kit document")
    get_p.add_argument("id", help="Kit id (e.g. visual-novel.dialogue)")

    resolve_p = sub.add_parser("resolve", help="Resolve one slot into a generation job")
    resolve_p.add_argument("kit", help="Kit id")
    resolve_p.add_argument("slot", help="Slot id")
    resolve_p.add_argument(
        "--vars",
        default=None,
        help='JSON object of placeholders, e.g. \'{"suit":"hearts","rank":"A","style":"ink"}\'',
    )

    sub.add_parser("validate", help="Validate merged catalog structure / schema")
    sub.add_parser("sources", help="Show base + overlay catalog sources")
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    store = CatalogStore.open(base=args.catalog, extras=list(args.extra))
    if args.command == "list":
        return cmd_list(
            store,
            args.json,
            genre=args.genre,
            module=args.module,
            tag=args.tag,
            q=args.q,
        )
    if args.command == "get":
        return cmd_get(store, args.id, args.json)
    if args.command == "resolve":
        return cmd_resolve(store, args.kit, args.slot, args.vars, args.json)
    if args.command == "validate":
        return cmd_validate(store, args.json)
    if args.command == "sources":
        return cmd_sources(store, args.json)
    parser.error(f"unknown command {args.command}")
    return 2


if __name__ == "__main__":
    import utf8_stdio

    utf8_stdio.enable_utf8_stdio()
    raise SystemExit(main())
