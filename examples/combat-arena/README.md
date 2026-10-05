# Combat Arena

Deterministic vertical-slice that composes the action-combat foundation:

- 3D character controller (run / jump / dodge i-frames)
- Melee hitbox/hurtbox sweep + damage
- Cancel buffer + combo graph
- Hitstop/hitstun feel
- Guard/parry windows
- Soft/hard lock-on
- Simple near-band enemy AI

This example is a headless-friendly simulation loop driven from Squirrel. It
proves the runtimes compose without copying HP/cooldown state into script.

## Run

```bash
make run/linux-debug GAME=examples/combat-arena
# or
make run/win32-debug GAME=examples/combat-arena
```

Headless smoke:

```bash
MIN_RUN_SECONDS=2 RUN_SECONDS=4 bash scripts/smoke_examples.sh combat-arena
```

## Controls (when a window is present)

- `WASD` move
- `Space` jump
- `Shift` dodge
- `J` light attack (scripted pulse)
- `K` cycle lock-on

The first few seconds auto-run a scripted duel so CI smoke does not require input.
