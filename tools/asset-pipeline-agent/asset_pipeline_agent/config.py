"""配置加载与默认值（深合并 + 合规白名单校验）。"""

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
    "license": {
        # 合规白名单；sources 检索时据此硬过滤，并永久写入 attribution 元数据。
        "allowed": ["cc0", "cc-by", "cc-by-sa"],
        "allow_cc_by": True,          # cc-by 需要致谢，仍合规
        "allow_cc_by_sa": True,
    },
    "models": {
        "parser": {
            "enabled": True,
            "base_url": "https://api.openai.com/v1",
            "model": "gpt-4o-mini",
            "api_key": "",
            "vision": False,
            "temperature": 0.0,
            "max_tokens": 1024,
        },
        "selector": {
            "enabled": True,
            "base_url": "https://api.openai.com/v1",
            "model": "gpt-4o-mini",
            "api_key": "",
            "vision": False,
            "temperature": 0.0,
            "max_tokens": 512,
        },
    },
    "sources": {
        "poly_haven": {
            "enabled": True,
            "index_url": "https://api.polyhaven.com/assets",
            "file_root": "https://dl.polyhaven.org/file/ph-assets",
            "type": "model",
            "timeout_s": 30,
        },
        "sketchfab": {
            "enabled": False,          # 需 SKETCHFAB_TOKEN；默认关闭
            "search_url": "https://api.sketchfab.com/v3/search",
            "download_url": "https://api.sketchfab.com/v3/models/{uid}/download",
            "token_env": "SKETCHFAB_TOKEN",
            "timeout_s": 30,
        },
    },
    "cache": {
        "dir": "asset_cache",
        "keep_archives": True,         # 保留原始压缩包（含版权信息）
    },
    "blender": {
        "python": "py -3.13",          # 安装了 bpy 的 Python 解释器
        "script": "blender_scripts/standardize.py",
        "timeout_s": 600,
        "defaults": {
            "up_axis": "Y",
            "scale": 1.0,
            "max_triangles": 200000,
            "decimate": True,
            "recalc_normals": True,
            "fix_pivot": True,
            "clean_mesh": True,
            "standardize_material": True,
            "compress_textures": True,
            "texture_max_size": 2048,
            "output_format": "glb",
        },
    },
    "mcp": {
        "transport": "bridge",         # bridge | tcp
        "host": "127.0.0.1",
        "port": 7529,
        "bridge_js": "../eve-mcp/server.js",
        "node": "node",
        "connect_timeout_ms": 5000,
        # 逻辑能力 -> 引擎 MCP 工具名与参数（引擎侧 agent 落地后在此对齐）。
        "tools": {
            "ingest": {"name": "eve_asset_ingest", "args": {"uri": "", "tags": [], "metadata": {}}},
            "notify": {"name": "eve_asset_notify", "args": {"callback": "", "status": "", "payload": {}}},
        },
    },
    "iteration": {
        "max_candidates": 5,           # 尝试候选硬上限
        "max_react_steps": 8,
    },
    "output": {
        "work_dir": "asset_work",
        "report_path": "asset_pipeline_report.json",
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

    def model(self, tier: str) -> Dict[str, Any]:
        return self.raw["models"][tier]

    def mcp_tool(self, capability: str) -> Dict[str, Any]:
        return self.raw["mcp"]["tools"].get(capability) or {"name": capability, "args": {}}

    @property
    def max_candidates(self) -> int:
        return int(self.raw["iteration"]["max_candidates"])

    @property
    def output(self) -> Dict[str, Any]:
        return self.raw["output"]


def load_config(path: Optional[str] = None, overrides: Optional[Dict[str, Any]] = None) -> Config:
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
