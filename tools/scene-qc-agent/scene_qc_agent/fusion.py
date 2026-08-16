"""三源信息融合：3D 几何真值 + 本地小模型 + 云端 VLM -> 融合评分与判定。"""

from __future__ import annotations

from typing import Any, Dict, List, Optional

from .report import FrameVerdict, GeometryInfo, Issue, LocalEval, SceneReport, VlmReview

CRITICAL = "critical"
PASS = "PASS"
FAIL = "FAIL"
ESCALATE = "ESCALATE"


def worst_severity(issues: List[Issue]) -> str:
    rank = {"critical": 4, "high": 3, "medium": 2, "low": 1}
    top = "low"
    for i in issues:
        if rank.get(i.severity, 0) > rank.get(top, 0):
            top = i.severity
    return top


def geometry_score(gi: GeometryInfo) -> float:
    if gi is None:
        return 1.0
    score = gi.score if gi.score is not None else 1.0
    penalty = 0.0
    rank = {"critical": 0.35, "high": 0.25, "medium": 0.15, "low": 0.05}
    for i in gi.issues:
        penalty = max(penalty, rank.get(i.severity, 0.05))
    return max(0.0, min(1.0, score, 1.0 - penalty))


def needs_escalation(gi: GeometryInfo, local: Optional[LocalEval], screen_escalate: float) -> bool:
    if local is None or local.score < screen_escalate:
        return True
    return any(i.severity == CRITICAL for i in (gi.issues if gi else []))


def fuse_frame(
    frame_id: str,
    camera: Dict[str, Any],
    geometry: Optional[GeometryInfo],
    local: Optional[LocalEval],
    vlm: Optional[VlmReview],
    weights: Dict[str, float],
    screen_escalate: float,
    pass_threshold: float,
) -> FrameVerdict:
    gs = geometry_score(geometry) if geometry else 1.0
    ls = local.score if local else 1.0

    escalated = needs_escalation(geometry, local, screen_escalate)
    if escalated and vlm is None:
        verdict = ESCALATE
        final = min(gs, ls)
        issues = list((geometry.issues if geometry else []) + (local.issues if local else []))
        return FrameVerdict(frame_id, camera, geometry, local, vlm, final, verdict, issues)

    vs = vlm.score if vlm else None
    avail = {"geometry": gs, "local": ls}
    w = dict(weights)
    if vlm is not None:
        avail["vlm"] = vs
    if vlm is None:
        w.pop("vlm", None)
    total = sum(w[k] for k in avail)
    if total <= 0:
        total = 1.0
    final = sum(avail[k] * w[k] for k in avail) / total

    issues = list((geometry.issues if geometry else []) + (local.issues if local else []))
    if vlm:
        issues += vlm.issues

    if vlm is not None and not vlm.passed:
        verdict = FAIL
    elif worst_severity(issues) == CRITICAL:
        verdict = FAIL
    elif final < pass_threshold:
        verdict = FAIL
    else:
        verdict = PASS

    return FrameVerdict(frame_id, camera, geometry, local, vlm, final, verdict, issues)


def aggregate_frames(scene_id: str, round_: int, frames: List[FrameVerdict], pass_threshold: float) -> SceneReport:
    if not frames:
        return SceneReport(scene_id=scene_id, round=round_, frames=[], passed=True, score=1.0, summary="no frames")
    passed = all(f.verdict == PASS for f in frames)
    score = min(f.score for f in frames)
    total_issues = sum(len(f.issues) for f in frames)
    all_issues = [i for f in frames for i in f.issues]
    summary = (
        f"round {round_}: {len(frames)} views, {'PASS' if passed else 'FAIL'} "
        f"(score {score:.2f}, worst {worst_severity(all_issues)}, "
        f"{total_issues} issues)"
    )
    return SceneReport(scene_id=scene_id, round=round_, frames=frames, passed=passed,
                       score=score, summary=summary)
