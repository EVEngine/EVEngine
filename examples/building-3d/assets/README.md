# Simple house fixtures

Original, deterministic low-poly test assets, dedicated to the public domain under
CC0-1.0. No downloaded models, textures, or material files are required.

- `cottage.obj`: gable roof, chimney, raised door/window frames (92 triangles).
- `barn.obj`: long gable-roof house with double doors (56 triangles).
- `pavilion.obj`: four-sided pyramid roof and door/window frames (78 triangles).

All files use triangles, explicit flat normals, one mesh, Y-up, and a front facing
+Z. X/Z fit within [-0.5, 0.5]; Y spans [-0.5, 0.5]. The base is Y=-0.5, so the
BuildingFx definitions use `offsetY = height / 2` to rest on the ground. Geometry
is slightly inset from the normalized footprint so adjacent houses remain distinct.
The example gives the barn a 3x2 footprint to expose rotation/occupancy mistakes.

Regenerate with `python examples/building-3d/assets/generate_houses.py` from the
repository root. The checked-in OBJ files are the fixtures; regeneration is optional.
