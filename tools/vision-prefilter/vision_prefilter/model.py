"""Lightweight backend client for the vision pre-filter.

Backend: llama.cpp ``llama-server`` (OpenAI-compatible API) serving a GGUF
Qwen2-VL-2B model. No PyTorch / transformers / bitsandbytes are used at all.

JSON-only enforcement happens at the token-sampling level via a JSON schema
(see ``grammar.JSON_SCHEMA``), so the model physically cannot emit free text
or markdown. We still hard-parse the reply so any unexpected shape
becomes a structured error rather than leaking raw model text to callers.

The fixed task prompt lives here so consumers do not repeat it; a per-scene
``prompt`` may override only the task section.
"""

from __future__ import annotations

import base64
import json
import re
from typing import Optional, Sequence

import requests

from .grammar import JSON_SCHEMA
from .protocol import ProblemType, SceneInput, error_result, ok_result

# ---- fixed task prompt ---------------------------------------------------

FIXED_PROMPT = """You are a lightweight visual pre-filter for a 3D game scene.
Look at the provided render screenshot and the compact geometry text.
Judge scene quality and layout risks ONLY. Be conservative: a normal, balanced
game scene should score 0. Only flag an issue when it is clearly visible.

Return the scene risk assessment as JSON. Obey the schema exactly: fields
risk_score, has_problem, problem_regions, need_high_precision_review.

Rules:
- risk_score: 0 = balanced / normal layout; 1 = low; 2 = medium; 3 = high.
- has_problem: true only if a real problem is visible (leave false for normal scenes).
- problem_regions: list of { "bbox": [x1,y1,x2,y2], "type": "遮挡|过密|空旷|穿插|道路遮挡|植被扎堆", "note": "..." }.
  bbox is in image pixels. Leave EMPTY for a normal, balanced scene. A sparse
  tree or bush at the edge/corner is normal, NOT "植被扎堆".
- need_high_precision_review: true only when risk_score >= 3 (e.g. road occlusion).

Only output the JSON object. No prose, no markdown, no code fence.
"""

# ---- JSON extraction ------------------------------------------------------

_JSON_FENCE = re.compile(r"```(?:json)?\s*(\{.*?\})\s*```", re.DOTALL)
_JSON_OBJECT = re.compile(r"(\{.*\})", re.DOTALL)


def _extract_json(text: str) -> Optional[str]:
    """Return the best-guess JSON object substring from model text."""
    if not text:
        return None
    t = text.strip().lstrip("\ufeff")
    m = _JSON_FENCE.search(t)
    if m:
        t = m.group(1)
    else:
        m = _JSON_OBJECT.search(t)
        if m:
            t = m.group(1)
    try:
        json.loads(t)
        return t
    except json.JSONDecodeError:
        return None


def parse_model_result(raw: str) -> dict:
    """Parse raw model text into the standard result dict.

    Raises ValueError on anything that is not valid, schema-shaped JSON.
    """
    blob = _extract_json(raw)
    if blob is None:
        raise ValueError("model output is not JSON")
    data = json.loads(blob)

    risk = int(data.get("risk_score", 0))
    risk = max(0, min(3, risk))
    has_problem = bool(data.get("has_problem", risk >= 1))
    review = bool(data.get("need_high_precision_review", risk >= 3))
    regions = []
    for r in data.get("problem_regions", []) or []:
        bbox = list(r.get("bbox", [0, 0, 0, 0]))
        if len(bbox) != 4:
            bbox = [0, 0, 0, 0]
        ptype = r.get("type", ProblemType.OCCLUSION.value)
        if ptype not in {p.value for p in ProblemType}:
            ptype = ProblemType.OCCLUSION.value
        regions.append({"bbox": bbox, "type": ptype, "note": r.get("note")})
    return {
        "risk_score": risk,
        "has_problem": has_problem,
        "problem_regions": regions,
        "need_high_precision_review": review,
    }


# ---- llama-server client ----------------------------------------------------

class LlamaServer:
    """Thin client over llama.cpp ``llama-server`` OpenAI-compatible API."""

    def __init__(self, base_url: str = "http://127.0.0.1:8080", timeout: float = 120.0) -> None:
        self.base_url = base_url.rstrip("/")
        self.timeout = timeout

    @property
    def ready(self) -> bool:
        try:
            r = requests.get(f"{self.base_url}/health", timeout=2.0)
            return r.status_code == 200
        except requests.RequestException:
            return False

    def _image_data_url(self, scene: SceneInput) -> str:
        if scene.image.startswith("data:"):
            return scene.image
        return "data:image/png;base64," + scene.image

    def generate(self, scene: SceneInput) -> dict:
        """Run one scene; return the standard result dict (or raise)."""
        prompt = scene.prompt or FIXED_PROMPT
        if scene.geometry:
            prompt = f"{prompt}\n\nScene geometry:\n{scene.geometry}"

        payload = {
            "model": "qwen2-vl",
            "messages": [
                {
                    "role": "user",
                    "content": [
                        {"type": "image_url", "image_url": {"url": self._image_data_url(scene)}},
                        {"type": "text", "text": prompt},
                    ],
                }
            ],
            "max_tokens": 512,
            "temperature": 0.0,
            "json_schema": JSON_SCHEMA,
        }
        r = requests.post(f"{self.base_url}/v1/chat/completions", json=payload, timeout=self.timeout)
        r.raise_for_status()
        raw = r.json()["choices"][0]["message"]["content"]
        return parse_model_result(raw)

    def close(self) -> None:  # noqa: D401 - no persistent resources
        return None


def screen_batch(backend: LlamaServer, scenes: Sequence[SceneInput]) -> list:
    """Screen a batch; per-scene errors become structured error results."""
    results = []
    for scene in scenes:
        try:
            data = backend.generate(scene)
            results.append(ok_result(scene.id, model_result_from_dict(data)))
        except Exception as exc:  # noqa: BLE001 - keep service alive per scene
            results.append(error_result(scene.id, f"inference failed: {exc}"))
    return results


def model_result_from_dict(data: dict):
    from .protocol import ModelResult

    return ModelResult(**data)


__all__ = [
    "LlamaServer",
    "FIXED_PROMPT",
    "parse_model_result",
    "screen_batch",
    "model_result_from_dict",
]
