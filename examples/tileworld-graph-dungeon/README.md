# TileWorld Graph Dungeon

Asset-free 3D example for the TileWorldCreator-style graph port.

The live path is:

`GridGraph(GeneratorRegistry(level.roguelike) -> semantic layers -> autotile -> random selection -> grid-to-points)`
`-> PointGraph(transform)`
`-> BuildLayerStack + IncrementalBuildExecutor(cluster delta)`
`-> MeshGraph(grid tiles + point instancing)`
`-> MeshBuild -> Vulkan renderables`.

The example uses four independent build layers:

- autotile-mask floor groups with per-group material tints;
- a raised boundary-wall layer extracted from the roguelike generator's wall-autotile masks;
- a gold navigation layer produced by `grid.path` between deterministic endpoints;
- sparse procedural rock instances generated through PointGraph.

The example executes the stack through an 8x8-cell incremental cluster cache.
Its scene cache consumes each upsert/removal delta to replace or hide only the
affected floor and prop renderables, then repeats the same update to prove the
stable delta is empty. Each live cluster also owns a static triangle-mesh
collider derived from the exact same floor artifact. Collider replacement is
construct-before-destroy, and removal destroys the matching body. When the
`physics` module is trimmed, this optional consumer is visibly reported as
`physics=disabled` and rendering continues. The example does not execute a
second full-stack preview build.

- `R`: regenerate with the next deterministic seed.
- `Space`: pause/resume the camera orbit.

Run from the repository root:

```powershell
make run/win32-debug GAME=examples/tileworld-graph-dungeon
```
