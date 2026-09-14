# RTS Sandbox

This example is the end-to-end Squirrel composition profile for the existing
`eve.RTS` module. RTS owns the gameplay roots and orchestration only. Navigation,
local avoidance, sensing, damage, weapons, economy, definitions, and action
lifecycles are provided by their canonical engine modules.

The scenario loads `data/content.json`, creates two factions, buildings, mineral
fields, automatically assigned workers, and opposing marine groups. The central
wall and low-cost road exercise formation pathfinding. Units use the shared crowd
provider for group collision avoidance and the shared sensing/weapon/damage
pipeline for automatic firefights.

Run it with:

```sh
make rts
```

Controls:

- Left click selects one friendly unit.
- `1` selects all friendly marines.
- Right click issues a grid-formation move.
- `A` issues attack-move; hold Shift to append it.
- `D` focuses the enemy nearest the cursor.
- `B` orders a selected worker to pay for and construct a barracks.
- `F` queues a marine through the canonical economy/action/production transaction.
- `U` researches infantry weapons and `I` researches frontline shields.
- `Q`, `Y`, and `T` cast the data-defined grenade, suppressive barrage, and stim abilities.
- `H` holds position and `X` stops.
- `R` rebuilds the scenario.

All script calls use canonical UUID subjects, logical definition IDs, and checked
result objects. `inspectState()` is a read-only projection for rendering and UI;
it does not become a second gameplay state store.
