"""OpenAI-backed LLM helper for the Creative Brain.

The intent parser and the layout generator call an LLM to turn natural language
into structured output. To keep the tool testable and runnable without a key,
every LLM entry point has a deterministic fallback that emits a valid (if
generic) result. Set ``OPENAI_API_KEY`` (and optionally ``OPENAI_BASE_URL`` /
``OPENAI_MODEL``) to enable real inference.
"""

from __future__ import annotations

import json
import os
import random
from typing import Any, Callable, Dict

MODEL = os.environ.get("OPENAI_MODEL", "gpt-4o-mini")

# A deterministic RNG shared by fallbacks so a given seed reproduces a plan.
_fallback_rng = random.Random(1)


def llm_enabled() -> bool:
    return bool(os.environ.get("OPENAI_API_KEY"))


def _client():
    """Lazy OpenAI client. Import inside the function so `openai` is optional."""
    try:
        import openai  # type: ignore

        kwargs: Dict[str, Any] = {}
        if os.environ.get("OPENAI_BASE_URL"):
            kwargs["base_url"] = os.environ["OPENAI_BASE_URL"]
        if os.environ.get("OPENAI_API_KEY"):
            kwargs["api_key"] = os.environ["OPENAI_API_KEY"]
        return openai.OpenAI(**kwargs)
    except Exception as e:  # pragma: no cover - environment dependent
        raise RuntimeError(f"OpenAI client unavailable: {e}") from e


def call_structured(
    system: str,
    user: str,
    schema: Dict[str, Any],
    fallback: Callable[[], Any],
    temperature: float = 0.2,
) -> Any:
    """Call the LLM requesting JSON matching ``schema``; fall back if disabled/fail.

    Returns a Python object (dict/list) or the fallback value.
    """
    if not llm_enabled():
        return fallback()
    try:
        client = _client()
        resp = client.chat.completions.create(
            model=MODEL,
            temperature=temperature,
            response_format={"type": "json_schema", "json_schema": schema},
            messages=[
                {"role": "system", "content": system},
                {"role": "user", "content": user},
            ],
        )
        text = resp.choices[0].message.content or ""
        return json.loads(text)
    except Exception as e:  # pragma: no cover - network/API dependent
        print(f"[llm] warning: OpenAI call failed ({e}); using deterministic fallback.")
        return fallback()


def parse_intent_with_llm(user_prompt: str) -> Dict[str, Any]:
    """Turn a natural-language request into a structured SceneConfig dict.

    Uses the JSON schema from :mod:`brain.schema` so output always validates.
    Falls back to :func:`brain.intent.parse_intent_deterministic`.
    """
    from brain import intent

    system = (
        "You are the Creative Brain of a procedural game engine. Parse the user's "
        "scene request into a structured JSON scene configuration matching the given "
        "schema. Map vague words to concrete choices: style from genre, terrain from "
        "landscape, lighting from mood/time, road network from movement style, and "
        "derive exploration prop rules. Use engine algorithm ids for terrain "
        "(dungeon.bsp, cave.cellular, cave.drunkard, maze.backtrack, noise.terrain, "
        "terrain.heightmap, wfc.simple, level.roguelike). Keep every field present."
    )

    def fallback() -> Dict[str, Any]:
        return intent.parse_intent_deterministic(user_prompt)

    return call_structured(system, user_prompt, SCENE_CONFIG_SCHEMA_WRAPPER(), fallback)


def SCENE_CONFIG_SCHEMA_WRAPPER() -> Dict[str, Any]:
    """Wrap the scene schema as an OpenAI json_schema response_format object."""
    from brain.schema import SCENE_CONFIG_SCHEMA

    return {"name": "scene_config", "strict": True, "schema": SCENE_CONFIG_SCHEMA}


def pick_with_llm(
    prompt: str, choices: list, describe: str, fallback_index: int = 0
) -> str:
    """Ask the LLM to pick the best option from ``choices`` (returns one choice)."""
    schema = {
        "name": "pick_option",
        "strict": True,
        "schema": {
            "type": "object",
            "additionalProperties": False,
            "required": ["choice"],
            "properties": {
                "choice": {"type": "string", "description": "One of the available choices."},
            },
        },
    }
    system = f"You pick the best match. {describe}. Reply only with one of: {choices}."

    def fallback() -> str:
        return choices[fallback_index if fallback_index < len(choices) else 0]

    out = call_structured(system, prompt, schema, fallback)
    choice = out.get("choice") if isinstance(out, dict) else None
    if choice not in choices:
        return fallback()
    return choice
