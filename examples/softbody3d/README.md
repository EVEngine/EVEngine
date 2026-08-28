# Softbody 3D — Cloth

Interactive 3D Verlet cloth demo: a curtain hangs from a top bar and drapes
over static Box3D bodies (box + sphere). Demonstrates self-collision, the
dihedral fold-angle limit, particle-vs-rigid-body collision, and the
Fluid2D-style `interactAt` pointer field.

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
