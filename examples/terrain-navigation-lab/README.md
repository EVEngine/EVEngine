# Terrain Navigation Lab

A real Cesium Man character compares adjustable actor traversal policies on one
continuous 64 × 64 metre heightfield rendered as four chunks.

Run with an engine that enables procgen, physics, map, model3d, graphics and UI:

```sh
cd examples/terrain-navigation-lab
../../build/cloud-minimal/src/engine/eve run
```

Cyan starts at 35° maximum slope with slope cost 2. Amber starts at 75° with no
additional slope cost. Edit the sliders, then press Reset routes. Each policy owns
its cost raster and pathfinder. Both A* and flow-field reachability are checked.
The same height samples, metre spacing and origin feed rendering and physical
collision; movement uses a capsule and the corresponding maximum slope.

The left camera covers the full map (far plane 400 m); the right follows Cyan.
Waypoint cubes show the routes. The model is a real imported glTF asset; this
example displays its bind pose, without an animation claim.

Use `python3 verify.py --seconds 120` for a fresh headless run. It checks startup,
both arrivals, crashes, script diagnostics, progress and process memory growth.
Engine-owned screenshot output is `overview.png`.

See [VERIFICATION.md](VERIFICATION.md) for current results and limits, and
[asset attribution](assets/ATTRIBUTION.md) for the model licence.
