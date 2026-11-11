---
name: eve-rpg
description: EVEngine RPG domain. Use when implementing attributes, buffs/status, skills/casting, vitals, XP/level, quests/trackers, equipment modifiers, loot, traits, or turn-based RPG battle.
---

# RPG domain (EVEngine)

Module guide: `docs/usr/modules/rpg.md`. Script entry: `eve.RPG()` (slot `rpg`).

RPG **composes** attributes, effects/status, skills, vitals, progression, quests,
equipment modifiers, loot, traits, turn battle, and game-state flags. It does not
own hitboxes, scene transforms, or bag item counts (`inventory` owns bags).

## Definition vs instance

| Layer | Owner |
| --- | --- |
| Effect / skill / loot **definitions** (JSON) | `RPG` registries |
| Actor attributes, buffs, learned skills, cooldowns, cast, XP, vitals | `RPGActor` |
| Cross-frame events (tick, status, cast, level-up, vitals) | poll after `rpg.update(dt)` |
| Equipment **items** | `inventory`; RPG applies modifiers via equipment system |

```squirrel
local rpg = eve.RPG()
local actor = rpg.newActor()
actor.setBaseAttribute("hp", 100)
actor.setBaseAttribute("attack", 12)
rpg.registerEffectsFromJson(/* definitions */)
local buffId = actor.applyBuff("buff.power", "potion")
```

`applyBuff` / `hasBuff` alias `applyEffect` / `hasEffect` — one status container.

## Combat composition

- Realtime ARPG damage into vitals: use the settlement / `RPGActionDamageSink`
  path from `.cursor/skills/eve-arpg/SKILL.md`. Do not parallel-deduct the same HP
  from a homemade CombatState and RPG vitals.
- Turn-based battle stays on the RPG battle surface; do not run both realtime and
  turn pipelines against one vitals owner in the same fight.

## Quests & progression

- Trackers observe **committed facts** (kills, pickups), not raw hit callbacks.
- Level/XP APIs own progression; script may present level-up events but must not
  invent a second XP counter.
- Loot rolls need an injected RNG stream; seeded ≠ cross-platform replay claim.

## Save

Use RPG save session / participants where available. Restore rebuilds mappings;
do not persist raw runtime pointers across load.

## Do not

- Create a universal GameplayActor that owns RPG + combat + inventory + animation.
- Discard Result tables from register/cast/settlement APIs.
- Drive cooldowns from wall clock.

## Gold

- `examples/rpg/` — dungeon survival slice
- `examples/arpg-loop/` — vitals + quest + equip in an action loop
- Related: `docs/usr/modules/attributes.md`, `inventory.md`, `settlement.md`
