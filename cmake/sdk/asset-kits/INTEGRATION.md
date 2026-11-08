# Integrating asset kits into internal systems

Asset kits are a **language-neutral contract**, not a Cursor-only feature. Any
internal system that prepares, generates, validates, or ships game art should
speak `eve.asset-kit.api` against `eve.asset-kit.catalog`.

Machine-readable protocol: [`protocol.json`](protocol.json).

## Why one interface

| Without kits | With kits |
| --- | --- |
| Each pipeline invents 512² / 1024² / “portrait-ish” sizes | One `pixelSize` / `aspectRatio` / `alpha` per slot |
| EveScript consumers diverge from CI / creative-brain / CDN tools | Same `resolve` job feeds every system |
| Project forks copy engine constants | Overlays replace kit ids without editing the engine catalog |

## Pick a transport

All transports return the same JSON envelope (`api`, `apiVersion`, `ok`, …):

1. **File (no process)** — read `share/eve/ai/asset-kits/catalog.json` from an
   installed SDK (or `cmake/sdk/asset-kits/` in-tree). Validate with
   `schema.json`.
2. **CLI / library** — `python3 scripts/asset_kits.py --json …` or
   `import asset_kits; CatalogStore.open()`.
3. **MCP** — `eve_asset_kit_list` / `eve_asset_kit_get` /
   `eve_asset_kit_resolve` on a running `eve` (any task profile).

Prefer **resolve** when a system is about to generate or fetch one asset:
it returns a concrete `job` (`path`, `prompt`, `pixelSize`, `bindings`).

## Minimal Python plug-in

```python
import asset_kits  # scripts/ on PYTHONPATH, or run from repo root

store = asset_kits.CatalogStore.open()              # engine + EVE_ASSET_KITS_EXTRA
listed = store.list(genre="card", tag="ccg")
job = store.resolve(
    "card.collectible",
    "face",
    {"cardId": "flame", "name": "烈焰", "cost": 2, "kind": "creature", "style": "ink"},
)["job"]

assert job["pixelSize"] == {"width": 512, "height": 724}
# hand job["path"] / job["prompt"] to your generator or resource agent
for binding in job["bindings"]:
    if binding["system"] == "pipeline":
        ...  # drop file into binding["ref"]
```

CLI equivalent:

```bash
python3 scripts/asset_kits.py --json list --genre card --tag ccg
python3 scripts/asset_kits.py --json resolve card.playing face \
  --vars '{"suit":"hearts","rank":"A","style":"flat"}'
```

## Overlay catalogs (company / project kits)

Keep the engine catalog immutable. Ship internal kits as overlay JSON with the
**same schema**, then merge:

```bash
export EVE_ASSET_KITS_EXTRA=/opt/acme/asset-kits:/path/to/project/asset-kits
python3 scripts/asset_kits.py --json sources
python3 scripts/asset_kits.py --json list --tag acme
```

Rules:

- Overlay document = `eve.asset-kit.catalog` (may contain a subset of kits).
- Later overlays **replace** a kit with the same `id`.
- `sources` / list `source` fields show which file won.
- MCP currently serves the **engine-embedded** catalog; overlays are for
  offline / pipeline / CLI consumers until a project host loads them.

## Binding internal systems

Each kit (and optionally each slot) may declare `bindings[]`:

```json
{ "system": "creative-brain", "kind": "queue-request", "ref": "resource_request.asset_id", "notes": "…" }
```

Suggested `system` ids: `evescript`, `pipeline`, `creative-brain`, `http`,
`ci`, plus your own (`acme.cdn`, …). `consumer` remains the EveScript-oriented
string for agents; `bindings` is the multi-system index.

## Handshake checklist for a new internal system

1. Depend on `apiVersion` / `schemaVersion` from responses (currently both `1`).
2. Discover with `list` filters (`genre` / `module` / `tag` / `q`) — do not hardcode kit ids unless pinned.
3. Call `resolve` before generation; refuse to invent sizes when `ok` is false.
4. Honor `alpha` / `format` / `constraints`; write exactly `job.path`.
5. Route completion through `bindings` for your `system` id.
6. If you need project-specific sizes, ship an overlay kit — do not patch the engine catalog in place.
