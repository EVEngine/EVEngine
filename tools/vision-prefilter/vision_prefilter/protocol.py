"""Standardised input/output protocol for the vision pre-filter service.

Every endpoint accepts one *batch* of scenes and returns one result per scene.
The model is forced to emit a single JSON object (no free text); if raw model
output cannot be parsed we never leak text to callers -- we return a structured
error result instead, so the schema below is the ONLY contract consumers see.
"""

from __future__ import annotations

from enum import Enum
from typing import Any, List, Optional

from pydantic import BaseModel, Field, field_validator

# risk_score semantics
#   0 = no problem (layout balanced / safe)
#   1 = low risk (minor, cosmetic)
#   2 = medium risk (vegetation clustering / denseness)
#   3 = high risk (road occlusion etc.)


class ProblemType(str, Enum):
    """Problem regions recognised by the pre-filter."""

    OCCLUSION = "遮挡"
    DENSE = "过密"
    SPARSE = "空旷"
    INTERSECT = "穿插"
    ROAD_BLOCKED = "道路遮挡"
    VEGETATION_CLUSTER = "植被扎堆"


class ProblemRegion(BaseModel):
    bbox: List[int] = Field(
        description="Normalised? No -- pixel bbox [x1, y1, x2, y2] on the source image.",
        min_length=4,
        max_length=4,
    )
    type: ProblemType
    note: Optional[str] = Field(default=None, description="Short reason (optional).")

    @field_validator("bbox")
    @classmethod
    def _bbox_sane(cls, v: List[int]) -> List[int]:
        if not (v[0] <= v[2] and v[1] <= v[3]):
            raise ValueError(f"bbox not ordered: {v}")
        return v


class SceneInput(BaseModel):
    """One snapshot + compact geometry text + task context to be screened."""

    id: str = Field(description="Caller-supplied scene identifier, echoed back.")
    image: str = Field(
        description=(
            "Base64-encoded image (PNG/JPEG). For the JSON transport use "
            "data-URL form 'data:image/png;base64,...'."
        )
    )
    geometry: Optional[str] = Field(
        default=None,
        description=(
            "Compact geometry text snapshot, e.g. newline-delimited "
            "'road=(startX,startY)->(endX,endY); treeCluster N=6@(x,y)'."
        ),
    )
    prompt: Optional[str] = Field(
        default=None, description="Optional per-scene task override (defaults to fixed task)."
    )


class ModelResult(BaseModel):
    """The strict JSON the model is asked to emit (output contract)."""

    risk_score: int = Field(ge=0, le=3, description="0..3 risk rating.")
    has_problem: bool
    problem_regions: List[ProblemRegion] = Field(default_factory=list)
    need_high_precision_review: bool = Field(
        description="True when this scene must be re-checked by the expensive VLM."
    )


class SceneResult(BaseModel):
    id: str
    ok: bool
    result: Optional[ModelResult] = None
    error: Optional[str] = Field(default=None, description="Structured error, never free text.")


class BatchRequest(BaseModel):
    protocol_version: str = Field(default="1.0")
    scenes: List[SceneInput] = Field(min_length=1)


class BatchResponse(BaseModel):
    protocol_version: str
    service: str
    results: List[SceneResult]


# ---------- helpers ----------

EMPTY_MODEL_RESULT = ModelResult(
    risk_score=0, has_problem=False, problem_regions=[], need_high_precision_review=False
)


def error_result(scene_id: str, message: str) -> SceneResult:
    return SceneResult(id=scene_id, ok=False, error=message)


def ok_result(scene_id: str, result: ModelResult) -> SceneResult:
    return SceneResult(id=scene_id, ok=True, result=result)


def as_dict(model: BaseModel) -> dict[str, Any]:
    return model.model_dump(mode="json")
