"""Config loading for story-scene-agent (deep-merge over defaults)."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any, Dict

REPO_ROOT = Path(__file__).resolve().parents[3]
SCENE_DIRECTOR_NUT = REPO_ROOT / "src" / "scripts" / "scene_director.nut"
CREATIVE_BRAIN_DIR = REPO_ROOT / "tools" / "creative-brain"
SCENE_QC_DIR = REPO_ROOT / "tools" / "scene-qc-agent"


def _deep_merge(base: Dict[str, Any], override: Dict[str, Any]) -> Dict[str, Any]:
    out = dict(base)
    for k, v in override.items():
        if isinstance(v, dict) and isinstance(out.get(k), dict):
            out[k] = _deep_merge(out[k], v)
        else:
            out[k] = v
    return out


DefaultConfig: Dict[str, Any] = {
    "engine": {"host": "127.0.0.1", "port": 7529, "connect_timeout_s": 5.0},
    "creative": {
        "catalog": str(CREATIVE_BRAIN_DIR / "catalogs" / "assets.example.json"),
        "seed": 2026,
        "tile": 2.0,
    },
    "stage": {
        "default_camera": {"eye": [0.0, 12.0, 22.0], "target": [0.0, 1.0, 0.0], "fov": 55.0},
        "focal_distance": 2.0,
    },
    "qc": {
        "config": str(SCENE_QC_DIR / "config.example.json"),
        "max_rounds": 3,
        "targets": ["scene"],
        "save_snapshots": True,
    },
    "output": {"out_dir": "ai_stage_out", "report": "story_scene_report.json"},
}


def load_config(path: str = "") -> Dict[str, Any]:
    """Return deep-merged config (defaults <- file)."""
    cfg = json.loads(json.dumps(DefaultConfig))
    if path:
        with open(path, "r", encoding="utf-8") as f:
            cfg = _deep_merge(cfg, json.load(f))
    return cfg


def load_scene_director_source() -> str:
    try:
        return SCENE_DIRECTOR_NUT.read_text(encoding="utf-8")
    except OSError:
        return ""


def load_qc_config(path: str = "") -> Dict[str, Any]:
    """Load the scene-qc agent config (defaults to its example file)."""
    p = Path(path) if path else Path(DefaultConfig["qc"]["config"])
    if not p.is_absolute():
        p = (REPO_ROOT / p).resolve()
    with open(p, "r", encoding="utf-8") as f:
        return json.load(f)