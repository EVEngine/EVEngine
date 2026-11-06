# Multiscale sphere batch view

Requires the optional `graphics_multiscale` module, Vulkan, and the engine's
SPIR-V resource validation provider (missing support returns a diagnostic). Build with
`EVENGINE_MODULE_GRAPHICS_MULTISCALE=ON` and its dependency
`EVENGINE_MODULE_SPATIAL_MULTISCALE=ON`, then run from the repository:

```
make run/win32-debug GAME=examples/multiscale-spheres
```

The 1920x1080 example alternates every four seconds between a red foreground
sphere occluding a blue distant sphere and the blue sphere alone. Both have the
same angular size. Their distances differ by 10^17, in a lattice whose cells
measure 2^900 physical units. The absolute far distance cannot be a binary64
number; decimal cell addresses and relative normalization avoid constructing it.

`eve.MultiscaleView()` is a stateless factory. `prepareSphereFrame(description,
maxSpheres)` returns an immutable owned frame or a checked diagnostic. Descriptions
contain explicit camera position, orthonormal basis/FOV/aspect/cell exponent, and
sphere positions/radii/linear RGB values. Position cell components must be int64
decimal strings; fractional components must be in [-0.5,0.5). Unknown fields are
rejected. The budget is an integer in [1,1000000]; conversion has bounded nesting
and element counts before native preparation. Script numeric precision still
limits supplied fractions and radius mantissas.

`prepareSphereBatch(gfx, frame)` explicitly uploads an owning GPU snapshot and
returns a common Result table. The example prepares two CPU frames and one batch at startup;
every four seconds `batch.update(frame)` publishes the other prepared frame into that batch.
Updates preserve pipeline and resource dimensions, wait for prior GPU use, and reject visible
counts above the initial capacity. Input frames are not retained by the batch.
`batch.draw()` begins the dedicated pass;
the engine loop presents it. Check every Result. Do not combine this pass with
legacy depth rendering. Preparation costs O(N) packing/upload plus pipeline
construction; reuse the batch until data changes. Drawing uses one instanced
submission and fragments within projected rectangles (worst case O(visible N * pixels)).
Frames retain no input tables. The VM owns returned instances; batches revoke
provider access when Graphics dies. `newSphereView(gfx)` and `view.draw(frame)`
remain available as the per-sphere reference path.

This is a rendering integration example, not the complete generated-world demo
or the million-object benchmark. It does not simulate gravity, generate ships or
cities, support subpixel star flux, or establish 60 FPS for a million states.
Current renderer precision/backend restrictions are documented in
`src/modules/graphics/multiscale/README.md`.
