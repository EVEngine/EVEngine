# Quality notes — Mini RPG Workshop (EVEngine)

## What works
- **Character**: `eve.RPG()` actor with HP/MP/ATK/DEF; **C** prints vitals.
- **Combat**: F2 / encounter tile starts a goblin fight; Strike, Arc Bolt, Mend; flee with Esc.
- **Inventory**: Herb Potion + Iron Blade registered; **Q** consumes a potion and heals.
- **Dialogue**: keeper.offer dnut with accept/later choice; goblin.hint before fight; journal + optional `dialogue.say`.
- **Map edit**: F3 paint floor(1)/wall(2)/erase; pathfinder blocks gid=2 so edits affect explore movement.
- **Scene edit**: F4 arrow keys move a 2D crate marker that blocks walk in explore; optional `scene_director` 3D box/pillar/camera.
- **Runs**: smoke PASS (≥2s alive); `RPG_BENCH_COMPARE_READY` printed from `eve_init`.

## Missing / polish gaps
- Dialogue is journal-driven rather than a full VN box UI (no typewriter / choice buttons).
- Iron Blade is in the bag but there is no equip hotkey in this slice.
- Map uses solid tile GIDs without a custom tileset texture (functional, not pretty).
- No automated input script covering combat/dialogue/edit paths — only process-alive smoke.

## Known bugs / risks
- Re-spawning `marker_crate` on every prop move via `sd.spawn` may warn or no-op depending on scene_director overwrite semantics; 2D marker remains authoritative for play blocking.
- Headless/Xvfb smoke does not exercise mouse paint or keyboard combat; those paths are code-complete but not CI-driven here.
- Soft-reload (`persist`) keeps actors/bags; intentional, but a bad mid-edit hot reload can leave stale journal lines.
