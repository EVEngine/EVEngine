"""Data models and JSON schemas for the Creative Brain pipeline.

These dataclasses are the contract between the intent parser, the asset
matcher, the layout generator and the MCP batch driver. Every model round-trips
through plain JSON so a plan can be produced by a (possibly remote) OpenAI call
and consumed locally (or pushed to the engine).
"""

from __future__ import annotations

import dataclasses
from dataclasses import dataclass, field, asdict
from typing import Any, Dict, List, Optional

# JSON Schema embedded as a dict so callers can pass it directly to OpenAI's
# structured-output / function-calling layer, or validate plans offline.
SCENE_CONFIG_SCHEMA: Dict[str, Any] = {
    "type": "object",
    "additionalProperties": False,
    "required": [
        "name",
        "style",
        "terrain",
        "lighting",
        "roads",
        "vegetation",
        "exploration",
        "size",
    ],
    "properties": {
        "name": {"type": "string", "description": "Short human label for the scene."},
        "style": {
            "type": "string",
            "description": "Art / genre style, e.g. 'fantasy', 'cyberpunk', 'low-poly forest'.",
        },
        "terrain": {
            "type": "object",
            "additionalProperties": False,
            "required": ["algorithm", "seed", "width", "height"],
            "properties": {
                "algorithm": {
                    "type": "string",
                    "description": "A registered procgen algorithm id (see engine Procgen module).",
                    "enum": [
                        "dungeon.bsp",
                        "cave.cellular",
                        "cave.drunkard",
                        "maze.backtrack",
                        "noise.terrain",
                        "terrain.heightmap",
                        "wfc.simple",
                        "level.roguelike",
                    ],
                },
                "seed": {"type": "integer", "minimum": 0},
                "width": {"type": "integer", "minimum": 8, "maximum": 512},
                "height": {"type": "integer", "minimum": 8, "maximum": 512},
                "material": {
                    "type": "string",
                    "description": "Ground material recipe, e.g. 'pbr.soil'.",
                },
            },
        },
        "lighting": {
            "type": "object",
            "additionalProperties": False,
            "required": ["atmosphere", "timeOfDay", "intensity"],
            "properties": {
                "atmosphere": {
                    "type": "string",
                    "description": "Lighting mood, e.g. 'dusk', 'moonlit', 'sunny midday'.",
                },
                "timeOfDay": {
                    "type": "string",
                    "enum": ["dawn", "day", "dusk", "night"],
                },
                "intensity": {"type": "number", "minimum": 0.0, "maximum": 2.0},
            },
        },
        "roads": {
            "type": "object",
            "additionalProperties": False,
            "required": ["network", "width", "noBuildBuffer"],
            "properties": {
                "network": {
                    "type": "string",
                    "description": "Road layout style, e.g. 'grid', 'organic', 'radial'.",
                },
                "width": {"type": "integer", "minimum": 1},
                "noBuildBuffer": {
                    "type": "integer",
                    "minimum": 0,
                    "description": "Cells kept empty on either side of a road.",
                },
            },
        },
        "vegetation": {
            "type": "object",
            "additionalProperties": False,
            "required": ["density", "clusters", "excludeRoads"],
            "properties": {
                "density": {
                    "type": "number",
                    "minimum": 0.0,
                    "maximum": 1.0,
                    "description": "Fraction of usable cells to scatter vegetation into.",
                },
                "clusters": {
                    "type": "integer",
                    "minimum": 0,
                    "description": "Number of grouped vegetation clusters.",
                },
                "excludeRoads": {"type": "boolean"},
            },
        },
        "exploration": {
            "type": "object",
            "additionalProperties": False,
            "required": ["propRules"],
            "properties": {
                "propRules": {
                    "type": "array",
                    "items": {
                        "type": "object",
                        "additionalProperties": False,
                        "required": ["asset", "count", "placement"],
                        "properties": {
                            "asset": {"type": "string"},
                            "count": {"type": "integer", "minimum": 0},
                            "placement": {
                                "type": "string",
                                "description": "e.g. 'corners', 'scattered', 'roadside', 'centerpiece'.",
                            },
                        },
                    },
                }
            },
        },
        "size": {
            "type": "object",
            "additionalProperties": False,
            "required": ["width", "height"],
            "properties": {
                "width": {"type": "integer", "minimum": 8, "maximum": 512},
                "height": {"type": "integer", "minimum": 8, "maximum": 512},
            },
        },
    },
}


@dataclass
class TerrainConfig:
    algorithm: str = "noise.terrain"
    seed: int = 1
    width: int = 32
    height: int = 32
    material: str = "pbr.soil"

    @classmethod
    def from_dict(cls, d: dict) -> "TerrainConfig":
        return cls(
            algorithm=d.get("algorithm", "noise.terrain"),
            seed=int(d.get("seed", 1)),
            width=int(d.get("width", 32)),
            height=int(d.get("height", 32)),
            material=d.get("material", "pbr.soil"),
        )


@dataclass
class LightingConfig:
    atmosphere: str = "sunny midday"
    timeOfDay: str = "day"
    intensity: float = 1.0

    @classmethod
    def from_dict(cls, d: dict) -> "LightingConfig":
        return cls(
            atmosphere=d.get("atmosphere", "sunny midday"),
            timeOfDay=d.get("timeOfDay", "day"),
            intensity=float(d.get("intensity", 1.0)),
        )


@dataclass
class RoadConfig:
    network: str = "grid"
    width: int = 1
    noBuildBuffer: int = 1

    @classmethod
    def from_dict(cls, d: dict) -> "RoadConfig":
        return cls(
            network=d.get("network", "grid"),
            width=int(d.get("width", 1)),
            noBuildBuffer=int(d.get("noBuildBuffer", 1)),
        )


@dataclass
class VegetationConfig:
    density: float = 0.2
    clusters: int = 2
    excludeRoads: bool = True

    @classmethod
    def from_dict(cls, d: dict) -> "VegetationConfig":
        return cls(
            density=float(d.get("density", 0.2)),
            clusters=int(d.get("clusters", 2)),
            excludeRoads=bool(d.get("excludeRoads", True)),
        )


@dataclass
class PropRule:
    asset: str = ""
    count: int = 0
    placement: str = "scattered"

    @classmethod
    def from_dict(cls, d: dict) -> "PropRule":
        return cls(
            asset=d.get("asset", ""),
            count=int(d.get("count", 0)),
            placement=d.get("placement", "scattered"),
        )


@dataclass
class ExplorationConfig:
    propRules: List[PropRule] = field(default_factory=list)

    @classmethod
    def from_dict(cls, d: dict) -> "ExplorationConfig":
        return cls(propRules=[PropRule.from_dict(r) for r in d.get("propRules", [])])


@dataclass
class SceneConfig:
    name: str = "untitled"
    style: str = "generic"
    terrain: TerrainConfig = field(default_factory=TerrainConfig)
    lighting: LightingConfig = field(default_factory=LightingConfig)
    roads: RoadConfig = field(default_factory=RoadConfig)
    vegetation: VegetationConfig = field(default_factory=VegetationConfig)
    exploration: ExplorationConfig = field(default_factory=ExplorationConfig)
    width: int = 32
    height: int = 32

    @classmethod
    def from_dict(cls, d: dict) -> "SceneConfig":
        size = d.get("size") or {}
        return cls(
            name=d.get("name", "untitled"),
            style=d.get("style", "generic"),
            terrain=TerrainConfig.from_dict(d.get("terrain", {})),
            lighting=LightingConfig.from_dict(d.get("lighting", {})),
            roads=RoadConfig.from_dict(d.get("roads", {})),
            vegetation=VegetationConfig.from_dict(d.get("vegetation", {})),
            exploration=ExplorationConfig.from_dict(d.get("exploration", {})),
            width=int(size.get("width", 32) or 32),
            height=int(size.get("height", 32) or 32),
        )

    def to_dict(self) -> dict:
        d = asdict(self)
        d["size"] = {"width": self.width, "height": self.height}
        return d


@dataclass
class AssetRef:
    """A single catalog entry matched by the asset matcher."""

    id: str
    category: str
    tags: List[str]
    recipe: Optional[str] = None  # procgen recipe id (tex.* / pbr.* / mesh.*)
    style_tags: List[str] = field(default_factory=list)
    source: str = "local"  # "local" | "remote" (via resource agent)

    @classmethod
    def from_dict(cls, d: dict) -> "AssetRef":
        return cls(
            id=d.get("id", ""),
            category=d.get("category", "prop"),
            tags=list(d.get("tags", [])),
            recipe=d.get("recipe"),
            style_tags=list(d.get("style_tags", [])),
            source=d.get("source", "local"),
        )


@dataclass
class LayoutRule:
    """One concrete layout strategy emitted by the layout rule generator."""

    kind: str  # "road_buffer" | "layer" | "scatter" | "corner"
    description: str = ""
    params: Dict[str, Any] = field(default_factory=dict)

    def to_dict(self) -> dict:
        return {"kind": self.kind, "description": self.description, "params": self.params}


@dataclass
class GenerationStep:
    """A single batch action pushed to the engine via MCP."""

    action: str  # "terrain" | "texture" | "mesh" | "place" | "set_lighting"
    target: str  # e.g. algorithm id / recipe id / asset id / layer name
    params: Dict[str, Any] = field(default_factory=dict)

    def to_dict(self) -> dict:
        return {"action": self.action, "target": self.target, "params": self.params}


@dataclass
class GenerationPlan:
    """Standardized output of the Creative Brain: config + whitelist + layout + steps."""

    config: SceneConfig
    whitelist: List[AssetRef] = field(default_factory=list)
    missing_assets: List[str] = field(default_factory=list)
    layout_rules: List[LayoutRule] = field(default_factory=list)
    steps: List[GenerationStep] = field(default_factory=list)

    def to_dict(self) -> dict:
        return {
            "config": self.config.to_dict(),
            "whitelist": [asdict(a) for a in self.whitelist],
            "missing_assets": self.missing_assets,
            "layout_rules": [r.to_dict() for r in self.layout_rules],
            "steps": [s.to_dict() for s in self.steps],
        }

    def to_json(self, indent: int = 2) -> str:
        import json

        return json.dumps(self.to_dict(), indent=indent, ensure_ascii=False)
