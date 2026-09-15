# Layered Animation

Procedural stick-figure demo for `AnimLayerMixer`:

- **base** — looping walk (`AnimPlayer`)
- **upper** — override layer with a torso/head bone mask
- **recoil** — additive kick using bind-pose reference (pulses automatically)

```sh
make run/linux-debug GAME=examples/layered-animation
```

Script helpers (from the MCP / console):

- `toggle_upper()` — enable/disable the override layer (clock keeps advancing)
- `set_upper_weight(w)` — blend the aim overlay
- `cycle_recoil_reference()` — flip recoil between `bind` and `identity`

No external assets are required.
