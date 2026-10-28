# Dynamic surface fluid reference demo

This deterministic CPU demo exercises the production surface binding, droplet
solver and wetness field on the existing spherical fluid reference surface. The
sphere undergoes a small rigid oscillation to validate dynamic attachment without
replacing the established sphere-based regression scene. It is intentionally a
small offline reference renderer: visual regressions can be inspected without a
GPU while the same material-space simulation data remains suitable for Vulkan
instancing and wet-material shading.

The generated frame demonstrates:

- droplets following a moving triangle surface;
- tangential gravity and cross-triangle transport;
- contact-angle-derived droplet size and merging;
- narrow, fading wet trails with diffusion and evaporation.

After the normal engine configure has prepared `third-party/glm`, build this
standalone CPU example from the repository root:

```powershell
# Windows (Ninja + MSVC)
cmake/with-msvc.cmd cmake.exe -S examples/surface-fluid-dynamic -B build/surface-fluid-demo -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake/with-msvc.cmd cmake.exe --build build/surface-fluid-demo
build/surface-fluid-demo/surface_fluid_demo.exe build/surface-fluid-demo/frame.ppm
```

```sh
# Linux / macOS
cmake -S examples/surface-fluid-dynamic -B build/surface-fluid-demo -DCMAKE_BUILD_TYPE=Debug
cmake --build build/surface-fluid-demo
build/surface-fluid-demo/surface_fluid_demo build/surface-fluid-demo/frame.ppm
```

No window, audio device or Vulkan runtime is needed. A successful run exits zero
and writes the PPM frame. The repository's fluid tests cover the individual
simulation invariants.
