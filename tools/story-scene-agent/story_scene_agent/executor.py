"""Scene build executor: plan steps + stage brief -> live scene.

Two implementations:
  - McpSceneExecutor: drives the real engine through MCP + the scene_director kit.
  - MemorySceneExecutor: in-memory scene (dry-run / tests / planning preview).

Both expose the same surface so the orchestrator stays engine-agnostic.
"""

from __future__ import annotations

import sys
from typing import Any, Dict, List, Optional

from .config import load_scene_director_source


class SceneExecutor:
    """Interface: build / mutate / inspect a staged scene."""

    def connect(self) -> None: ...

    def close(self) -> None: ...

    def reset(self) -> None: ...

    def install_kit(self) -> bool: ...

    def spawn(self, params: Dict[str, Any]) -> Dict[str, Any]: ...

    def set_lighting(self, params: Dict[str, Any]) -> Dict[str, Any]: ...

    def set_camera(self, params: Dict[str, Any]) -> Dict[str, Any]: ...

    def modify(self, action: str, target: str, params: Dict[str, Any]) -> Dict[str, Any]: ...

    def info(self) -> Dict[str, Any]: ...

    def cameras(self, count: int = 6) -> List[Dict[str, Any]]: ...

    def screenshot(self, path: str) -> Dict[str, Any]: ...

    def run_script(self, source: str) -> str: ...


class McpSceneExecutor(SceneExecutor):
    """Real engine via the creative-brain TCP MCP client."""

    def __init__(self, host: str = "127.0.0.1", port: int = 7529):
        # Import lazily so the package works without the creative-brain checkout.
        from brain.mcp import McpClient  # type: ignore

        self.client = McpClient(host, port)
        self._kit_installed = False

    def connect(self) -> None:
        self.client.connect()

    def close(self) -> None:
        try:
            self.client.close()
        except Exception:
            pass

    def install_kit(self) -> bool:
        source = load_scene_director_source()
        if not source:
            return False
        try:
            self.client.run_script(source)
            self._kit_installed = True
            return True
        except Exception:
            return False

    def _ensure_kit(self) -> None:
        if not self._kit_installed:
            self.install_kit()

    def _modify(self, action: str, target: str, params: Dict[str, Any]) -> Dict[str, Any]:
        self._ensure_kit()
        raw = self.client.call_tool("eve_scene_modify",
                                     {"action": action, "target": target, "params": params})
        return _parse_dict(raw)

    def reset(self) -> None:
        self._ensure_kit()
        try:
            self.client.call_tool("eve_scene_reset")
        except Exception:
            pass

    def spawn(self, params: Dict[str, Any]) -> Dict[str, Any]:
        return self._modify("add_object", str(params.get("id", "prop")), params)

    def set_lighting(self, params: Dict[str, Any]) -> Dict[str, Any]:
        return self._modify("lighting", "scene", params)

    def set_camera(self, params: Dict[str, Any]) -> Dict[str, Any]:
        return self._modify("camera", "scene", params)

    def modify(self, action: str, target: str, params: Dict[str, Any]) -> Dict[str, Any]:
        return self._modify(action, target, params)

    def info(self) -> Dict[str, Any]:
        self._ensure_kit()
        return _parse_dict(self.client.call_tool("eve_scene_info"))

    def cameras(self, count: int = 6) -> List[Dict[str, Any]]:
        self._ensure_kit()
        raw = self.client.call_tool("eve_camera_generate", {"count": count})
        parsed = _parse_dict(raw)
        if isinstance(parsed, list):
            return parsed
        return []

    def screenshot(self, path: str) -> Dict[str, Any]:
        raw = self.client.call_tool("eve_screenshot", {"path": path})
        return _parse_dict(raw)

    def run_script(self, source: str) -> str:
        return self.client.run_script(source)


class MemorySceneExecutor(SceneExecutor):
    """Deterministic in-memory scene: no engine / no MCP (tests & dry-run)."""

    def __init__(self):
        self.props: Dict[str, Dict[str, Any]] = {}
        self.lighting: Dict[str, Any] = {}
        self.camera: Dict[str, Any] = {}
        self._counter = 0

    def connect(self) -> None:
        pass

    def close(self) -> None:
        pass

    def reset(self) -> None:
        self.props.clear()
        self.lighting = {"timeOfDay": "day", "intensity": 1.0}
        self.camera = {}

    def install_kit(self) -> bool:
        return bool(load_scene_director_source())

    def spawn(self, params: Dict[str, Any]) -> Dict[str, Any]:
        pid = str(params.get("id", f"prop_{self._counter}"))
        self._counter += 1
        self.props[pid] = {
            "id": pid,
            "kind": params.get("kind", "box"),
            "pos": list(params.get("pos", [0.0, 0.5, 0.0])),
            "scale": list(params.get("scale", [1.0, 1.0, 1.0])),
            "yaw_deg": params.get("yaw_deg", 0.0),
            "tint": list(params.get("tint", [1.0, 1.0, 1.0])),
        }
        return {"ok": True, "id": pid}

    def set_lighting(self, params: Dict[str, Any]) -> Dict[str, Any]:
        self.lighting.update(params)
        return {"ok": True}

    def set_camera(self, params: Dict[str, Any]) -> Dict[str, Any]:
        self.camera.update(params)
        return {"ok": True}

    def modify(self, action: str, target: str, params: Dict[str, Any]) -> Dict[str, Any]:
        p = dict(params or {})
        if action == "add_object":
            return self.spawn(p)
        if action == "move_object":
            if target in self.props:
                pos = p.get("pos") or [p.get("x", 0), p.get("y", 0), p.get("z", 0)]
                self.props[target]["pos"] = list(pos)
                return {"ok": True, "id": target}
        if action == "remove_object":
            if target in self.props:
                del self.props[target]
                return {"ok": True, "id": target}
        if action == "scale":
            if target in self.props and "scale" in p:
                self.props[target]["scale"] = list(p["scale"])
                return {"ok": True, "id": target}
        if action == "rotate":
            if target in self.props and "yaw_deg" in p:
                self.props[target]["yaw_deg"] = p["yaw_deg"]
                return {"ok": True, "id": target}
        if action == "lighting":
            return self.set_lighting(p)
        if action == "camera":
            return self.set_camera(p)
        if action == "cameras":
            return {"cameras": self.cameras(int(p.get("count", 6)))}
        if action == "info":
            return self.info()
        if action == "list":
            return {"ids": list(self.props.keys())}
        return {"ok": False, "error": f"unknown action: {action}"}

    def info(self) -> Dict[str, Any]:
        return {
            "count": len(self.props),
            "props": [
                {"id": v["id"], "kind": v["kind"], "pos": v["pos"],
                 "scale": v["scale"], "yaw_deg": v["yaw_deg"], "tint": v["tint"]}
                for v in self.props.values()
            ],
        }

    def cameras(self, count: int = 6) -> List[Dict[str, Any]]:
        cams = []
        for i in range(max(1, count)):
            ang = (i * 2.0 * 3.14159265) / max(1, count)
            cams.append({
                "id": f"cam_{i}",
                "eye": [12.0 * _cos(ang), 8.0, 12.0 * _sin(ang)],
                "target": [0.0, 1.0, 0.0],
                "fov": 55.0,
            })
        return cams

    def screenshot(self, path: str) -> Dict[str, Any]:
        return {"path": path, "width": 800, "height": 600}

    def run_script(self, source: str) -> str:
        return "dry-run script ok"


def _cos(a: float) -> float:
    import math

    return math.cos(a)


def _sin(a: float) -> float:
    import math

    return math.sin(a)


def _parse_dict(raw: str) -> Any:
    import json

    if not raw:
        return {}
    try:
        return json.loads(raw)
    except json.JSONDecodeError:
        return {"raw": raw}
