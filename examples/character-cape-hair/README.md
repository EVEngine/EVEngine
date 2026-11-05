# Character Cape + Hair Physics

Internet-sourced pixiv female character with two real secondary-motion systems:

1. **Flowing hair** — `DynamicBoneSolver` / `setupHairChain` on the model's
   `J_Sec_Hair*` secondary bones, plus a head sphere collider.
2. **Moving cape** — `Cloth3D` XPBD cloth (gravity, wind aerodynamics, self-collision)
   with the top edge pinned to the shoulder / upper-chest bones each frame.

The model has no authored cape mesh, so the shawl is a procedural hanging cloth panel
attached to the skeleton. Assets are vendored under `assets/pixiv/` (offline-safe);
see that folder for the VRM Public License 1.0 notes.

## Run

```bash
make run/linux-debug GAME=examples/character-cape-hair
# or
cd examples/character-cape-hair && ../../build/linux-debug/src/engine/eve run
```

## Controls

| Input | Action |
|---|---|
| Space | Toggle wind |
| R | Reset cape pose + hair solver |
| Left drag | Grab / drag the cape |
| Right drag | Orbit camera |
| Mouse wheel | Zoom |

## Physics notes

- Hair chains are discovered by bone-name pattern `J_Sec_Hair1_NN` → tip.
- Cape starts as a native XZ `Cloth3D` grid, then particle positions are remapped into
  a vertical XY panel behind the torso (same approach as `physics-workshop`).
- Top-row particles stay pinned and are driven from `leftShoulder` / `upperChest` /
  `rightShoulder` world positions after Dynamic Bone updates the pose.
