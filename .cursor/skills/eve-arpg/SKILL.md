---
name: eve-arpg
description: EVEngine action-RPG closed loop. Use when building ARPG combat, ability timelines, hit windows, damage settlement into RPG vitals, loot/equip progression, or examples like arpg-loop / combat-arena.
---

# ARPG closed loop (EVEngine)

Design baseline: `docs/dev/ARPG框架设计.md`. Gold slice: `examples/arpg-loop/`.

Product loop: **fight → committed facts → rewards → equip → harder fight**.
Do not invent a universal GameObject / CombatActor root.

## Ownership map

| Domain | Owns | Does not own |
| --- | --- | --- |
| `action` | Ability grant/cooldown, Action phase, timelines | HP, board, character entity |
| `combat` | locomotion, melee hit/hurt, lock-on, cancel/combo, feel | long-term progression |
| `settlement` | damage/reward pipeline commit | character presentation |
| `rpg` | vitals, XP/level, quests, equip modifiers, RPG save | realtime hitbox geometry |
| `inventory` | bag / equipment slot ownership | attribute meaning of items |
| script host | input mapping, encounter flow, telegraph timing | second HP copy |

Pipeline: Input/Intent → **action** timeline → **combat** hit windows →
**settlement** commit → RPG/inventory facts → quests/loot/equip → attributes
feed later settlement.

## Script rules

1. Create runtimes via Result APIs (`eve.Action()`, `eve.Combat()`, `eve.RPG()`, …).
   Check `result.ok` before using `result.value`.
2. Inject tick/`dt` into action/combat advance — never wall clock for cooldown/phase.
3. One damage sink path into vitals (e.g. `RPGActionDamageSink` / settlement adapter).
   Do not write base HP from hit detection and again from RPG.
4. Kill rewards and pickups are separate transactions; retain failed deliveries and
   validate actor generations (see arpg-loop `ActionEncounter`).
5. Saves are checkpoints of progression identities — not mid-combo combat rewind
   unless explicitly designed. Loading clears transient attack/dodge/cooldown queues.

## Targeting & sensing

Prefer `sensing` query / presets for in-range candidates. Do not scan every enemy
array as the main path (`docs/usr/modules/sensing.md`).

## Verify

- Ship `game.agent.json` with the same commands as keyboard/HUD.
- Acceptance drivers (e.g. `examples/arpg-loop/agent_check.py`) use `eve_play` +
  engine readback — not `eve_eval`.
- Follow `.cursor/skills/eve-agent-loop/SKILL.md` for evidence sessions.

## Do not

- Maintain a second health field in script when RPG vitals are authoritative.
- Activate abilities by inventing Unity AnimationEvent / Godot signal APIs.
- Mix CombatState and RPG vitals as dual writers for the same resource.

## Gold

- `examples/arpg-loop/` — move/attack/dodge, telegraph, loot, equip, checkpoint
- `examples/combat-arena/` — combat feel slice
- Modules: `docs/usr/modules/action.md`, `combat.md`, `rpg.md`, `inventory.md`
