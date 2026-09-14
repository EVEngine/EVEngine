# Softbody 3D — Cloth and volumetric body

Interactive 3D soft-body demo: a Verlet curtain hangs over static Box3D bodies,
while an orange volumetric body falls, squashes and recovers using overlapping
shape-matching clusters. It demonstrates cloth self-collision/fold limits and
the volumetric component's deformation resistance, volume preservation,
plasticity, and rigid-body collision.

## Run

```bash
make run/linux-debug GAME=examples/softbody3d
```

## Controls

| Input      | Action                                  |
|------------|-----------------------------------------|
| Left drag  | Grab / drag the cloth                   |
| Space      | Toggle wind                             |
| C          | Toggle self-collision                   |
| F          | Toggle fold-angle limit                 |
| R          | Reset the cloth to its flat pose        |

The physics world uses Box3D (`physics.newWorld3D`, meter space, +Y up); the
cloth grid lies in the XZ plane with the top row pinned.
