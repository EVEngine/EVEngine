"""场景质检数据模型。"""

from __future__ import annotations

from dataclasses import dataclass, field, asdict
from typing import Any, List, Optional

SEVERITIES = ("critical", "high", "medium", "low")
VERDICTS = ("PASS", "ESCALATE", "FAIL")


def _severity_rank(s: str) -> int:
    return {"critical": 4, "high": 3, "medium": 2, "low": 1}.get(s, 0)


@dataclass
class Issue:
    code: str
    message: str
    severity: str = "medium"
    target: str = ""
    suggestion: str = ""
    source: str = ""  # geometry | local | vlm | planner

    def to_dict(self) -> dict:
        return asdict(self)


@dataclass
class GeometryInfo:
    """引擎 3D 几何真值（规则化自检结果）。"""

    frame_id: str = ""
    objects: List[dict] = field(default_factory=list)
    issues: List[Issue] = field(default_factory=list)
    score: float = 1.0

    def to_dict(self) -> dict:
        d = asdict(self)
        d["issues"] = [i.to_dict() for i in self.issues]
        return d


@dataclass
class LocalEval:
    """本地轻量模型筛查结果。"""

    score: float = 1.0
    issues: List[Issue] = field(default_factory=list)
    tags: List[str] = field(default_factory=list)
    raw: str = ""

    def to_dict(self) -> dict:
        d = asdict(self)
        d["issues"] = [i.to_dict() for i in self.issues]
        return d


@dataclass
class VlmReview:
    """云端高精度 VLM 精细评审结果。"""

    score: float = 1.0
    passed: bool = True
    issues: List[Issue] = field(default_factory=list)
    suggestions: List[str] = field(default_factory=list)
    raw: str = ""

    def to_dict(self) -> dict:
        d = asdict(self)
        d["issues"] = [i.to_dict() for i in self.issues]
        return d


@dataclass
class FrameVerdict:
    """单视角融合判定。"""

    frame_id: str = ""
    camera: dict = field(default_factory=dict)
    geometry: Optional[GeometryInfo] = None
    local: Optional[LocalEval] = None
    vlm: Optional[VlmReview] = None
    score: float = 1.0
    verdict: str = "PASS"
    issues: List[Issue] = field(default_factory=list)

    def to_dict(self) -> dict:
        return {
            "frame_id": self.frame_id,
            "camera": self.camera,
            "geometry": self.geometry.to_dict() if self.geometry else None,
            "local": self.local.to_dict() if self.local else None,
            "vlm": self.vlm.to_dict() if self.vlm else None,
            "score": round(self.score, 4),
            "verdict": self.verdict,
            "issues": [i.to_dict() for i in self.issues],
        }


@dataclass
class SceneReport:
    """一轮完整巡检报告。"""

    scene_id: str = ""
    round: int = 0
    frames: List[FrameVerdict] = field(default_factory=list)
    passed: bool = False
    score: float = 0.0
    summary: str = ""
    meta: dict = field(default_factory=dict)

    @property
    def defects(self) -> List[Issue]:
        out: List[Issue] = []
        for f in self.frames:
            out.extend(f.issues)
        return out

    def worst_severity(self) -> str:
        top = "low"
        for i in self.defects:
            if _severity_rank(i.severity) > _severity_rank(top):
                top = i.severity
        return top

    def to_dict(self) -> dict:
        return {
            "scene_id": self.scene_id,
            "round": self.round,
            "frames": [f.to_dict() for f in self.frames],
            "passed": self.passed,
            "score": round(self.score, 4),
            "summary": self.summary,
            "defects": [i.to_dict() for i in self.defects],
            "meta": self.meta,
        }

    def to_json(self) -> str:
        import json

        return json.dumps(self.to_dict(), ensure_ascii=False, indent=2)
