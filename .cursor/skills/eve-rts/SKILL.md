---
name: eve-rts
description: EVEngine RTS pattern. Use when building fixed-step simulation, factions, fog of war, unit commands, production queues, or composing card/economy payments with RTS.
---

# RTS pattern (EVEngine)

Module guide: `docs/usr/modules/rts.md`. Script entry: `eve.RTS()` (slot `rts`).
Gold: `examples/rts-sandbox/`, `examples/commandery-rts/`.

## Ownership

| Owner | Fact |
| --- | --- |
| RTS facade / ECS roots | world cells, fog mirrors, command admission |
| economy / adapters | balances & reservations |
| production / action / combat / crowd | their own queues and motion |
| HUD / render | `inspectState` projections only |

Do not let UI write a second position or resource balance that bypasses commands.

## Fixed step

Accumulate `dt`, step at a fixed interval (e.g. 1/30), with a per-frame catch-up
cap. Keep leftover time; do not infinite-step one frame; do not claim variable-dt
parity across machines.

```squirrel
accumulated += dt
local steps = 0
while (accumulated >= 1.0 / 30.0 && steps < 8) {
    rts_value(simulation.stepScript(1.0 / 30.0))
    accumulated -= 1.0 / 30.0
    ++steps
}
```

Persistent subject ids are UUIDs — unique per entity; demo UUIDs are not a schema.

## Commands & production

1. Configure world → create faction → explore/visibility as needed.
2. Spawn / produce through RTS APIs; move/attack via admitted commands.
3. Sensing for target candidates: `eve.Sensing()` query — not full unit scans.

## Payments (card + economy)

Compose with the shared transaction batch — stage then commit once:

```squirrel
local batch = transaction.newAtomicBatch()
cards.stagePlay(batch, card)
sim.stageAbility(batch, casterId, "grenade", "", x, y)
local result = batch.commit()
```

Staging must not pay, consume the card, start cooldown, or enqueue production.
Failed staging poisons the batch. Details:
`docs/usr/GAMEPLAY_PAYMENT_COMPOSITION.md`.

Keep **one** persistent ledger adapter per ledger while reservations are in flight.

## Do not

- Drive fog or resources from render-side caches as authority.
- Invent Unity NavMesh / Godot NavigationAgent APIs — use documented RTS /
  `combat_navigation` / map Pathfinder providers.
- Skip Result checks on configure/step/command.

## Gold

- `examples/rts-sandbox/` — produce + move
- `examples/commandery-rts/` — commandery-scale slice
- Targeting helpers: `docs/usr/modules/sensing.md`
