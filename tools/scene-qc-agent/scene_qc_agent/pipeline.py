"""巡检管线：机位生成 -> 快照 -> 本地小模型筛查 -> 高危送远端 VLM -> 融合判定。"""

from __future__ import annotations

import base64
import json
from typing import Any, Dict, List, Optional

from . import fusion as F
from .models import OpenAIClient, parse_json_block, make_data_url
from .report import FrameVerdict, GeometryInfo, Issue, LocalEval, SceneReport, VlmReview
from .tools import ToolRegistry


def _clamp(x: float) -> float:
    try:
        return max(0.0, min(1.0, float(x)))
    except (TypeError, ValueError):
        return 1.0


def _issues_from(raw_list: Any, source: str, default_sev: str = "medium") -> List[Issue]:
    out: List[Issue] = []
    if not isinstance(raw_list, list):
        return out
    for it in raw_list:
        if not isinstance(it, dict):
            continue
        out.append(Issue(
            code=str(it.get("code", "issue")),
            message=str(it.get("message", "")),
            severity=str(it.get("severity", default_sev)),
            target=str(it.get("target", "")),
            suggestion=str(it.get("suggestion", "")),
            source=source,
        ))
    return out


class QcPipeline:
    """把工具注册表 + 本地/云端模型 + 融合策略串成一轮完整巡检。"""

    def __init__(self, registry: ToolRegistry, local: Optional[OpenAIClient],
                 vlm: Optional[OpenAIClient], cfg):
        self.reg = registry
        self.local = local
        self.vlm = vlm
        self.cfg = cfg
        self.fusion_cfg = cfg["fusion"]
        self.save_snapshots = bool(cfg.get("output", {}).get("save_snapshots", False))

    # ---- 本地小模型筛查 ----
    def screen_frame(self, frame_id: str, camera: Dict[str, Any], image_b64: str,
                     geometry: GeometryInfo) -> LocalEval:
        if self.local is None or not image_b64:
            # 无本地模型或无图：退化用几何真值当本地信号，避免阻塞。
            return LocalEval(score=F.geometry_score(geometry), issues=list(geometry.issues), tags=["geometry-fallback"])
        prompt = (
            "你是本地轻量场景筛查模型。给定一帧游戏画面与3D几何信息，快速判断画面质量。\n"
            "只输出JSON：{\"score\":0~1,\"tags\":[\"...\"],\"issues\":[{\"code\":\"...\","
            "\"severity\":\"low|medium|high|critical\",\"message\":\"...\",\"suggestion\":\"...\"}]}。\n"
            f"frame={frame_id} camera={json.dumps(camera)} geometry_issues={json.dumps(geometry.to_dict()['issues'])}"
        )
        data_url = make_data_url(base64.b64decode(image_b64)) if image_b64 else ""
        try:
            raw = self.local.complete([{"role": "user", "content": prompt}],
                                      images=[data_url] if self.local.vision and data_url else None)
        except Exception as e:  # 模型异常不应拖垮整轮
            return LocalEval(score=F.geometry_score(geometry), issues=list(geometry.issues),
                             tags=[f"local-error:{e}"], raw=str(e))
        parsed = parse_json_block(raw)
        if not isinstance(parsed, dict):
            return LocalEval(score=F.geometry_score(geometry), issues=list(geometry.issues), raw=raw)
        return LocalEval(
            score=_clamp(parsed.get("score")),
            issues=_issues_from(parsed.get("issues"), "local"),
            tags=[str(t) for t in parsed.get("tags", []) if isinstance(t, str)],
            raw=raw,
        )

    # ---- 远端高精度 VLM 复核 ----
    def review_frame(self, frame_id: str, camera: Dict[str, Any], image_b64: str,
                     geometry: GeometryInfo, local: LocalEval) -> VlmReview:
        if self.vlm is None or not image_b64:
            return VlmReview(score=local.score, passed=local.score >= self.fusion_cfg["pass_threshold"])
        prompt = (
            "你是高精度场景质检 VLM。请精细评审这一帧的画面与3D几何，识别视觉缺陷（遮挡、穿插、出框、"
            "构图、光照、材质、特效）。只输出JSON：{\"score\":0~1,\"passed\":true|false,"
            "\"issues\":[{\"code\":\"...\",\"severity\":\"low|medium|high|critical\","
            "\"message\":\"...\",\"suggestion\":\"...\"}],\"suggestions\":[\"...\"]}。\n"
            f"frame={frame_id} local_score={local.score:.2f} "
            f"geometry={json.dumps(geometry.to_dict())}"
        )
        data_url = make_data_url(base64.b64decode(image_b64)) if image_b64 else ""
        try:
            raw = self.vlm.complete([{"role": "user", "content": prompt}],
                                    images=[data_url] if self.vlm.vision and data_url else None)
        except Exception as e:
            return VlmReview(score=local.score, passed=False, raw=str(e))
        parsed = parse_json_block(raw)
        if not isinstance(parsed, dict):
            return VlmReview(score=local.score, passed=False, raw=raw)
        return VlmReview(
            score=_clamp(parsed.get("score")),
            passed=bool(parsed.get("passed", parsed.get("score", 0) >= self.fusion_cfg["pass_threshold"])),
            issues=_issues_from(parsed.get("issues"), "vlm"),
            suggestions=[str(s) for s in parsed.get("suggestions", []) if isinstance(s, str)],
            raw=raw,
        )

    # ---- 单视角完整流程 ----
    def inspect_frame(self, scene_id: str, round_: int, idx: int,
                      camera: Dict[str, Any], targets: List[str]) -> FrameVerdict:
        frame_id = camera.get("id") or f"{scene_id}_{round_}_cam{idx}"
        _, image_b64, _meta = self.reg.screenshot(camera)
        geometry = self.reg.scene_info(frame_id, targets)

        local = self.screen_frame(frame_id, camera, image_b64, geometry)

        fw = self.fusion_cfg
        vlm: Optional[VlmReview] = None
        if F.needs_escalation(geometry, local, float(fw["screen_escalate"])):
            vlm = self.review_frame(frame_id, camera, image_b64, geometry, local)

        return F.fuse_frame(frame_id, camera, geometry, local, vlm,
                            fw["weights"], float(fw["screen_escalate"]), float(fw["pass_threshold"]))

    # ---- 一轮完整巡检 ----
    def inspect_scene(self, scene_id: str, round_: int, targets: List[str],
                      camera_count: Optional[int] = None) -> SceneReport:
        count = camera_count or int(self.cfg["cameras"]["count"])
        cameras = self.reg.camera_generate(targets, count)
        frames: List[FrameVerdict] = []
        for idx, cam in enumerate(cameras):
            frames.append(self.inspect_frame(scene_id, round_, idx, cam, targets))
        return F.aggregate_frames(scene_id, round_, frames, float(self.fusion_cfg["pass_threshold"]))
