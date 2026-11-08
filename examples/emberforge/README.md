# Emberforge — craft, enhance, equip, strike

A forge vertical slice for the RPG production adapters:

- `rpg::Crafting` pays bag materials and grants a new Ember Blade through `production`
- `rpg::EquipmentUpgrade` mutates the same item instance's `enhance` prop (no new item id)
- `EquipmentSystem` scales additive equip stats from that enhance level
- Script entry: `eve.RPGForge()` (`ForgeBench`) — one bag, one queue, craft + upgrade

The scene is a blacksmith loft: anvil progress, hammer swings, a glowing blade that
lengthens with enhance, and a training post so equip attack is visible as strike damage.

## Run

```bash
make run/<platform>-debug GAME=examples/emberforge
# Cloud / headless smoke:
# MIN_RUN_SECONDS=2 bash scripts/smoke_examples.sh emberforge
```

The bundled DejaVu font's license is under `assets/fonts/NOTICE.txt`.

## Controls

| Key | Action |
| --- | --- |
| `C` | Craft an Ember Blade (3 ore + 1 coal) |
| `U` | Enhance the first blade in the bag (+1, costs emberstones) |
| `E` | Equip / unequip the blade |
| `F` | Strike the training post (requires equipped blade; damage = final attack) |
| `R` | Restock materials |
| `Space` | Pause / resume the forge clock |

Suggested loop: `C` → wait for the anvil → `U` a few times → `E` → `F` and watch
attack climb. Unequip before the next enhance — upgrade pins a bag instance.
