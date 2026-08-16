"""Built-in business judgement rules.

These encode the project-specific risk semantics so the lightweight VLM is only
asked to *perceive* what is in the frame; the *decision* is deterministic and
explainable. Rule outcomes influence the final risk rating and the
``need_high_precision_review`` flag.

Rules (as specified):
    * road occlusion           -> HIGH risk (risk_score 3)
    * vegetation clustering    -> MEDIUM risk (risk_score 2)
    * balanced layout          -> no risk (risk_score 0)
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Dict, List, Optional

from .protocol import EMPTY_MODEL_RESULT, ModelResult, ProblemRegion, ProblemType

# Deterministic risk contribution per recognised problem type.
BASE_RISK: Dict[ProblemType, int] = {
    ProblemType.ROAD_BLOCKED: 3,
    ProblemType.VEGETATION_CLUSTER: 2,
    ProblemType.OCCLUSION: 3,
    ProblemType.DENSE: 2,
    ProblemType.SPARSE: 1,
    ProblemType.INTERSECT: 2,
}


@dataclass
class RuleContext:
    """Everything a rule needs to make a deterministic judgement."""

    geometry: str
    prompt: str
    # Parsed problem regions *as perceived by the model*.
    model_regions: List[ProblemRegion] = field(default_factory=list)


@dataclass
class RuleOutcome:
    """Aggregate, deterministic rating for one scene."""

    risk_score: int = 0
    has_problem: bool = False
    regions: List[ProblemRegion] = field(default_factory=list)
    need_high_precision_review: bool = False
    reasons: List[str] = field(default_factory=list)

    def to_model_result(self) -> ModelResult:
        return ModelResult(
            risk_score=self.risk_score,
            has_problem=self.has_problem,
            problem_regions=self.regions,
            need_high_precision_review=self.need_high_precision_review,
        )


def evaluate(geometry: str, prompt: str, model_regions: List[ProblemRegion]) -> RuleOutcome:
    """Combine geometry hint + model perception into a deterministic rating."""
    ctx = RuleContext(geometry=geometry, prompt=prompt, model_regions=model_regions)
    out = RuleOutcome()
    road_present = _geometry_mentions_road(geometry)

    # 1. Rule: explicit road occlusion in geometry hint => HIGH risk.
    #    Merely mentioning a road is not an occlusion -- only a clear
    #    "blocked / occluded" signal warrants the high rating.
    if _geometry_mentions_road_blocked(geometry):
        out.regions.append(
            ProblemRegion(bbox=[0, 0, 0, 0], type=ProblemType.ROAD_BLOCKED, note="geometry hints road occlusion")
        )
        out.reasons.append("road occlusion in geometry -> high risk")
        out.risk_score = max(out.risk_score, 3)

    # 2. Model perceived problem regions.
    seen_types: set[ProblemType] = set()
    for r in model_regions:
        out.regions.append(r)
        seen_types.add(r.type)

    for t in seen_types:
        if t in (ProblemType.ROAD_BLOCKED, ProblemType.OCCLUSION):
            out.risk_score = max(out.risk_score, 3)
            out.reasons.append(f"{t.value} -> high risk")
        elif t in (ProblemType.VEGETATION_CLUSTER, ProblemType.DENSE, ProblemType.INTERSECT):
            out.risk_score = max(out.risk_score, 2)
            out.reasons.append(f"{t.value} -> medium risk")

    # 3. Vegetation clustering in geometry bumps to medium (road present also
    #    raises attention but never alone to high).
    if _geometry_mentions_vegetation_cluster(geometry):
        out.risk_score = max(out.risk_score, 2)
        out.reasons.append("vegetation cluster in geometry -> medium risk")
    elif road_present and out.risk_score >= 2:
        out.reasons.append("road present with detected issues -> keep risk")

    # 4. Balanced layout (nothing detected) -> no risk.
    if out.risk_score == 0:
        out.risk_score = 0
        out.has_problem = False
        return out

    out.has_problem = True
    # High risk scenes always need a high-precision re-check.
    out.need_high_precision_review = out.risk_score >= 3
    return out


def _geometry_mentions_road(geometry: str) -> bool:
    g = (geometry or "").lower()
    return any(k in g for k in ("road", "道路", "road=(", "roadstart"))


def _geometry_mentions_road_blocked(geometry: str) -> bool:
    g = (geometry or "").lower()
    return any(k in g for k in ("blocked", "occluded", "occlusion", "road_blocked", "遮挡"))


def _geometry_mentions_vegetation_cluster(geometry: str) -> bool:
    g = (geometry or "").lower()
    # Only a *cluster* of vegetation is a medium-risk signal; a single sparse
    # tree / bush at a corner is normal layout and must not bump the score.
    return any(k in g for k in ("cluster", "vegetation", "植被", "dense", "密集")) or (
        "tree" in g and any(k in g for k in ("n=", "n =", "x", "group", "patch"))
    )


__all__ = ["evaluate", "RuleOutcome", "RuleContext", "BASE_RISK", "EMPTY_MODEL_RESULT"]
