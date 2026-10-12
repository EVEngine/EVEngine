"""Execute a white-box playtest recipe against a live engine or dry-run memory."""

from __future__ import annotations

import json
from copy import deepcopy
from typing import Any, Dict, List, Optional

from .mcp_client import McpClient, McpError
from .recipe import default_recipe, eval_assert, validate_recipe


class DryRunEngine:
    """In-memory stand-in so CI / agents can exercise the recipe without eve."""

    def __init__(self) -> None:
        self.props: Dict[str, Dict[str, Any]] = {}
        self.lighting: Dict[str, Any] = {}
        self.camera: Dict[str, Any] = {}

    def reset(self) -> Dict[str, Any]:
        self.props.clear()
        self.lighting = {"timeOfDay": "day", "intensity": 1.0}
        self.camera = {}
        return {"ok": True}

    def batch(self, actions: List[Dict[str, Any]], stop_on_error: bool = True) -> Dict[str, Any]:
        results: List[Dict[str, Any]] = []
        applied = 0
        failed = 0
        for index, item in enumerate(actions):
            action = str(item.get("action", ""))
            target = str(item.get("target", ""))
            params = dict(item.get("params") or {})
            out: Dict[str, Any] = {"ok": True, "index": index, "action": action, "target": target}
            if action in ("add_object", "spawn", "place"):
                pid = str(params.get("id") or target or f"prop_{index}")
                self.props[pid] = {
                    "id": pid,
                    "kind": params.get("kind", "box"),
                    "pos": list(params.get("pos") or [0, 0, 0]),
                    "scale": list(params.get("scale") or [1, 1, 1]),
                }
            elif action in ("move", "move_object"):
                if target not in self.props:
                    out = {"ok": False, "index": index, "error": f"missing {target}"}
                else:
                    self.props[target]["pos"] = list(params.get("pos") or [0, 0, 0])
            elif action == "scale":
                if target not in self.props:
                    out = {"ok": False, "index": index, "error": f"missing {target}"}
                else:
                    self.props[target]["scale"] = list(params.get("scale") or [1, 1, 1])
            elif action in ("remove", "remove_object"):
                self.props.pop(target, None)
            elif action in ("lighting", "set_lighting"):
                self.lighting.update(params)
            elif action == "camera":
                self.camera.update(params)
            elif action in ("info", "list", "reset"):
                if action == "reset":
                    self.reset()
            else:
                out = {"ok": False, "index": index, "error": f"unknown action {action}"}
            results.append(out)
            if out.get("ok"):
                applied += 1
            else:
                failed += 1
                if stop_on_error:
                    return {
                        "ok": False,
                        "applied": applied,
                        "failed": failed,
                        "stoppedAt": index,
                        "results": results,
                    }
        return {
            "ok": failed == 0,
            "applied": applied,
            "failed": failed,
            "count": len(actions),
            "results": results,
            "revision": applied,
        }

    def info(self) -> Dict[str, Any]:
        props = list(self.props.values())
        return {"ok": True, "count": len(props), "props": props}


def run_recipe(
    recipe: Dict[str, Any],
    *,
    host: str = "127.0.0.1",
    port: int = 7529,
    dry_run: bool = False,
    poll_timeout_s: float = 30.0,
) -> Dict[str, Any]:
    recipe = validate_recipe(deepcopy(recipe))
    report: Dict[str, Any] = {
        "schemaId": "eve.whitebox.playtest-report",
        "schemaVersion": 1,
        "recipeId": recipe.get("id"),
        "dryRun": dry_run,
        "ok": False,
        "asserts": [],
    }

    if dry_run:
        engine = DryRunEngine()
        if recipe.get("reset", True):
            report["reset"] = engine.reset()
        scene = engine.batch(list(recipe.get("scene") or []), bool(recipe.get("stopOnError", True)))
        report["scene"] = scene
        report["info"] = engine.info()
        if recipe.get("inputs"):
            report["input"] = {
                "ok": True,
                "status": "ready",
                "injected": len(recipe["inputs"]),
                "dryRun": True,
            }
        if recipe.get("play"):
            report["play"] = {"ok": True, "op": "batch", "count": len(recipe["play"]), "dryRun": True}
    else:
        with McpClient(host, port) as client:
            # Prefer the engine one-shot when available; fall back to primitives.
            payload = {
                "reset": bool(recipe.get("reset", True)),
                "scene": recipe.get("scene") or [],
                "stopOnError": bool(recipe.get("stopOnError", True)),
            }
            if recipe.get("inputs"):
                payload["inputs"] = recipe["inputs"]
                payload["waitFrames"] = int(recipe.get("waitFrames") or 0)
                payload["capture"] = bool(recipe.get("capture") or False)
                if recipe.get("capturePath"):
                    payload["capturePath"] = recipe["capturePath"]
            if recipe.get("play"):
                payload["play"] = recipe["play"]
            try:
                live = client.tool("eve_whitebox_playtest", payload)
            except McpError:
                live = _run_via_primitives(client, recipe)
            if not isinstance(live, dict):
                raise McpError(f"unexpected whitebox response: {live!r}")
            report.update(live)
            if report.get("status") == "pending" or (
                isinstance(report.get("input"), dict) and report["input"].get("status") == "pending"
            ):
                batch_id = None
                if isinstance(report.get("input"), dict):
                    batch_id = report["input"].get("id")
                report["input"] = client.poll_input_until_ready(batch_id, timeout_s=poll_timeout_s)
                report["status"] = "ready"
            if "info" not in report:
                report["info"] = client.tool("eve_scene_info", {})

    # Pipeline status before asserts so recipes can assert path "ok".
    pipeline_ok = True
    if isinstance(report.get("scene"), dict) and report["scene"].get("ok") is False:
        pipeline_ok = False
    if isinstance(report.get("input"), dict) and report["input"].get("ok") is False:
        pipeline_ok = False
    if isinstance(report.get("play"), dict) and report["play"].get("ok") is False:
        pipeline_ok = False
    if report.get("ok") is False and not dry_run and "fallback" not in report:
        # Live eve_whitebox_playtest already failed.
        pipeline_ok = False
    report["ok"] = pipeline_ok

    asserts = []
    all_ok = pipeline_ok
    for item in recipe.get("assert") or []:
        result = eval_assert(report, item)
        asserts.append(result)
        if not result.get("ok"):
            all_ok = False
    report["asserts"] = asserts
    report["ok"] = all_ok
    report["passed"] = all_ok
    return report


def _run_via_primitives(client: McpClient, recipe: Dict[str, Any]) -> Dict[str, Any]:
    report: Dict[str, Any] = {
        "schemaId": "eve.whitebox.playtest-report",
        "schemaVersion": 1,
        "ok": True,
        "status": "ready",
        "fallback": "primitives",
    }
    if recipe.get("reset", True):
        report["reset"] = client.tool("eve_scene_reset", {})
    scene_actions = list(recipe.get("scene") or [])
    if scene_actions:
        try:
            report["scene"] = client.tool(
                "eve_scene_batch",
                {"actions": scene_actions, "stopOnError": bool(recipe.get("stopOnError", True))},
            )
        except McpError:
            results = []
            applied = 0
            for item in scene_actions:
                one = client.tool(
                    "eve_scene_modify",
                    {
                        "action": item["action"],
                        "target": item.get("target", ""),
                        "params": item.get("params") or {},
                    },
                )
                results.append(one if isinstance(one, dict) else {"ok": True, "value": one})
                if isinstance(one, dict) and one.get("ok") is False:
                    report["scene"] = {"ok": False, "applied": applied, "results": results}
                    report["ok"] = False
                    return report
                applied += 1
            report["scene"] = {"ok": True, "applied": applied, "results": results}
    if recipe.get("inputs"):
        args: Dict[str, Any] = {"actions": recipe["inputs"]}
        if recipe.get("waitFrames") is not None:
            args["waitFrames"] = int(recipe["waitFrames"])
        if recipe.get("capture"):
            args["capture"] = True
            args["capturePath"] = recipe.get("capturePath") or "wb_playtest.png"
        report["input"] = client.tool("eve_input_batch", args)
    if recipe.get("play"):
        requests = []
        for item in recipe["play"]:
            req = dict(item)
            req.setdefault("schemaId", "evengine.play-request")
            req.setdefault("schemaVersion", 1)
            requests.append(req)
        report["play"] = client.tool(
            "eve_play",
            {
                "request": {
                    "schemaId": "evengine.play-request",
                    "schemaVersion": 1,
                    "op": "batch",
                    "requests": requests,
                }
            },
        )
    report["info"] = client.tool("eve_scene_info", {})
    return report


def run_default_dry() -> Dict[str, Any]:
    return run_recipe(default_recipe(), dry_run=True)
