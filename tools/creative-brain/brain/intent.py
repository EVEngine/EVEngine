"""Intent parsing: natural language -> structured SceneConfig.

``parse_intent`` is the entry point used by the CLI. When OpenAI is available it
uses the LLM (see :mod:`brain.llm`); otherwise it falls back to the keyword
heuristic :func:`parse_intent_deterministic` so the pipeline always works.
"""

from __future__ import annotations

import random
from typing import Dict, Optional, Tuple

from brain import llm
from brain.schema import (
    ExplorationConfig,
    LightingConfig,
    PropRule,
    RoadConfig,
    SceneConfig,
    TerrainConfig,
    VegetationConfig,
)

_STYLE_KEYWORDS: Dict[str, str] = {
    "fantasy": "fantasy",
    "medieval": "fantasy medieval",
    "cyberpunk": "cyberpunk",
    "neon": "cyberpunk neon",
    "forest": "low-poly forest",
    "wood": "woodland",
    "desert": "desert dunes",
    "snow": "snowy tundra",
    "arctic": "snowy tundra",
    "dungeon": "dark dungeon",
    "cave": "dark cave",
    "space": "space station",
    "cosmic": "space",
    "underwater": "underwater",
    "ruins": "overgrown ruins",
    "city": "urban streets",
}

# Most-specific keyword first so "cave dungeon" prefers cave.cellular.
_TERRAIN_KEYWORDS: list = [
    ("maze", "maze.backtrack"),
    ("cave", "cave.cellular"),
    ("roguelike", "level.roguelike"),
    ("dungeon", "dungeon.bsp"),
    ("heightmap", "terrain.heightmap"),
    ("wfc", "wfc.simple"),
    ("noise", "noise.terrain"),
]

_ROAD_KEYWORDS = {
    "grid": "grid",
    "square": "grid",
    "organic": "organic",
    "winding": "organic",
    "curved": "organic",
    "radial": "radial",
    "ring": "radial",
}

_PLACEMENT_KEYWORDS = {
    "corner": "corners",
    "corners": "corners",
    "center": "centerpiece",
    "centerpiece": "centerpiece",
    "roadside": "roadside",
    "scatter": "scattered",
    "scattered": "scattered",
}


def _match_any(text: str, table: Dict[str, str], default: str) -> Tuple[str, bool]:
    low = text.lower()
    for k, v in table.items():
        if k in low:
            return v, True
    return default, False


def parse_intent(user_prompt: str, seed: Optional[int] = None) -> SceneConfig:
    """Parse a natural-language request into a SceneConfig (LLM-backed if enabled)."""
    if llm.llm_enabled():
        data = llm.parse_intent_with_llm(user_prompt)
        cfg = SceneConfig.from_dict(data)
        if seed is not None:
            cfg.terrain.seed = seed
        return cfg
    return parse_intent_deterministic(user_prompt, seed)


def parse_intent_deterministic(
    user_prompt: str, seed: Optional[int] = None
) -> SceneConfig:
    """Deterministic keyword fallback intent parser (no network / LLM)."""
    text = (user_prompt or "").lower()
    rng = random.Random(seed)

    style, _ = _match_any(text, _STYLE_KEYWORDS, "generic")

    algorithm, _ = "noise.terrain", False
    for kw, alg in _TERRAIN_KEYWORDS:
        if kw in text:
            algorithm = alg
            break

    material = "pbr.soil"
    if "snow" in text or "arctic" in text:
        material = "pbr.stone"
    elif "water" in text or "underwater" in text:
        material = "pbr.water"
    elif "dungeon" in text or "cave" in text or "ruins" in text or "stone" in text:
        material = "pbr.stone"

    time_of_day = "day"
    atmosphere = "sunny midday"
    if "night" in text or "moon" in text:
        time_of_day, atmosphere = "night", "moonlit"
    elif "dusk" in text or "sunset" in text:
        time_of_day, atmosphere = "dusk", "dusky amber"
    elif "dawn" in text or "sunrise" in text:
        time_of_day, atmosphere = "dawn", "soft pink morning"
    elif "storm" in text:
        time_of_day, atmosphere = "day", "stormy overcast"

    road_network, _ = _match_any(text, _ROAD_KEYWORDS, "grid")

    density = 0.2
    if "dense" in text or "lush" in text or "jungle" in text:
        density = 0.6
    elif "sparse" in text or "barren" in text or "desert" in text:
        density = 0.05
    elif "forest" in text or "wood" in text:
        density = 0.45

    width, height = 32, 32
    for kw, val in (("large", 64), ("huge", 96), ("big", 64), ("small", 16)):
        if kw in text:
            width = height = val
            break

    prop_rules = []
    for kw, asset in (
        ("treasure", "chest"),
        ("chest", "chest"),
        ("torch", "torch"),
        ("lantern", "lantern"),
        ("rock", "rock"),
        ("boulder", "rock"),
        ("bush", "bush"),
        ("tree", "tree"),
        ("pillar", "pillar"),
    ):
        if kw in text:
            placement, _ = _match_any(text, _PLACEMENT_KEYWORDS, "scattered")
            prop_rules.append(
                PropRule(asset=asset, count=int(rng.randint(1, 4)), placement=placement)
            )

    seed_value = rng.randint(1, 100000) if seed is None else seed

    return SceneConfig(
        name=_slug(user_prompt or "scene"),
        style=style,
        terrain=TerrainConfig(
            algorithm=algorithm, seed=seed_value, width=width, height=height, material=material
        ),
        lighting=LightingConfig(atmosphere=atmosphere, timeOfDay=time_of_day, intensity=1.0),
        roads=RoadConfig(network=road_network, width=1, noBuildBuffer=1),
        vegetation=VegetationConfig(density=density, clusters=int(density * 6), excludeRoads=True),
        exploration=ExplorationConfig(propRules=prop_rules),
        width=width,
        height=height,
    )


def _slug(text: str) -> str:
    keep = "".join(c for c in text.lower() if c.isalnum() or c in " -_").strip()
    keep = " ".join(keep.split())[:40]
    return keep.replace(" ", "_") or "scene"
