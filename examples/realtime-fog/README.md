# Realtime Fog (MAC + Froxel)

Demonstrates the `graphics_fog` satellite: MAC fluid transport, SceneWind,
analytic sphere interactors, and froxel composite through the existing
`Volumetric` atlas path.

```bash
make run/linux-debug GAME=examples/realtime-fog
```

Controls:

| Key | Action |
| --- | --- |
| `1` / `2` / `3` | Fast / Enhanced / Physical Reference quality |
| `A` / `D` | Steer main wind left / right |
| `W` / `S` | Increase / decrease curl strength |
| `←` `→` `↑` `↓` | Move the solid sphere interactor |
| `Space` | Pause / resume simulation |
| `R` | Reseed height fog |

Each frame the example steps the MAC solver, syncs density into the Volumetric
froxel grid (`FogSystem.syncToVolumetric`), uploads the slice atlas, and
composites with GBuffer depth.
