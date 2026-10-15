# RPG: Dungeon Survival

[中文](README.md) · [Example guide](../README.en.md)

A small playable demo built with `eve.RPG()`: survive waves of increasingly strong enemies. It shows attributes, effects, status, skills and settlement working together. The full [RPG system design](../../docs/dev/RPG系统设计.md) is currently in Chinese.

## Run

From the repository root, using a compatible runtime:

```sh
eve run examples/rpg
# Or build and run from source (choose your platform):
make run/win32-debug GAME=examples/rpg
```

## Play

- `1`: basic attack, with no resource cost and a short cooldown.
- `2`: fireball; costs 15 MP, casts for 0.6 seconds, and applies burning damage over time, up to three stacks.
- `3`: strength stance; costs 10 stamina and increases attack power.
- `4`: healing potion; restores health instantly.
- `R`: restart after death.

Enemies retaliate with claw attacks and a roar that lowers your defense. Later waves become stronger; surviving longer increases your score.

## Change the game data

`data/effects.json` defines healing, strength, weakness, burning and passive regeneration. `data/skills.json` defines costs, cooldowns, cast times and applied effects. Save changes to these JSON files to hot reload their values.

## What to learn

- **Attributes:** base/final attributes and modifiers; permanent `clampMin`/`clampMax` modifiers keep health within its allowed range.
- **Effects:** instant healing, duration/refresh buffs and debuffs, and periodic stacking burns.
- **Status:** read effect IDs, stacks and remaining time for the UI; `pollTicks` drives burning and regeneration.
- **Skills:** learning, cooldowns, costs and casting; `canCastSkillReason` explains a failed cast, and `getCastProgress` drives the cast bar.
- **Settlement:** this teaching demo keeps its damage formula in the script's `computeDamage`. Production Battle uses the general Settlement system; the old named pipeline is a compatibility path.

## Limits and extension points

`runSettlement` retains the old numeric `SettlementContext` interface for callbacks registered by native plugins. Its scheduling delegates to general Settlement. New projects should configure Battle's `settlement.rules` so damage, healing, status ticks, buffs and trigger chains use the formal path. Do not submit authoritative state through the compatibility numeric bag. Custom conditions registered with `SkillSystem::registerCastCondition` are also C++ only.

## A binding pitfall for engine contributors

The native attribute system calculates with `double`, but this build of Squirrel uses 32-bit `SQFloat` without `SQUSEDOUBLE`. Binding a `double` parameter or return value directly through `simplesquirrel` can fall through to the object template and fail at runtime:

```text
Type error bad cast expected: INSTANCE got: FLOAT
```

The RPG binding adapts script-facing floats explicitly:

```cpp
actor.addFunc("setBaseAttribute", [](RPGActor *a, const std::string &attribute, float value) {
    if (a) a->setBaseAttribute(attribute, double(value));
});
```

Use supported script-facing float types or conversion lambdas for new bindings, including return values. See the [binding guide](../../docs/en/dev/SCRIPT-BINDINGS.md).
