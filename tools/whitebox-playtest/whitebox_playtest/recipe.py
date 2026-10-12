"""White-box playtest recipe schema and validation."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any, Dict, List, Optional

SCHEMA_ID = "eve.whitebox.playtest-recipe"
SCHEMA_VERSION = 1


class RecipeError(ValueError):
    pass


def load_recipe(path: Path | str) -> Dict[str, Any]:
    text = Path(path).read_text(encoding="utf-8")
    data = json.loads(text)
    return validate_recipe(data)


def validate_recipe(data: Dict[str, Any]) -> Dict[str, Any]:
    if not isinstance(data, dict):
        raise RecipeError("recipe must be a JSON object")
    schema = data.get("schemaId", SCHEMA_ID)
    version = data.get("schemaVersion", SCHEMA_VERSION)
    if schema != SCHEMA_ID:
        raise RecipeError(f"unsupported schemaId: {schema}")
    if version != SCHEMA_VERSION:
        raise RecipeError(f"unsupported schemaVersion: {version}")
    scene = data.get("scene", [])
    if not isinstance(scene, list):
        raise RecipeError("scene must be an array")
    for index, action in enumerate(scene):
        if not isinstance(action, dict) or "action" not in action:
            raise RecipeError(f"scene[{index}] requires action")
    inputs = data.get("inputs", [])
    if not isinstance(inputs, list):
        raise RecipeError("inputs must be an array")
    play = data.get("play", [])
    if not isinstance(play, list):
        raise RecipeError("play must be an array")
    asserts = data.get("assert", [])
    if not isinstance(asserts, list):
        raise RecipeError("assert must be an array")
    for index, item in enumerate(asserts):
        if not isinstance(item, dict) or "path" not in item or "op" not in item:
            raise RecipeError(f"assert[{index}] requires path and op")
    return data


def resolve_path(root: Any, path: str) -> Any:
    cur = root
    for part in path.split("."):
        if isinstance(cur, dict):
            if part not in cur:
                raise KeyError(path)
            cur = cur[part]
        elif isinstance(cur, list):
            cur = cur[int(part)]
        else:
            raise KeyError(path)
    return cur


def eval_assert(report: Dict[str, Any], item: Dict[str, Any]) -> Dict[str, Any]:
    path = str(item["path"])
    op = str(item["op"])
    expected = item.get("value")
    try:
        actual = resolve_path(report, path)
    except (KeyError, IndexError, TypeError, ValueError) as exc:
        return {"ok": False, "path": path, "op": op, "error": str(exc)}
    ok = False
    if op in ("eq", "=="):
        ok = actual == expected
    elif op in ("ne", "!="):
        ok = actual != expected
    elif op in ("gt", ">"):
        ok = actual > expected
    elif op in ("gte", ">="):
        ok = actual >= expected
    elif op in ("lt", "<"):
        ok = actual < expected
    elif op in ("lte", "<="):
        ok = actual <= expected
    elif op == "contains":
        ok = expected in actual
    elif op == "truthy":
        ok = bool(actual)
    elif op == "falsy":
        ok = not bool(actual)
    else:
        return {"ok": False, "path": path, "op": op, "error": f"unknown op {op}"}
    return {"ok": ok, "path": path, "op": op, "expected": expected, "actual": actual}


def default_recipe() -> Dict[str, Any]:
    return {
        "schemaId": SCHEMA_ID,
        "schemaVersion": SCHEMA_VERSION,
        "id": "blockout-corridor",
        "reset": True,
        "scene": [
            {
                "action": "add_object",
                "target": "floor",
                "params": {"id": "floor", "kind": "ground", "pos": [0, 0, 0], "scale": [12, 0.2, 12]},
            },
            {
                "action": "add_object",
                "target": "wall_l",
                "params": {"id": "wall_l", "kind": "box", "pos": [-4, 1, 0], "scale": [0.4, 2, 8]},
            },
            {
                "action": "add_object",
                "target": "wall_r",
                "params": {"id": "wall_r", "kind": "box", "pos": [4, 1, 0], "scale": [0.4, 2, 8]},
            },
            {
                "action": "lighting",
                "target": "scene",
                "params": {"timeOfDay": "day", "intensity": 1.0},
            },
            {
                "action": "camera",
                "target": "scene",
                "params": {"eye": [0, 8, 14], "target": [0, 1, 0], "fov": 55},
            },
        ],
        "inputs": [
            {"op": "key", "key": "W", "down": True},
            {"op": "wait", "frames": 3},
            {"op": "key", "key": "W", "down": False},
        ],
        "waitFrames": 1,
        "capture": False,
        "play": [{"op": "status"}],
        "assert": [
            {"path": "scene.applied", "op": "gte", "value": 1},
            {"path": "info.count", "op": "gte", "value": 1},
            {"path": "ok", "op": "eq", "value": True},
        ],
    }
