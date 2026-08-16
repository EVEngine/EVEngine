"""OpenAI 兼容 / opencode-go 模型客户端（本地小模型 + 远端 VLM + 修复规划）。"""

from __future__ import annotations

import base64
import json
import urllib.request
import urllib.error
from typing import Any, Dict, List, Optional


class ModelError(RuntimeError):
    pass


def _image_content(data_url: str) -> Dict[str, Any]:
    return {"type": "image_url", "image_url": {"url": data_url}}


def make_data_url(image_bytes: bytes, mime: str = "image/png") -> str:
    b64 = base64.b64encode(image_bytes).decode("ascii")
    return f"data:{mime};base64,{b64}"


class OpenAIClient:
    """极简 OpenAI 兼容 /chat/completions 客户端（仅标准库，零依赖）。

    base_url 可指向任意 OpenAI 兼容网关（OpenAI、Ollama、vLLM、opencode-go 等）。
    """

    def __init__(self, spec: Dict[str, Any]):
        self.base_url = (spec.get("base_url") or "").rstrip("/")
        self.model = spec.get("model") or ""
        self.api_key = spec.get("api_key") or ""
        self.vision = bool(spec.get("vision", False))
        self.temperature = float(spec.get("temperature", 0.2))
        self.max_tokens = int(spec.get("max_tokens", 1024))
        self.timeout = float(spec.get("timeout", 60))

    def complete(self, messages: List[Dict[str, Any]], images: Optional[List[str]] = None) -> str:
        """messages 为 OpenAI 格式；images 为 data-url 列表，拼接为 image_url 内容块。"""
        if self.vision and images:
            content: List[Dict[str, Any]] = [_image_content(u) for u in images]
            for m in messages:
                txt = m.get("content")
                if isinstance(txt, str) and txt:
                    content.insert(0, {"type": "text", "text": txt})
            payload_messages = [{"role": m.get("role", "user"), "content": content}] + messages[1:]
            # 上面拼接粗糙，改为规范化：首条 user 消息挂图
            payload_messages = [dict(m) for m in messages]
            if payload_messages and payload_messages[-1]["role"] == "user":
                last = dict(payload_messages[-1])
                if isinstance(last.get("content"), str):
                    last["content"] = [{"type": "text", "text": last["content"]}]
                last["content"] = list(last["content"]) + [_image_content(u) for u in images]
                payload_messages[-1] = last
        else:
            payload_messages = [dict(m) for m in messages]

        body: Dict[str, Any] = {
            "model": self.model,
            "messages": payload_messages,
            "temperature": self.temperature,
            "max_tokens": self.max_tokens,
        }
        req = urllib.request.Request(
            f"{self.base_url}/chat/completions",
            data=json.dumps(body).encode("utf-8"),
            headers={
                "Content-Type": "application/json",
                "Authorization": f"Bearer {self.api_key}" if self.api_key else "",
            },
            method="POST",
        )
        try:
            with urllib.request.urlopen(req, timeout=self.timeout) as resp:
                data = json.loads(resp.read().decode("utf-8"))
        except urllib.error.HTTPError as e:
            detail = e.read().decode("utf-8", "replace")[:500]
            raise ModelError(f"{self.model}: HTTP {e.code} {detail}") from e
        except urllib.error.URLError as e:
            raise ModelError(f"{self.model}: {e.reason}") from e

        try:
            return data["choices"][0]["message"]["content"] or ""
        except (KeyError, IndexError) as e:
            raise ModelError(f"{self.model}: unexpected response {str(data)[:300]}") from e


def parse_json_block(text: str) -> Optional[Any]:
    """从模型输出中提取 JSON：取最外层 {..} 或 [..] 平衡块，优先对象。"""
    if not text:
        return None
    t = text.strip()
    if t.startswith("```"):
        t = t.split("```", 2)[1] if "```" in t[3:] else t
        t = t.strip().lstrip("json").strip()
    try:
        return json.loads(t)
    except json.JSONDecodeError:
        pass

    opens = {"{": "}", "[": "]"}
    for s, ch in enumerate(t):
        if ch not in opens:
            continue
        depth, in_str, esc = 0, False, False
        for p in range(s, len(t)):
            c = t[p]
            if in_str:
                if esc:
                    esc = False
                elif c == "\\":
                    esc = True
                elif c == '"':
                    in_str = False
                continue
            if c == '"':
                in_str = True
            elif c in opens:
                depth += 1
            elif c == opens[ch]:
                depth -= 1
                if depth == 0:
                    try:
                        return json.loads(t[s : p + 1])
                    except json.JSONDecodeError:
                        break
    return None
