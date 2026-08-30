# Autotile production roadmap

Date: 2026-08-30
Status: implemented; automated visual capture pending a Vulkan-capable SDL display

## Product outcome

EVEngine shall provide a creator-facing tilemap workflow comparable to RPG Maker
for common 2D RPG maps while retaining Tiled as the open canonical interchange.
Authors paint logical terrain; one transaction resolves visuals, navigation,
collision and occlusion for the dirty region. RPG Maker MV/MZ assets are admitted
through a versioned adapter and do not become the map module's canonical model.

## Acceptance matrix

### P1: standard interchange

- Tiled JSON/TMJ supports embedded and external TSJ/TSX tilesets, multiple tilesets,
  finite and infinite layers, groups, typed properties, animations, transforms,
  Wang sets and tile collision objects.
- Import is prepare-then-commit. Failure returns stable diagnostics and leaves the
  currently observable map unchanged.
- Unknown fields are preserved when round-tripping editor documents. Unknown newer
  schema versions are rejected; the current and previous schema versions migrate.

### P2: canonical autotile and navigation model

- `eve.autotile/1` owns terrain families, exact eight-position connectivity,
  deterministic weighted variants, animation groups, layer policy and a navigation
  profile. Tile visuals are a derived projection, never a second terrain authority.
- Navigation profiles express enter/exit direction masks, movement cost, collision,
  occlusion and extensible semantic flags. Pathfinder, flow field, FOV and collision
  consume this single profile.
- Painting resolves the dirty region plus declared neighborhood in one monotonic
  revision and produces a receipt suitable for editor undo/redo.

### P3: production resolvers

- Mixed edge/corner water-land transitions support still and animated water.
- Walls resolve top, face, inner/outer corners, height continuity and occlusion.
- Waterfalls resolve lip, repeating body and foot/splash with synchronized animation.
- Palette, brush, rectangle, fill, erase and eyedropper use the same domain operation
  path from UI, script, tests and Agents. A runnable example demonstrates all three.

### P4: RPG Maker MV/MZ adapter

- Import `MapXXX.json`, `Tilesets.json` and A1-A5/B-E sheets without launching RPG
  Maker. Decode quarter-tile autotiles, A1 water/waterfall animation, A3 buildings,
  A4 walls and layer/shadow rules into the canonical IR.
- Map passage, four-direction passage, star overlay, ladder, bush, counter, damage
  floor and terrain tags into navigation/semantic profiles with explicit diagnostics
  for plugin-defined behavior that cannot be translated.
- The adapter records source engine/version and never mutates source project files.

## Authority and lifecycle

The logical terrain grid and versioned tileset definition are authoritative. Render
GIDs, navigation cells, collision rectangles and FOV opacity are derived caches keyed
by the tile revision and definition generation. The map layer owns the grids and
definitions; Pathfinder/FOV observe them synchronously and retain no temporary tile
pointers. Hot reload prepares a complete candidate and swaps it only after validation.

All authoring and import calls are main-thread affine and non-reentrant. Resolution is
bit-exact for equal schema, seed and logical grid. Weighted variants use an injected,
named seed derived from map id, terrain family and cell coordinate rather than wall
clock or container iteration order.

## Failure and compatibility policy

New canonical operations return `Result` with stable `map.import.*`,
`map.autotile.*` or `map.navigation.*` diagnostics. Existing bool/script facades are
compatibility-only projections until their documented migration window ends. Missing
optional physics/editor providers is observable and tested; it never changes logical
terrain or navigation results.

## Review iteration 1

The initial proposal treated RPG Maker tile IDs as a second runtime representation.
Review rejected that because it would duplicate terrain authority and leak A1-A5
layout into rendering and pathfinding. The revised boundary decodes RPG Maker content
once into the same versioned IR used by Tiled and native assets.

## Review iteration 2

The revised proposal initially stored only `walkable` per GID. Review rejected it
because cliffs, wall lips and one-way stairs require edge semantics. The final model
uses enter/exit masks plus cost and semantic flags, with collision and FOV derived from
the same profile and covered by composition tests.

## Required evidence

- Parser fixtures for every admitted format and rollback/failure injection.
- Shared resolver contract tests, deterministic golden grids and navigation parity.
- Provider-present/provider-absent collision composition tests.
- Runnable headless smoke plus engine-owned screenshots for water, wall and waterfall.
- Strict bindings, manifest, dependency graph, architecture contracts, formatting and
  diff checks before handoff.

## Implementation evidence

- 96 non-interactive `map.*` CTests pass, including transactional rollback, external
  TSJ/TSX, nested groups, collision objects, transform flags, exact resolver grids,
  unified navigation/collision/FOV and MV/MZ quarter-tile tables.
- `examples/autotile-production` passes the repository smoke scanner and exercises
  shore, wall and waterfall operations through the public script bindings.
- Strict bindings, module manifest, dependency graph, quality metadata, architecture
  contracts and clang-format 18 changed-line checks pass.
- Engine-owned screenshot capture was attempted through `--mcp-port`; this checkout's
  SDL build reports `No available video device` under Xvfb, so no screenshot is claimed
  as visual-correctness evidence from this machine.
