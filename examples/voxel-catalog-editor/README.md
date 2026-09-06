# Voxel Sculpt example (MagicaVoxel-style)

A miniature voxel sculptor. Native occupancy + undo live in `VoxelCatalogEditor`.
The presenter is the voxel engine: an 8×8×8 (or larger) `VoxelWorld`, orbit camera,
and DDA ray picking (`Camera3D.screenToRay` + occupancy ray).

Run on Windows:

```powershell
make run/win32-debug GAME=examples/voxel-catalog-editor
```

Controls:

- Left-click in the 3D view to **attach** (default) or **erase** a voxel.
- Right-drag to orbit. Orbit L/R buttons also rotate.
- Switch models in the left list (`cube` is a 4³ block in an 8³ canvas, `bed` is sculpted).
- Undo/Redo: Ctrl+Z / Ctrl+Y.
