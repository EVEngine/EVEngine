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

From an existing debug build, compile the example against the fluids sources and
run it with an output PPM path. The repository's fluid tests cover the individual
simulation invariants.
