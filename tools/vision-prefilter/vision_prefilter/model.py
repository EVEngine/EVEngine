"""Qwen2-VL-2B 4-bit inference wrapper.

Responsibilities:
    * lazy-load Qwen2-VL-2B-Instruct with 4-bit quantization (bitsandbytes).
    * build a fixed task prompt + per-scene geometry text + image.
    * force the model to emit ONLY the standard JSON object (no free text).
    * hard-parse the output; anything unparseable becomes a structured error
      result rather than leaking raw model text back to the caller.

The fixed task prompt lives in this module so consumers do not have to repeat
it; a per-scene ``prompt`` may be supplied to *override* only the task section.
"""

from __future__ import annotations

import base64
import io
import json
import re
from typing import Optional, Sequence

from .protocol import ProblemType, SceneInput, error_result, ok_result

# ---- fixed task prompt ---------------------------------------------------

FIXED_PROMPT = """You are a lightweight visual pre-filter for a 3D game scene.
Look at the provided render screenshot and the compact geometry text.
Judge scene quality and layout risks ONLY.

Return EXACTLY ONE JSON object and NOTHING ELSE. No prose, no markdown, no
code fence. The object MUST match this schema exactly:

{
  "risk_score": 0,
  "has_problem": false,
  "problem_regions": [],
  "need_high_precision_review": false
}

Rules:
- risk_score: 0 = balanced layout / no problem; 1 = low; 2 = medium; 3 = high.
- has_problem: true if risk_score >= 1.
- problem_regions: list of { "bbox": [x1,y1,x2,y2], "type": "遮挡|过密|空旷|穿插|道路遮挡|植被扎堆", "note": "..." }.
  bbox is in image pixels. Empty list if no problem.
- need_high_precision_review: true when risk_score >= 3 (e.g. road occlusion).

If the image is unreadable, still return the JSON object with risk_score 0 and
has_problem false.
"""

# ---- JSON extraction ------------------------------------------------------

_JSON_FENCE = re.compile(r"```(?:json)?\s*(\{.*?\})\s*```", re.DOTALL)
_JSON_OBJECT = re.compile(r"(\{.*\})", re.DOTALL)


def _extract_json(text: str) -> Optional[str]:
    """Return the best-guess JSON object substring from model text."""
    if not text:
        return None
    # Strip BOM / whitespace.
    t = text.strip().lstrip("\ufeff")
    # Try a fenced block first (shouldn't happen, but be tolerant).
    m = _JSON_FENCE.search(t)
    if m:
        t = m.group(1)
    else:
        m = _JSON_OBJECT.search(t)
        if m:
            t = m.group(1)
    try:
        json.loads(t)  # validate
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
        regions.append(
            {"bbox": bbox, "type": ptype, "note": r.get("note")}
        )
    return {
        "risk_score": risk,
        "has_problem": has_problem,
        "problem_regions": regions,
        "need_high_precision_review": review,
    }


# ---- model wrapper ---------------------------------------------------------

class QwenVL:
    """Lazy Qwen2-VL-2B-Instruct 4-bit wrapper."""

    def __init__(
        self,
        model_id: str = "Qwen/Qwen2-VL-2B-Instruct",
        device: str = "auto",
        trust_remote_code: bool = False,
    ) -> None:
        self.model_id = model_id
        self.device = device
        self.trust_remote_code = trust_remote_code
        self._processor = None
        self._model = None

    def load(self) -> None:
        if self._model is not None:
            return
        import torch  # heavy; import lazily
        from transformers import AutoProcessor, BitsAndBytesConfig, Qwen2VLForConditionalGeneration

        bnb = BitsAndBytesConfig(
            load_in_4bit=True,
            bnb_4bit_quant_type="nf4",
            bnb_4bit_compute_dtype=torch.float16,
            bnb_4bit_use_double_quant=True,
        )
        self._model = Qwen2VLForConditionalGeneration.from_pretrained(
            self.model_id,
            quantization_config=bnb,
            device_map=self.device,
            torch_dtype=torch.float16,
            trust_remote_code=self.trust_remote_code,
        )
        self._processor = AutoProcessor.from_pretrained(
            self.model_id, trust_remote_code=self.trust_remote_code
        )

    @property
    def ready(self) -> bool:
        return self._model is not None

    def _decode_image(self, scene: SceneInput) -> "PIL.Image.Image":
        from PIL import Image

        data = scene.image
        if data.startswith("data:"):
            data = data.split(",", 1)[1]
        raw = base64.b64decode(data)
        return Image.open(io.BytesIO(raw)).convert("RGB")

    def generate(self, scene: SceneInput) -> dict:
        """Run one scene and return the standard result dict (or raise)."""
        self.load()
        image = self._decode_image(scene)

        prompt = scene.prompt or FIXED_PROMPT
        if scene.geometry:
            prompt = (
                f"{prompt}\n\nScene geometry:\n{scene.geometry}"
            )

        messages = [
            {
                "role": "user",
                "content": [
                    {"type": "image", "image": image},
                    {"type": "text", "text": prompt},
                ],
            }
        ]
        text = self._processor.apply_chat_template(
            messages, tokenize=False, add_generation_prompt=True
        )
        inputs = self._processor(text=[text], images=[image], return_tensors="pt").to(
            self._model.device
        )

        with __import__("torch").no_grad():
            out = self._model.generate(
                **inputs,
                max_new_tokens=256,
                do_sample=False,
                temperature=None,
                top_p=None,
            )
        generated = out[0][inputs["input_ids"].shape[-1]:]
        raw = self._processor.tokenizer.decode(generated, skip_special_tokens=True)
        return parse_model_result(raw)

    def close(self) -> None:
        self._model = None
        self._processor = None


def screen_batch(
    model: QwenVL,
    scenes: Sequence[SceneInput],
) -> list:
    """Screen a batch; per-scene errors become structured error results."""
    results = []
    for scene in scenes:
        try:
            data = model.generate(scene)
            results.append(ok_result(scene.id, model_result_from_dict(data)))
        except Exception as exc:  # noqa: BLE001 - keep service alive per scene
            results.append(error_result(scene.id, f"inference failed: {exc}"))
    return results


def model_result_from_dict(data: dict):
    """Re-validate a parsed dict through the pydantic model."""
    from .protocol import ModelResult

    return ModelResult(**data)


__all__ = [
    "QwenVL",
    "FIXED_PROMPT",
    "parse_model_result",
    "screen_batch",
    "model_result_from_dict",
]
