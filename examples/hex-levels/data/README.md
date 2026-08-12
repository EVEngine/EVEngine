# Hex level test fixtures

Canonical data for `examples/hex-levels` and `test/hex_level_data.cpp`.

| File | Purpose |
|------|---------|
| `catalog.json` | Level definitions with per-level boot configs (seed/size/algo/fov/light/cellCost/enable) |
| `items.json` | Inventory item definitions (14+ hex.* items) |
| `loot_tables.json` | Per-table loot offsets + perception gates |
| `seeds_matrix.json` | Seed × algorithm smoke matrix |
| `perception_cases.json` | FOV perception / stealth numeric cases |
| `fixtures.nut` | Squirrel mirrors (`LEVEL_CATALOG`, `LOOT_TABLES`) for the playable demo |
| `maps/tiny_hex.json` | Handcrafted hexagonal map with spawn/exit/loot |
| `maps/ring_hex.json` | Outer-ring corridor + center door path case |
| `particles/*.json` | Torch / pickup burst / ember / mist emitter configs |
