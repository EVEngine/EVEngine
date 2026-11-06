# AI asset kits

Engine-owned **pixel / format contracts** for agents and internal systems that
generate game art.

Do not invent Unity/Godot asset sizes. Discover kits first, then generate.

| Transport | Entry |
| --- | --- |
| MCP | `eve_asset_kit_list` / `eve_asset_kit_get` / `eve_asset_kit_resolve` |
| CLI / library | `python3 scripts/asset_kits.py` · `import asset_kits` |
| File | SDK `share/eve/ai/asset-kits/catalog.json` (this directory in-tree) |

**Integrators:** read [`INTEGRATION.md`](INTEGRATION.md) and
[`protocol.json`](protocol.json) — one `eve.asset-kit.api` surface for pipelines,
creative-brain, CI, and MCP clients. Overlays via `EVE_ASSET_KITS_EXTRA` /
`--extra` let internal catalogs extend the engine without forking it.

## Kits (v1)

| id | Genre | What to generate |
| --- | --- | --- |
| `card.playing` | Card | 52 playing-card faces at **262×372** (~2:3); optional back |
| `card.collectible` | Card | CCG faces at **512×724**; optional art window **512×384** |
| `visual-novel.dialogue` | Visual novel | Portraits **1024×1536** (display 256×384); backgrounds **1920×1080** |

Numbers come from shipping examples (`examples/cardgame`, `examples/dialogue`) and
`eve.card` `LayoutConfig` defaults (`cardW=110`, `cardH=150`).

## Agent / pipeline workflow

1. Discover: `list` (optional `genre` / `module` / `tag` / `q` filters).
2. Inspect: `get` with a kit id.
3. Hand-off: `resolve` kit + slot + vars → concrete `job` (`pixelSize`, `path`, `prompt`, `bindings`).
4. Generate required slots; write `job.path`.
5. Bind via `consumer` (EveScript) or `bindings[]` for your system id.
6. Verify in-engine (`eve_screenshot` / play observe) — do not trust the image model alone.

```bash
python3 scripts/asset_kits.py --json list --genre card
python3 scripts/asset_kits.py --json get visual-novel.dialogue
python3 scripts/asset_kits.py --json resolve card.playing face \
  --vars '{"suit":"spades","rank":"Q","style":"ink"}'
python3 scripts/asset_kits.py validate
python3 scripts/asset_kits.py --json sources
```

## Schema

`schema.json` describes `eve.asset-kit.catalog` version 1. Unknown fields are rejected
by the validator so kits stay a single contract, not a second free-form prompt dump.
Optional `tags` / `bindings` support multi-system discovery and hand-off.
