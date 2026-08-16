"""FastAPI HTTP service exposing the vision pre-filter.

Endpoints:
    GET  /health                 -> service liveness + model readiness
    GET  /protocol               -> describe the input/output JSON contract
    POST /v1/prefilter/batch     -> batch risk screening

The model is loaded lazily on first request (or at startup if
``VISION_PREFILTER_EAGER_LOAD=1``), so the service can sit idle cheaply.
"""

from __future__ import annotations

import os
from typing import Optional

from fastapi import FastAPI
from fastapi.responses import JSONResponse

from . import DEFAULT_PROTOCOL_VERSION, DEFAULT_SERVICE_NAME, __version__
from .model import QwenVL, screen_batch
from .protocol import (
    BatchRequest,
    BatchResponse,
    SceneInput,
    SceneResult,
    as_dict,
)
from .rules import evaluate

MODEL_ID = os.environ.get("VISION_PREFILTER_MODEL", "Qwen/Qwen2-VL-2B-Instruct")
EAGER_LOAD = os.environ.get("VISION_PREFILTER_EAGER_LOAD", "0") == "1"

app = FastAPI(
    title="EVEngine Vision Pre-filter",
    description="Low-cost local VLM (Qwen2-VL-2B 4bit) risk screening of render snapshots.",
    version=__version__,
)

# Shared model handle (threaded batch execution via FastAPI's sync endpoints).
_model: QwenVL = QwenVL(model_id=MODEL_ID)


@app.on_event("startup")
def _startup() -> None:
    if EAGER_LOAD:
        _model.load()


@app.get("/health")
def health() -> JSONResponse:
    return JSONResponse(
        content={
            "service": DEFAULT_SERVICE_NAME,
            "status": "ok",
            "model": MODEL_ID,
            "model_loaded": _model.ready,
        }
    )


@app.get("/protocol")
def protocol() -> JSONResponse:
    return JSONResponse(
        content={
            "version": DEFAULT_PROTOCOL_VERSION,
            "request_schema": as_dict(BatchRequest.model_json_schema()),
            "result_schema": as_dict(SceneResult.model_json_schema()),
        }
    )


def _screen_scene(scene: SceneInput) -> SceneResult:
    """Screen one scene: run the VLM, then apply deterministic business rules."""
    from .protocol import error_result, ok_result

    try:
        perceived = _model.generate(scene)
    except Exception as exc:  # noqa: BLE001
        return error_result(scene.id, f"inference failed: {exc}")

    from .protocol import ModelResult, ProblemRegion

    model_result = ModelResult(
        risk_score=perceived["risk_score"],
        has_problem=perceived["has_problem"],
        problem_regions=[ProblemRegion(**r) for r in perceived["problem_regions"]],
        need_high_precision_review=perceived["need_high_precision_review"],
    )

    # Deterministic business rules on top of model perception.
    outcome = evaluate(
        geometry=scene.geometry or "",
        prompt=scene.prompt or "",
        model_regions=model_result.problem_regions,
    )
    # Prefer rule outcome; fall back to model's own rating when no rule fired.
    if not outcome.reasons:
        outcome.risk_score = model_result.risk_score
        outcome.has_problem = model_result.has_problem
        outcome.need_high_precision_review = model_result.need_high_precision_review
    else:
        # Rules give a >= rating; keep max with model perception to be safe.
        outcome.risk_score = max(outcome.risk_score, model_result.risk_score)
        outcome.need_high_precision_review = (
            outcome.need_high_precision_review or model_result.need_high_precision_review
        )

    return ok_result(scene.id, outcome.to_model_result())


@app.post("/v1/prefilter/batch", response_model=BatchResponse)
def prefilter_batch(req: BatchRequest) -> BatchResponse:
    results = [_screen_scene(s) for s in req.scenes]
    return BatchResponse(
        protocol_version=DEFAULT_PROTOCOL_VERSION,
        service=DEFAULT_SERVICE_NAME,
        results=results,
    )
