# Realtime Fog (MAC on the atmospheric froxel path)

The pillar-city scene and display path match `examples/atmospheric-fog`: frustum
height fog, an emissive light proxy, `integrateFroxel`, then `applyFroxel`.
A GPU `mode="fog"` layer (`volumetric_fog.frag`) adds height/distance noise
wisps the same way `examples/bush-fog-volumes` does.

`graphics_fog` only **adds** MAC fluid + SceneWind + the sphere interactor into
that grid (`injectToVolumetric`). It does not replace the Volumetric atlas.

```bash
make run/linux-debug GAME=examples/realtime-fog
```

Controls:

| Key | Action |
| --- | --- |
| `1` / `2` / `3` | Thin / medium / dense height fog (same presets as atmospheric-fog) |
| `A` / `D` | Steer main wind left / right |
| `W` / `S` | Increase / decrease curl strength |
| `←` `→` `↑` `↓` | Move the solid sphere interactor |
| `Space` | Pause / resume MAC simulation |
| `R` | Reseed the MAC height band |

Froxel resolution is the atmospheric-fog default (`80×45×32`, near 0.1, far 100).
MAC lives in a world AABB that covers the camera and the city so the frustum
actually samples it. After a few frames the example writes `realtime-fog.png`.
