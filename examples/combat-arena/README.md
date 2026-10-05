# Combat Arena

Deterministic vertical-slice that composes the action-combat foundation:

- 3D character controller (run / jump / dodge i-frames / lock-relative dodge)
- Knockback / stun reaction while stunned (slide + gravity)
- Melee hitbox/hurtbox sweep + damage
- Cancel buffer + combo graph via `CombatCancelResolver`
- Hitstop/hitstun feel
- Guard/parry windows
- Perfect-dodge audit when i-frames negate a melee hit
- Soft/hard lock-on
- Lock-on camera framing (`CombatCameraFraming` + `CameraController` `lockon`)
- Motion warp toward the locked target during attacks
- Enemy Mid/Far approach steering + near-band attack with optional telegraph / recover
- `CombatLoopRuntime` welding the 3D slice for one simulation tick

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
