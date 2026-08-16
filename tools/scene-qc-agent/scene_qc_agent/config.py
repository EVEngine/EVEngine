"""配置加载与默认值。"""

from __future__ import annotations

import json
import os
from typing import Any, Dict, Optional


def _deep_merge(base: Dict[str, Any], override: Dict[str, Any]) -> Dict[str, Any]:
    out = dict(base)
    for k, v in override.items():
        if isinstance(v, dict) and isinstance(out.get(k), dict):
            out[k] = _deep_merge(out[k], v)
        else:
            out[k] = v
    return out


DEFAULTS: Dict[str, Any] = {
    "engine": {
        "transport": "bridge",  # bridge | tcp
        "host": "127.0.0.1",
        "port": 7529,
        "bridge_js": "../eve-mcp/server.js",
        "node": "node",
        "connect_timeout_ms": 5000,
    },
    "mcp_tools": {
        # 逻辑能力 -> 引擎 MCP 工具名与参数（其他 agent 落地后在此对齐）
        "status": {"name": "eve_status", "args": {}},
        "eval": {"name": "eve_eval", "args": {"expression": ""}},
        "camera_generate": {"name": "eve_camera_generate", "args": {"targets": [], "count": 6}},
        "screenshot": {"name": "eve_screenshot", "args": {"camera": {}}},
        "scene_info": {"name": "eve_scene_info", "args": {"frame_id": "", "targets": []}},
        "scene_modify": {"name": "eve_scene_modify", "args": {"action": "", "target": "", "params": {}}},
        "run_script": {"name": "eve_run_script", "args": {"source": ""}},
    },
    "models": {
        "local_screen": {
            "enabled": True,
            "base_url": "http://127.0.0.1:11434/v1",
            "model": "qwen2.5-vl:3b",
            "api_key": "ollama",
            "vision": True,
            "temperature": 0.2,
            "max_tokens": 512,
        },
        "vlm_review": {
            "enabled": True,
            "base_url": "https://api.openai.com/v1",
            "model": "gpt-4o",
            "api_key": "",
            "vision": True,
            "temperature": 0.2,
            "max_tokens": 1024,
        },
        "fix_planner": {
            "enabled": True,
            "base_url": "https://api.openai.com/v1",
            "model": "gpt-4o",
            "api_key": "",
            "vision": False,
            "temperature": 0.0,
            "max_tokens": 1024,
        },
    },
    "fusion": {
        "weights": {"geometry": 0.35, "local": 0.35, "vlm": 0.30},
        "screen_escalate": 0.7,   # local 得分低于此 -> 送远端 VLM
        "pass_threshold": 0.75,   # 融合得分低于此 -> 判定不达标
        "severity_order": ["critical", "high", "medium", "low"],
    },
    "iteration": {
        "max_rounds": 3,          # 优化硬上限，避免无限循环
        "max_react_steps": 8,
    },
    "cameras": {
        "count": 6,
        "jitter": 0.15,
    },
    "output": {
        "report_path": "scene_qc_report.json",
        "save_snapshots": False,
        "trace": False,
    },
}


class Config:
    def __init__(self, raw: Dict[str, Any], source: str = ""):
        self.raw = raw
        self.source = source

    def __getitem__(self, key: str) -> Any:
        return self.raw[key]

    def get(self, key: str, default: Any = None) -> Any:
        return self.raw.get(key, default)

    def tool(self, capability: str) -> Dict[str, Any]:
        return self.raw["mcp_tools"].get(capability) or {
            "name": capability,
            "args": {},
        }

    def model(self, tier: str) -> Dict[str, Any]:
        return self.raw["models"][tier]

    @property
    def max_rounds(self) -> int:
        return int(self.raw["iteration"]["max_rounds"])

    @property
    def fusion(self) -> Dict[str, Any]:
        return self.raw["fusion"]

    @property
    def output(self) -> Dict[str, Any]:
        return self.raw["output"]


def load_config(path: Optional[str] = None, overrides: Optional[Dict[str, Any]] = None) -> Config:
    """加载 JSON 配置，与默认值深合并；缺失文件时仅用默认值。"""
    raw = _deep_merge(DEFAULTS, {})
    source = ""
    if path and os.path.exists(path):
        with open(path, "r", encoding="utf-8") as fh:
            data = json.load(fh)
        raw = _deep_merge(raw, data)
        source = path
    if overrides:
        raw = _deep_merge(raw, overrides)
    return Config(raw, source)
