"""Story → stage brief: what the scene must contain and feel like.

The Creative Brain already parses style / terrain / lighting / vegetation from
free text. This module adds the *stage-directing* layer the story ask really
needs: focal elements (cast / centerpiece), camera intent, atmosphere keywords,
and a hint count of story beats — so the orchestrator can place a deliberate
centerpiece and frame it, instead of scattering props uniformly.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field, asdict
from typing import Any, Dict, List, Optional

# Thematic keyword -> procedural prop kind the kit can spawn.
THEME_KINDS: Dict[str, str] = {
    "castle": "skyscraper",
    "fortress": "skyscraper",
    "keep": "skyscraper",
    "tower": "skyscraper",
    "palace": "skyscraper",
    "城堡": "skyscraper",
    "王宫": "skyscraper",
    "要塞": "skyscraper",
    "高塔": "skyscraper",
    "塔": "skyscraper",
    "dragon": "rock",
    "monster": "rock",
    "beast": "rock",
    "巨龙": "rock",
    "龙": "rock",
    "怪物": "rock",
    "魔兽": "rock",
    "knight": "pillar",
    "hero": "pillar",
    "guard": "pillar",
    "statue": "pillar",
    "骑士": "pillar",
    "勇士": "pillar",
    "雕像": "pillar",
    "立柱": "pillar",
    "mountain": "rock",
    "hill": "rock",
    "boulder": "rock",
    "stone": "rock",
    "岩石": "rock",
    "巨石": "rock",
    "山石": "rock",
    "tree": "tree",
    "forest": "tree",
    "wood": "tree",
    "森林": "tree",
    "树": "tree",
    "bush": "bush",
    "灌木": "bush",
    "wall": "stonewall",
    "城墙": "stonewall",
    "石墙": "stonewall",
    "bridge": "bridge",
    "桥": "bridge",
    "fence": "fence",
    "篱笆": "fence",
    "栅栏": "fence",
    "hedge": "hedge",
    "树篱": "hedge",
    "chevaldefrise": "chevaldefrise",
    "treasure": "box",
    "chest": "box",
    "coin": "sphere",
    "jewel": "sphere",
    "crystal": "sphere",
    "temple": "box",
    "house": "box",
    "hut": "box",
    "宝箱": "box",
    "宝藏": "box",
    "宝石": "sphere",
    "水晶": "sphere",
    "神庙": "box",
    "殿堂": "box",
}

# Atmosphere / time-of-day keywords (checked before generic style).
TIME_OF_DAY: List[tuple] = [
    ("night", ["night", "moonlit", "moonlight", "midnight", "dark", "noir", "月光", "月下", "夜晚", "深夜", "夜色", "漆黑"]),
    ("dusk", ["dusk", "sunset", "twilight", "evening", "黄昏", "傍晚", "落日", "暮色"]),
    ("dawn", ["dawn", "sunrise", "morning", "拂晓", "黎明", "清晨"]),
    ("day", ["day", "noon", "midday", "sunny", "bright", "afternoon", "白天", "正午", "晴朗"]),
]

# Generic mood keywords -> atmosphere label (passed through to the plan).
MOOD_LABELS: List[tuple] = [
    ("moonlit", ["moonlit", "moonlight", "月光", "月下"]),
    ("dusk", ["dusk", "黄昏", "傍晚"]),
    ("rainy", ["rain", "rainy", "storm", "雨"]),
    ("foggy", ["fog", "mist", "foggy", "雾"]),
    ("sunny", ["sunny", "clear", "晴"]),
    ("eerie", ["eerie", "haunted", "ghost", "幽", "诡异"]),
    ("epic", ["epic", "battle", "war", "conflict", "对决", "战场"]),
    ("peaceful", ["peaceful", "quiet", "calm", "宁静"]),
    ("cyberpunk", ["cyberpunk", "neon", "synth", "赛博"]),
    ("fantasy", ["fantasy", "magic", "enchant", "魔法", "奇幻"]),
]


def _contains(text: str, keywords: List[str]) -> bool:
    low = text.lower()
    return any(k in low for k in keywords)


def _extract_kinds(text: str) -> List[str]:
    """Collect prop kinds mentioned in the story, most specific first."""
    seen: List[str] = []
    low = text.lower()
    for keyword, kind in THEME_KINDS.items():
        if keyword in low and kind not in seen:
            seen.append(kind)
    return seen


def _time_of_day(text: str) -> str:
    for tod, keys in TIME_OF_DAY:
        if _contains(text, keys):
            return tod
    return "day"


def _atmosphere(text: str) -> str:
    for label, keys in MOOD_LABELS:
        if _contains(text, keys):
            return label
    return "generic"


def _sentences(text: str) -> List[str]:
    parts = re.split(r"[。！？!?；;.\n]+", text)
    return [p.strip() for p in parts if p.strip()]


@dataclass
class StageBrief:
    """Stage-directing summary extracted from a story request."""

    name: str = "story-scene"
    atmosphere: str = "generic"
    timeOfDay: str = "day"
    beats: int = 1
    focal: List[Dict[str, Any]] = field(default_factory=list)  # [{kind, label}]
    cast: List[str] = field(default_factory=list)              # focal entity labels
    centerpiece: Optional[str] = None                          # main prop kind
    notes: List[str] = field(default_factory=list)

    def to_dict(self) -> dict:
        return asdict(self)


def parse_stage(story: str, name: str = "story-scene", llm: Any = None) -> StageBrief:
    """Parse a story request into a StageBrief (heuristic; LLM optional)."""
    story = (story or "").strip()
    brief = StageBrief(name=name)

    if not story:
        return brief

    brief.atmosphere = _atmosphere(story)
    brief.timeOfDay = _time_of_day(story)
    brief.beats = max(1, len(_sentences(story)))

    kinds = _extract_kinds(story)
    focal: List[Dict[str, Any]] = []
    for kind in kinds:
        focal.append({"kind": kind, "label": f"{kind}-focal"})
    if focal:
        brief.focal = focal
        brief.centerpiece = focal[0]["kind"]
        brief.cast = [f["label"] for f in focal]

    # Keep a lightweight narrative note for the build phase.
    brief.notes.append(story[:200])

    # Optional LLM refinement (OpenAI-compatible) — best effort, never fatal.
    if llm is not None:
        try:
            raw = llm.complete(
                [{
                    "role": "user",
                    "content": (
                        "你是舞台导演。把这段剧情抽成舞台要点："
                        "atmosphere(英文), timeOfDay(dawn|day|dusk|night), "
                        "centerpiece(道具种类：skyscraper|rock|pillar|tree|bush|stonewall|bridge"
                        "|box|sphere|cylinder), cast(角色/焦点实体名数组)。"
                        f"只输出JSON对象。剧情：{story}"
                    ),
                }]
            )
            parsed = _safe_json(raw)
            if isinstance(parsed, dict):
                if isinstance(parsed.get("atmosphere"), str):
                    brief.atmosphere = parsed["atmosphere"]
                if parsed.get("timeOfDay") in ("dawn", "day", "dusk", "night"):
                    brief.timeOfDay = parsed["timeOfDay"]
                if isinstance(parsed.get("centerpiece"), str):
                    brief.centerpiece = parsed["centerpiece"]
                if isinstance(parsed.get("cast"), list):
                    brief.cast = [str(c) for c in parsed["cast"] if str(c)]
        except Exception:
            pass
    return brief


def _safe_json(text: str) -> Optional[Any]:
    import json

    if not text:
        return None
    t = text.strip()
    if t.startswith("```"):
        t = t.split("```", 2)[1] if "```" in t[3:] else t
        t = t.strip().lstrip("json").strip()
    try:
        return json.loads(t)
    except json.JSONDecodeError:
        import re

        m = re.search(r"\{.*\}", t, re.DOTALL)
        if m:
            try:
                return json.loads(m.group(0))
            except json.JSONDecodeError:
                return None
    return None