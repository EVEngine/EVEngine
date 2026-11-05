# AI asset kits

Engine-owned **pixel / format contracts** for agents that generate game art.

Agents must not invent Unity/Godot asset sizes. Before generating images, call MCP
`eve_asset_kit_list` / `eve_asset_kit_get`, or read this catalog from the SDK at
`share/eve/ai/asset-kits/catalog.json` (source tree: `cmake/sdk/asset-kits/`).

## Kits (v1)

| id | Genre | What to generate |
| --- | --- | --- |
| `card.playing` | Card | 52 playing-card faces at **262×372** (~2:3); optional back |
| `card.collectible` | Card | CCG faces at **512×724**; optional art window **512×384** |
| `visual-novel.dialogue` | Visual novel | Portraits **1024×1536** (display 256×384); backgrounds **1920×1080** |

Numbers come from shipping examples (`examples/cardgame`, `examples/dialogue`) and
`eve.card` `LayoutConfig` defaults (`cardW=110`, `cardH=150`).

## Agent workflow

1. Discover: `eve_asset_kit_list` (or open `catalog.json`).
2. Fetch one kit: `eve_asset_kit_get` with `{ "id": "visual-novel.dialogue" }`.
3. Generate each **required** slot at `pixelSize`, obeying `constraints` / `promptTemplate`.
4. Write files using `pathPattern`.
5. Bind with the kit's `consumer` hint (`setArt`, `Avatar.addLayer`, …).
6. Verify in-engine (`eve_screenshot` / play observe) — do not trust the image model alone.

Offline CLI (no running engine):

```bash
python3 scripts/asset_kits.py list
python3 scripts/asset_kits.py get visual-novel.dialogue
python3 scripts/asset_kits.py validate
```

## Schema

`schema.json` describes `eve.asset-kit.catalog` version 1. Unknown fields are rejected
by the validator so kits stay a single contract, not a second free-form prompt dump.
