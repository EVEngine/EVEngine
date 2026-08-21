"""Pipeline assembly helpers: build and serialize a GenerationPlan.

Also provides ``run_pipeline`` — the single high-level entry the CLI calls —
that chains intent parsing -> asset matching -> layout generation, and keeps the
resource-agent handoff point explicit for when assets are missing.
"""

from __future__ import annotations

import os
from pathlib import Path
from typing import Optional, Tuple

from brain import intent, layout
from brain.assets import Catalog
from brain.resource_proto import ResourceBroker
from brain.schema import GenerationPlan, SceneConfig

DEFAULT_CATALOG = Path(__file__).resolve().parent.parent / "catalogs" / "assets.example.json"


def build_plan(
    user_prompt: str,
    catalog_path: Optional[str] = None,
    seed: Optional[int] = None,
) -> GenerationPlan:
    """Run the full Creative Brain pipeline and return a GenerationPlan."""
    cfg: SceneConfig = intent.parse_intent(user_prompt, seed=seed)
    catalog = Catalog.load(catalog_path or str(DEFAULT_CATALOG))
    whitelist, missing = catalog.match_to_config(cfg)
    plan = layout.apply_layout(cfg, whitelist, missing, seed=seed or cfg.terrain.seed)
    return plan


def save_plan(plan: GenerationPlan, out_path: str) -> str:
    os.makedirs(os.path.dirname(os.path.abspath(out_path)) or ".", exist_ok=True)
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(plan.to_json())
    return out_path


def run_pipeline(
    user_prompt: str,
    catalog_path: Optional[str] = None,
    seed: Optional[int] = None,
    out_path: Optional[str] = None,
) -> Tuple[GenerationPlan, str]:
    """Build the plan, trigger async resource fetch for missing assets, save it."""
    plan = build_plan(user_prompt, catalog_path=catalog_path, seed=seed)

    if plan.missing_assets:
        broker = ResourceBroker.from_env()
        for asset_id in plan.missing_assets:
            broker.request(asset_id)

    saved = save_plan(plan, out_path) if out_path else ""
    return plan, saved
