"""FastAPI HTTP service exposing the vision pre-filter.

Backend is llama.cpp ``llama-server`` (OpenAI-compatible) serving a GGUF
Qwen2-VL-2B model -- no PyTorch / transformers / bitsandbytes dependency.

Endpoints:
    GET  /health                 -> service liveness + backend readiness
    GET  /protocol               -> describe the input/output JSON contract
    POST /v1/prefilter/batch     -> batch risk screening
"""

from __future__ import annotations

import os

from fastapi import FastAPI
from fastapi.responses import JSONResponse

from . import DEFAULT_PROTOCOL_VERSION, DEFAULT_SERVICE_NAME, __version__
from .model import LlamaServer
from .protocol import (
    BatchRequest,
    BatchResponse,
    SceneInput,
    SceneResult,
    as_dict,
)
from .rules import evaluate

BACKEND_URL = os.environ.get("VISION_PREFILTER_BACKEND", "http://127.0.0.1:8080")

app = FastAPI(
    title="EVEngine Vision Pre-filter",
    description="Low-cost local VLM (Qwen2-VL-2B GGUF via llama.cpp) risk screening.",
    version=__version__,
)

_backend = LlamaServer(base_url=BACKEND_URL)


@app.get("/health")
def health() -> JSONResponse:
    return JSONResponse(
        content={
            "service": DEFAULT_SERVICE_NAME,
            "status": "ok",
            "backend": BACKEND_URL,
            "backend_ready": _backend.ready,
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
    from .protocol import ModelResult, ProblemRegion, error_result, ok_result

    try:
        perceived = _backend.generate(scene)
    except Exception as exc:  # noqa: BLE001
        return error_result(scene.id, f"inference failed: {exc}")

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
    if not outcome.reasons:
        outcome.risk_score = model_result.risk_score
        outcome.has_problem = model_result.has_problem
        outcome.need_high_precision_review = model_result.need_high_precision_review
    else:
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
