"""工具注册与引擎调用封装（含 dry-run 仿真，便于无引擎/无模型时演示闭环）。"""

from __future__ import annotations

import json
import os
import struct
import zlib
from typing import Any, Dict, List, Optional

from .models import parse_json_block
from .report import GeometryInfo, Issue


def _png_bytes(width: int = 16, height: int = 16, seed: int = 0) -> bytes:
    # 极小的合成 PNG（占位快照），用于 dry-run / 无真实截图时的流程演示。
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        for x in range(width):
            raw += bytes(((x * 8 + seed) & 0xFF, (y * 8 + seed) & 0xFF, (x ^ y) & 0xFF))
    def chunk(tag: bytes, data: bytes) -> bytes:
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", ihdr)
        + chunk(b"IDAT", zlib.compress(bytes(raw)))
        + chunk(b"IEND", b"")
    )


class DryRunEngine:
    """无真实引擎时仿真场景：每次 apply_fix 提升 quality，使巡检闭环收敛可复现。"""

    def __init__(self, initial_quality: float = 0.40):
        self.quality = initial_quality
        self.fixes = 0
        self.defects: Dict[str, float] = {
            "overlap": 0.6,
            "occlusion": 0.5,
            "out_of_frame": 0.7,
        }

    def camera_generate(self, targets: List[str], count: int) -> List[dict]:
        cams = []
        for i in range(count):
            cams.append({"id": f"cam_{i}", "target": (targets or ["hero"])[i % max(1, len(targets))],
                         "pitch": 20 + i * 5, "yaw": i * 60, "distance": 8.0 + i})
        return cams

    def screenshot(self, camera: dict) -> bytes:
        return _png_bytes(seed=int(camera.get("id", "cam_0").split("_")[-1]))

    def scene_info(self, camera: dict) -> GeometryInfo:
        gi = GeometryInfo(frame_id=camera.get("id", ""))
        gi.objects = [{"name": "hero", "pos": [0, 0, 0]}, {"name": "enemy", "pos": [2, 0, 1]}]
        for code, base in self.defects.items():
            if base > self.quality:
                gi.issues.append(Issue(code=code, message=f"dummy {code}", severity="medium", target=code))
        gi.score = self.quality
        return gi

    def scene_modify(self, action: str, target: str, params: Dict[str, Any]) -> str:
        self.quality = min(1.0, self.quality + 0.25)
        self.fixes += 1
        return f"ok applied {action} to {target} (quality now {self.quality:.2f})"


class ToolRegistry:
    """把逻辑能力映射到引擎 MCP 工具，并处理 dry-run 与 JSON 解析。"""

    def __init__(self, client, cfg, dry_run: bool = False):
        self.client = client
        self.cfg = cfg
        self.dry = DryRunEngine() if dry_run else None

    # ---- 底层工具调用 ----
    def _call(self, capability: str, **kwargs: Any) -> str:
        if self.dry:
            return self._dry(capability, kwargs)
        tool = self.cfg.tool(capability)
        args: Dict[str, Any] = {}
        for k, v in tool.get("args", {}).items():
            args[k] = kwargs.get(k, v)
        return self.client.call_tool(tool["name"], args)

    def _dry(self, capability: str, kwargs: Dict[str, Any]) -> str:
        if capability == "camera_generate":
            return json.dumps(self.dry.camera_generate(kwargs.get("targets", []), kwargs.get("count", 6)))
        if capability == "screenshot":
            cam = kwargs.get("camera", {})
            img = self.dry.screenshot(cam)
            return json.dumps({"frame_id": cam.get("id", "cam_0"), "image_b64": self._b64(img),
                               "width": 16, "height": 16})
        if capability == "scene_info":
            return json.dumps(self.dry.scene_info(kwargs).to_dict())
        if capability == "scene_modify":
            return self.dry.scene_modify(kwargs.get("action", ""), kwargs.get("target", ""),
                                         kwargs.get("params", {}))
        if capability == "status":
            return json.dumps({"dry_run": True})
        if capability == "eval":
            return json.dumps({"value": "dry-run", "ok": True})
        if capability == "run_script":
            return "dry-run script ok"
        return json.dumps({"dry_run": True, "capability": capability})

    @staticmethod
    def _b64(data: bytes) -> str:
        import base64
        return base64.b64encode(data).decode("ascii")

    # ---- 高层能力 ----
    def camera_generate(self, targets: List[str], count: int) -> List[dict]:
        raw = self._call("camera_generate", targets=targets, count=count)
        parsed = parse_json_block(raw)
        if isinstance(parsed, dict):
            parsed = parsed.get("cameras") or parsed.get("data") or parsed.get("result")
        return parsed if isinstance(parsed, list) else [{"id": "cam_0"}]

    def screenshot(self, camera: dict) -> tuple:
        """返回 (frame_id, base64_bytes, meta)。

        兼容两种引擎契约：
          - 直接返回 image_b64（如 dry-run / 模拟源）；
          - 返回磁盘 path（如 eve_screenshot），此处读文件并 base64 编码。

        若配置了绝对 output.screenshot_dir，截图写在该目录下（引擎与
        Agent 进程的 cwd 可能不同，必须用绝对路径对齐）。
        """
        frame_id = camera.get("id", "frame")
        shot_dir = (self.cfg.get("output") or {}).get("screenshot_dir") or ""
        if shot_dir:
            os.makedirs(shot_dir, exist_ok=True)
            path = os.path.join(shot_dir, f"eve_screenshot_{frame_id}.png")
        else:
            path = camera.get("path") or f"eve_screenshot_{frame_id}.png"
        raw = self._call("screenshot", camera=camera, path=path)
        parsed = parse_json_block(raw)
        if isinstance(parsed, dict):
            image_b64 = parsed.get("image_b64", "")
            if not image_b64 and parsed.get("path"):
                image_b64 = self._b64_of_file(str(parsed["path"]))
            return parsed.get("frame_id", frame_id), image_b64, parsed
        return frame_id, "", {}

    @staticmethod
    def _b64_of_file(path: str) -> str:
        try:
            with open(path, "rb") as f:
                data = f.read()
            import base64

            return base64.b64encode(data).decode("ascii")
        except OSError:
            return ""

    def scene_info(self, frame_id: str, targets: List[str]) -> GeometryInfo:
        raw = self._call("scene_info", frame_id=frame_id, targets=targets)
        parsed = parse_json_block(raw)
        if isinstance(parsed, dict):
            gi = GeometryInfo(frame_id=frame_id)
            for o in parsed.get("objects", []) or parsed.get("props", []):
                gi.objects.append(o)
            for it in parsed.get("issues", []):
                gi.issues.append(Issue(**{k: it[k] for k in ("code", "message", "severity", "target", "suggestion")
                                          if k in it}))
            gi.score = float(parsed.get("score", 1.0))
            return gi
        return GeometryInfo(frame_id=frame_id)

    def scene_modify(self, action: str, target: str, params: Dict[str, Any]) -> str:
        return self._call("scene_modify", action=action, target=target, params=params)

    def run_script(self, source: str) -> str:
        return self._call("run_script", source=source)
