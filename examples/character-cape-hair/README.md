# Character Cape + Hair Physics

Internet-sourced pixiv female character with three real secondary-motion systems:

1. **Flowing hair** — `DynamicBoneSolver` / `setupHairChain` on the model's
   `J_Sec_Hair*` secondary bones, plus a head sphere collider.
2. **Clothing cloth** — the same spring-bone solver on the authored Tops soft bones
   (`J_Sec_*_TopsUpperLeg*` hem + `J_Sec_*_TopsUpperArm*` sleeves, and bust secondaries).
   These are the clothing springs from the original VRM `VRMC_springBone` data; the
   skinned shirt mesh deforms with them (stiffer than hair so the silhouette holds).
3. **Moving cape** — `Cloth3D` XPBD cloth (gravity, wind aerodynamics, self-collision)
   with the top edge pinned behind the upper back, colliding against kinematic sphere
   proxies on the torso and thighs so it does not clip through clothing.

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
| R | Reset cape + secondary bones |
| Left drag | Grab / drag the cape |
| Right drag | Orbit camera |
| Mouse wheel | Zoom |

## Physics notes

- Hair chains are discovered by bone-name pattern `J_Sec_Hair1_NN` → tip.
- Clothing chains follow the source VRM Tops spring groups (root → `*_end`), with
  higher stiffness/damping than hair and torso/hips bone colliders on the solver.
- Cape starts as a native XZ `Cloth3D` grid, then particle positions are remapped into
  a vertical XY panel behind the torso (same approach as `physics-workshop`).
- Top-row particles stay pinned on a line behind `upperChest` after Dynamic Bone updates.
- Body collision: a back-facing lattice of kinematic spheres (torso + thighs);
  `cape.setCollideWorld(bodyWorld)` probes them each step.
