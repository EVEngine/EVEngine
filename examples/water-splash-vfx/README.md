# Water Splash VFX — clay sheets + semi-transparent noise lip

Matches the common stylized splash setup:

1. **Mesh** — nested flared “clay” sheets (`mesh.water_splash`): continuous open crowns,
   UV.v from ground → lip. No jagged geometry in the mesh itself.
2. **Material** — `shaders/splash.frag`: semi-transparent water. Solid cyan below a
   **noise-carved** cut, mottled white foam near the lip, transparent above so nested
   crowns and the burst animation stay readable.

Run:

```sh
make run GAME=examples/water-splash-vfx
MIN_RUN_SECONDS=2 bash scripts/smoke_examples.sh water-splash-vfx
```

Regenerate SPIR-V after editing the fragment source:

```sh
glslc -fshader-stage=frag shaders/splash.frag -o shaders/splash.frag.spv
spirv-val shaders/splash.frag.spv
```

## Mesh parameters (`procgen`)

| Key | Default | Meaning |
|-----|---------|---------|
| `height` / `width` | `2.4` / `3.2` | overall sheet size |
| `layers` | `4` | nested crowns (2–4) |
| `flare` | `1.0` | outward crown flare |
| `onlyLayer` | `-1` | emit one layer for staggered VFX |
| `radialSegments` / `heightSegments` | `32` / `12` | sheet resolution |

## Look parameters (example shader uniforms)

| Control | Meaning |
|---------|---------|
| Edge noise | jaggedness of the transparent cut |
| Foam | white mottling strength under the cut |
| Opacity / Soft edge | overall alpha and feather width |

Burst scrolls the noise mask upward while scaling each clay sheet, which is why the
look stays tied to simple geometry.
