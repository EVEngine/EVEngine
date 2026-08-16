"""Layout rule generator.

Emits the four canonical layout strategies the Creative Brain requires:
  1. road_buffer      - keep a no-build buffer band beside roads;
  2. layer            - near/mid/far depth layering for depth perception;
  3. scatter          - asymmetric, jittered, non-grid placement;
  4. corner           - weighted decoration at corners / junctions.

Each rule is a :class:`brain.schema.LayoutRule`. For scatter we also compute
concrete cell coordinates so the batch driver can place assets without
re-deriving geometry.
"""

from __future__ import annotations

import random
from typing import List, Tuple

from brain import llm
from brain.schema import (
    AssetRef,
    GenerationPlan,
    GenerationStep,
    LayoutRule,
    SceneConfig,
)

LAYER_NAMES = ("foreground", "midground", "background")


def _road_band(config: SceneConfig) -> List[Tuple[int, int]]:
    """Cells belonging to roads + their no-build buffer band (simple grid model)."""
    w, h = config.width, config.height
    buffer = max(0, config.roads.noBuildBuffer)
    road_cells = set()
    if config.roads.network == "grid":
        step = max(2, min(w, h) // 4)
        for x in range(0, w, step):
            for y in range(h):
                road_cells.add((x, y))
        for y in range(0, h, step):
            for x in range(w):
                road_cells.add((x, y))
    elif config.roads.network == "radial":
        cx, cy = w // 2, h // 2
        for x in range(w):
            for y in range(h):
                if (x - cx) % 4 == 0 and (y - cy) % 4 == 0:
                    road_cells.add((x, y))
    else:  # organic
        for x in range(w):
            for y in range(h):
                if (x + y) % 5 == 0:
                    road_cells.add((x, y))

    band = set(road_cells)
    for (x, y) in list(road_cells):
        for dx in range(-buffer, buffer + 1):
            for dy in range(-buffer, buffer + 1):
                if 0 <= x + dx < w and 0 <= y + dy < h:
                    band.add((x + dx, y + dy))
    return sorted(band)


def _usable_cells(config: SceneConfig) -> List[Tuple[int, int]]:
    band = set(_road_band(config))
    all_cells = [
        (x, y)
        for x in range(config.width)
        for y in range(config.height)
        if (x, y) not in band
    ]
    return all_cells


def generate_layout(config: SceneConfig, whitelist: List[AssetRef]) -> List[LayoutRule]:
    """Generate layout rules from a scene config + asset whitelist."""
    rules: List[LayoutRule] = []
    w, h = config.width, config.height
    buffer = config.roads.noBuildBuffer

    rules.append(
        LayoutRule(
            kind="road_buffer",
            description=(
                f"Keep a {buffer}-cell no-build band on both sides of the "
                f"'{config.roads.network}' road network; vegetation and props avoid it."
            ),
            params={"buffer": buffer, "network": config.roads.network},
        )
    )

    depth_layers = 3 if max(w, h) >= 32 else 2
    rules.append(
        LayoutRule(
            kind="layer",
            description=(
                f"Depth layers = {depth_layers}: "
                + " > ".join(LAYER_NAMES[:depth_layers])
                + " with decreasing density toward the back."
            ),
            params={"layers": list(LAYER_NAMES[:depth_layers])},
        )
    )

    rules.append(
        LayoutRule(
            kind="scatter",
            description=(
                "Asymmetric, jittered scatter with uneven spacing (never uniform "
                "grid) for a natural, hand-placed feel."
            ),
            params={"jitter": 1.0, "asymmetry": True},
        )
    )

    rules.append(
        LayoutRule(
            kind="corner",
            description=(
                "Weighted decoration at map corners and road junctions so focal "
                "points feel deliberate rather than random."
            ),
            params={"corner_weight": 2.0},
        )
    )
    return rules


def build_placement_steps(
    config: SceneConfig, whitelist: List[AssetRef], seed: int = 1
) -> List[GenerationStep]:
    """Compute concrete scatter + corner placements and return batch steps.

    Deterministic given ``seed``. Respects the road no-build band. Returns
    ``place`` steps (asset in a layer) plus a terrain + lighting + material step.
    """
    rng = random.Random(seed)
    cells = _usable_cells(config)
    if not cells:
        return []

    band = set(_road_band(config))
    corner_pts = [
        (1, 1),
        (config.width - 2, 1),
        (1, config.height - 2),
        (config.width - 2, config.height - 2),
        (config.width // 2, config.height // 2),
    ]
    steps: List[GenerationStep] = []

    steps.append(
        GenerationStep(
            action="terrain",
            target=config.terrain.algorithm,
            params={
                "seed": config.terrain.seed,
                "width": config.width,
                "height": config.height,
                "material": config.terrain.material,
            },
        )
    )
    steps.append(
        GenerationStep(
            action="set_lighting",
            target=config.terrain.material,
            params={
                "atmosphere": config.lighting.atmosphere,
                "timeOfDay": config.lighting.timeOfDay,
                "intensity": config.lighting.intensity,
            },
        )
    )

    target_cells = max(1, int(config.vegetation.density * len(cells)))
    rng.shuffle(cells)

    decor_assets = [a for a in whitelist if a.category in ("decoration", "plant", "prop")]
    for i, (x, y) in enumerate(cells[:target_cells]):
        layer = LAYER_NAMES[0 if i < target_cells // 3 else (1 if i < 2 * target_cells // 3 else 2)]
        asset = decor_assets[i % len(decor_assets)] if decor_assets else None
        if asset is None:
            continue
        steps.append(
            GenerationStep(
                action="place",
                target=asset.id,
                params={"x": x, "y": y, "layer": layer, "seed": rng.randint(0, 100000)},
            )
        )

    for idx, (x, y) in enumerate(corner_pts):
        if (x, y) in band or not decor_assets:
            continue
        steps.append(
            GenerationStep(
                action="place",
                target=decor_assets[idx % len(decor_assets)].id,
                params={
                    "x": x,
                    "y": y,
                    "layer": "foreground",
                    "corner": True,
                    "seed": rng.randint(0, 100000),
                },
            )
        )
    return steps


def apply_layout(
    config: SceneConfig, whitelist: List[AssetRef], missing_assets: List[str], seed: int = 1
) -> GenerationPlan:
    """Assemble a complete GenerationPlan from config + whitelist."""
    rules = generate_layout(config, whitelist)
    steps = build_placement_steps(config, whitelist, seed=seed)
    if llm.llm_enabled() and rules:
        llm.pick_with_llm(
            f"Which layout rule best matches style '{config.style}'?",
            [r.kind for r in rules],
            "Choose one layout strategy",
            fallback_index=0,
        )
    return GenerationPlan(
        config=config,
        whitelist=whitelist,
        missing_assets=missing_assets,
        layout_rules=rules,
        steps=steps,
    )
