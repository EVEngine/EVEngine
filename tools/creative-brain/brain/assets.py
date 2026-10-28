"""Asset matching: query the engine asset catalog by tag -> asset whitelist.

The catalog is a JSON array (see ``catalogs/assets.schema.json`` and
``catalogs/assets.example.json``). Each entry carries ``id``, ``category``,
``tags``, an optional procgen ``recipe`` and ``style_tags`` used to score
style fit. ``match_to_config`` builds the whitelist the layout generator and
batch driver consume, and reports assets that are missing (handed to the
resource-agent protocol so they can be fetched asynchronously).
"""

from __future__ import annotations

import json
from dataclasses import dataclass
from typing import List, Optional, Set, Tuple

from brain import llm
from brain.schema import AssetRef, SceneConfig


@dataclass
class Catalog:
    assets: List[AssetRef]

    @classmethod
    def load(cls, path: str) -> "Catalog":
        with open(path, "r", encoding="utf-8") as f:
            raw = json.load(f)
        entries = raw.get("assets", raw) if isinstance(raw, dict) else raw
        return cls(assets=[AssetRef.from_dict(e) for e in entries])

    def ids(self) -> Set[str]:
        return {a.id for a in self.assets}

    def categories(self) -> List[str]:
        return sorted({a.category for a in self.assets})

    def query(
        self,
        category: Optional[str] = None,
        tags: Optional[List[str]] = None,
        style_tags: Optional[List[str]] = None,
    ) -> List[AssetRef]:
        tags = tags or []
        style_tags = style_tags or []
        out = []
        for a in self.assets:
            if category and a.category != category:
                continue
            if tags and not any(t in a.tags for t in tags):
                continue
            if style_tags and not any(s in a.style_tags for s in style_tags):
                continue
            out.append(a)
        return out

    def style_score(self, asset: AssetRef, config: SceneConfig) -> int:
        """Count overlapping style tags between the asset and the scene style."""
        style_words = set(config.style.lower().replace("/", " ").split())
        return sum(1 for t in asset.style_tags if t in style_words)

    def match_to_config(
        self, config: SceneConfig
    ) -> Tuple[List[AssetRef], List[str]]:
        """Return (whitelist, missing_asset_ids) for the whole scene config.

        The whitelist covers:
          - the terrain ``material`` (a recipe) if the catalog has it;
          - every ``exploration.propRules[].asset``;
          - a default 'decoration' pool so layout rules can scatter generically.
        """
        wanted: List[AssetRef] = []
        missing: List[str] = []
        seen: Set[str] = set()

        def add(asset: AssetRef) -> None:
            if asset.id in seen:
                return
            seen.add(asset.id)
            wanted.append(asset)

        material = config.terrain.material
        found = self.query(tags=["material"]) or self.query(
            style_tags=[material.split(".")[-1]]
        )
        found = [a for a in found if a.recipe == material] or found
        if found:
            add(found[0])
        else:
            missing.append(material)

        for rule in config.exploration.propRules:
            candidates = self.query(tags=["prop", rule.asset]) or self.query(
                tags=[rule.asset]
            )
            candidates = sorted(
                candidates, key=lambda a: self.style_score(a, config), reverse=True
            )
            if candidates:
                add(candidates[0])
            else:
                missing.append(rule.asset)

        decor = self.query(category="decoration")
        if decor:
            decor = sorted(
                decor, key=lambda a: self.style_score(a, config), reverse=True
            )
            add(decor[0])

        return wanted, sorted(set(missing))


def match_config_to_assets(config: SceneConfig, catalog_path: str):
    """Convenience wrapper: load catalog, match, optionally let LLM rank picks."""
    catalog = Catalog.load(catalog_path)
    whitelist, missing = catalog.match_to_config(config)
    if llm.llm_enabled() and whitelist:
        choices = [a.id for a in whitelist]
        top = llm.pick_with_llm(
            f"Best asset fit for style '{config.style}' with material "
            f"'{config.terrain.material}'",
            choices,
            "Choose the single most on-style asset id",
            fallback_index=0,
        )
        whitelist = [a for a in whitelist if a.id == top] + [
            a for a in whitelist if a.id != top
        ]
    return catalog, whitelist, missing
