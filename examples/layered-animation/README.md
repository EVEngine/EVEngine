# Layered Animation (real KayKit clips)

Bootstraps three CC0 KayKit animations, plays each alone, and shows the
`AnimLayerMixer` composition beside them:

| Slot | Clip | Role |
|------|------|------|
| 1 | `Walking_A` | locomotion alone |
| 2 | `Melee_1H_Attack_Chop` | upper-body source alone |
| 3 | `Hit_A` | additive source alone |
| 4 | layered mix | walk **base** + spine-masked attack **override** + hit **additive** |

```sh
./fetch_assets.sh   # copies verified KayKit files from combat-action-editor
make run/linux-debug GAME=examples/layered-animation
```

Console helpers:

- `toggle_upper()` — enable/disable the override layer
- `set_upper_weight(w)` — blend the attack overlay
- `cycle_hit_reference()` — flip additive hit between `bind` and `identity`

After a few frames the example writes `layered-animation.png` (engine-owned
screenshot) into the game directory.

Licenses: `assets/kaykit/LICENSE-KAYKIT-*.txt` (CC0).
