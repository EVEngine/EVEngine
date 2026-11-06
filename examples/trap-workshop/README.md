# Trap Workshop

2D teaching room for **scene traps and manipulable props**. It shows how three
engine pieces compose — not a TrapSystem:

| Concern | Module | What this example does |
|---|---|---|
| Volume enter/leave | `physics` sensor + **2D Trigger buffer** | Spike pit, crate-only pressure plate, exit zone |
| Nearby “press E” props | `sensing` | Lever prompt within range (tags, not geometry LOS) |
| When facts change → open gate / disarm | `emergence` | Indexed rising-edge rules; script applies activations |

```sh
make run/linux-debug GAME=examples/trap-workshop
# Windows / macOS: run/win32-debug / run/macosx-debug
MIN_RUN_SECONDS=2 RUN_SECONDS=6 bash scripts/smoke_examples.sh trap-workshop
```

**Controls:** A/D or arrows move, Space jump, E interact (lever), R reset.

## Route

1. **Spike pit** — physics `begintrigger` with sensor tag `zone.spikes`. Damages
   while spikes are armed.
2. **Lever** — `sensing.circle` finds `interactable,lever` near the player; E
   sets Emergence tag `lever.pulled`. Rule `disarm.spikes` fires once.
3. **Crate + plate** — push the amber crate onto the cyan plate. Only visitor
   tag `prop.crate` counts (player standing on the plate does nothing).
4. **Gate** — rule `open.gate` sets state `gate=open`; script lifts the kinematic
   gate. Cross the green exit sensor to finish.

Do not mutate the physics world inside solver callbacks — poll Trigger buffers
after `world.update`, mirror facts into Emergence, then `drain(tick)`.

Font: DejaVu Sans (see `assets/fonts/NOTICE.txt`).
