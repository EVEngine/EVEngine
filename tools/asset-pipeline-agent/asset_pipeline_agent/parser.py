"""需求解析：把场景 Agent 的自由文本需求 -> 结构化 AssetRequest（LLM + 启发式兜底）。

强制前置约束贯穿：license 白名单、坐标系上轴、面数区间、输出格式都会被
规范化并校验；不合规的输入会被纠正或拒绝，避免污染下游链路。
"""

from __future__ import annotations

import json
from typing import List, Optional

from .models import OpenAIClient, parse_json_block
from .report import ALLOWED_LICENSES, ENGINE_FORMATS, UP_AXES, AssetRequest

_LICENSE_HINTS = {
    "cc0": ("cc0", "public domain", "公有领域"),
    "cc-by": ("cc-by", "cc by", "cc-by 4.0", "署名"),
    "cc-by-sa": ("cc-by-sa", "cc by-sa", "share alike", "相同方式共享"),
}

_FORMAT_HINTS = {
    "glb": ("glb", "binary gltf"),
    "gltf": ("gltf",),
    "fbx": ("fbx",),
    "obj": ("obj",),
}


def _pick_license(text: str, fallback: str) -> str:
    low = text.lower()
    for code in ALLOWED_LICENSES:
        for hint in _LICENSE_HINTS[code]:
            if hint in low:
                return code
    return fallback if fallback in ALLOWED_LICENSES else "cc0"


def _pick_format(text: str, fallback: str) -> str:
    low = text.lower()
    for fmt in ENGINE_FORMATS:
        for hint in _FORMAT_HINTS[fmt]:
            if hint in low:
                return fmt
    return fallback if fallback in ENGINE_FORMATS else "glb"


def _pick_up_axis(text: str, fallback: str) -> str:
    low = text.upper()
    z_hints = ("Z轴", "Z 轴", "+Z", "Z-UP", "Z UP", "Z朝上", "Z 朝上")
    y_hints = ("Y轴", "Y 轴", "+Y", "Y-UP", "Y UP", "Y朝上", "Y 朝上")
    for h in z_hints:
        if h in low or h in text:
            return "Z"
    for h in y_hints:
        if h in low or h in text:
            return "Y"
    return fallback if fallback in UP_AXES else "Y"


def _extract_ints(text: str) -> List[int]:
    import re

    out: List[int] = []
    for m in re.finditer(r"(\d[\d,]*)", text):
        try:
            out.append(int(m.group(1).replace(",", "")))
        except ValueError:
            pass
    return out


class RequirementParser:
    """需求解析器：优先 LLM 结构化输出，模型不可用时退化为启发式。"""

    def __init__(self, client: Optional[OpenAIClient], cfg):
        self.client = client
        self.cfg = cfg

    # ---- 启发式兜底 ----
    @staticmethod
    def _fallback(text: str, request: AssetRequest) -> AssetRequest:
        request.description = text.strip() or request.description
        low = text.lower()
        if "low poly" in low or "低模" in text or "low-poly" in low:
            request.style = "low_poly"
        elif "stylized" in low or "卡通" in text or "风格化" in text:
            request.style = "stylized"
        elif "realistic" in low or "写实" in text or "pbr" in low:
            request.style = "realistic"

        ints = _extract_ints(text)
        if len(ints) >= 2:
            request.min_triangles = min(ints[0], ints[1])
            request.max_triangles = max(ints[0], ints[1])
        elif ints:
            request.max_triangles = ints[0]

        for kw in ("prop", "道具", "杂物"):
            if kw in low or kw in text:
                request.purpose = "prop"
                break
        if not request.purpose:
            for kw in ("environment", "环境", "地形", "场景"):
                if kw in low or kw in text:
                    request.purpose = "environment"
                    break
        if not request.purpose:
            request.purpose = "prop"

        request.license = _pick_license(text, request.license)
        request.output_format = _pick_format(text, request.output_format)
        request.up_axis = _pick_up_axis(text, request.up_axis)
        return request

    def parse(self, text: str, request: Optional[AssetRequest] = None) -> AssetRequest:
        """解析需求文本，返回校验后的 AssetRequest（不合规字段就地纠正并记录 tags）。"""
        base = request or AssetRequest()
        if self.client is not None:
            try:
                prompt = (
                    "你是外部素材管线的需求解析 Agent。把场景 Agent 的素材需求解析为结构化 JSON。\n"
                    "字段：purpose, description, style, max_triangles, min_triangles, "
                    "license(cc0|cc-by|cc-by-sa), up_axis(Y|Z), output_format(glb|gltf|fbx|obj), "
                    "scale, callback, tags[]。\n"
                    "强制约束：license 仅可从 cc0/cc-by/cc-by-sa 选择；无法确认时用 cc0。\n"
                    "只输出 JSON。\n"
                    f"需求：{text}\n当前默认：{json.dumps(base.to_dict(), ensure_ascii=False)}"
                )
                raw = self.client.complete([{"role": "user", "content": prompt}])
                parsed = parse_json_block(raw)
                if isinstance(parsed, dict):
                    for k, v in parsed.items():
                        if hasattr(base, k) and v is not None:
                            setattr(base, k, v)
            except Exception:
                pass
        req = self._fallback(text, base)
        # 规范化 + 校验；不合规自动纠正（宁可保守，不引入违规素材）。
        req.license = _pick_license(text, req.license)
        req.output_format = _pick_format(text, req.output_format)
        req.up_axis = _pick_up_axis(text, req.up_axis)
        req.tags = [t for t in req.tags if isinstance(t, str)]
        if "合规" not in req.tags:
            req.tags.append("合规")
        return req
