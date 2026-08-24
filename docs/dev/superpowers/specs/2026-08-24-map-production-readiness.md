# Map production-readiness design

## Goal

Move `map` from a capable small-map runtime to a production tilemap stack without
breaking existing Squirrel games. The target is cost proportional to visible or
dirty chunks, a versioned TileSet data model, automatic derived-data invalidation,
and a level-authoring loop that does not require hand-editing JSON.

## Compatibility boundary

- `TileLayer` remains the script facade and fixed maps keep their current coordinates.
- GID 0 remains empty and Tiled transform bits remain preserved in storage.
- Rendering, physics and editor integrations cross module boundaries through
  capabilities; `map` must not add upward includes.
- Existing `eve.tileset/1` manifests remain loadable. Version 2 fields are optional.

## Stage 0: measurable baseline

The regression fixture uses 1024 x 1024 maps in sparse and dense forms. Required
counters are total chunks, non-empty chunks, visited chunks, visited cells and
emitted tiles. CPU timing is informative locally; CI gates deterministic visit
counts so machine variance cannot create flakes.

Acceptance criteria:

- an empty 1024 x 1024 layer scans zero cells;
- a sparse layer scans only its occupied 32 x 32 chunks;
- a camera-bound layer rejects chunks outside the viewport;
- bulk edits publish one monotonic tile revision.

## Stage 1: chunked runtime

`TileLayer::Tiles` owns a 32 x 32 spatial index beside the compatibility GID array.
All mutations update chunk occupancy and a monotonic revision. Rendering iterates
non-empty chunks and performs conservative chunk/AABB viewport rejection before
cell collection. A later sparse/infinite backend can replace the dense array behind
the same facade.

## Stage 2: TileSet v2

Tile definitions gain typed custom data, animation alternatives, terrain/Wang
connectivity and optional collision/navigation/occlusion shapes. Runtime terrain
painting resolves a region plus its one-cell neighborhood deterministically.

## Stage 3: derived systems

Pathfinder and FOV remember the bound layer revision and resync automatically before
queries or compute. Physics integration is supplied by a capability implemented by
the physics module; map emits dirty tile regions and never includes physics headers.

## Stage 4: authoring

The editor supplies palette, brush, rectangle, fill, erase, eyedropper and undo/redo
commands over a shared `TileEditCommand`. Tiled import expands to infinite chunks,
external tilesets, groups, typed properties, Wang sets and animation with actionable
diagnostics and schema migration.

## Delivery and gates

Each stage is independently reviewable but the public interface, backends, consumers,
documentation and tests ship together. Required gates are the focused map tests,
`check_bindings.py --strict`, module layering, changed-line formatting and an actual
`iso-grid-walk` runtime smoke with a captured frame.
