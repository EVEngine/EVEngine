# Realtime fog system

> Status: CPU orchestrator + existing AtmosphereVolume froxel display path  
> Module: `graphics_fog` (`src/modules/graphics/fog/`)  
> Script: `eve.RealtimeFog()`  
> User manual: [`docs/usr/modules/graphics_fog.md`](../usr/modules/graphics_fog.md)

Four cooperating routes — world-space ray marching, MAC fluid transport,
camera froxel integration, and analytic volumetric lights — share one density
owner. Art parameters never write density, velocity, or optical transport.

## Layers

| Layer | Owner | Writes |
| --- | --- | --- |
| Density Field | `FogDensityField` | density bands, curl assist, lighting assist |
| Optical profile | `FogProfile` | σ_t per density, albedo, AO, up/down ambient, HG anisotropy |
| MAC fluid | `MacFluidGrid` | staggered velocity, pressure, concentration |
| Wind | `SceneWind` | rate-limited main wind + independent curl |
| Interactors | `FogInteractor` | solid mask, interior clear, wake pair |
| Beer cache | `BeerLightCache` | derived transmittance (invalidated on revision/light/view) |
| Froxel | `FogFroxelBridge` → `AtmosphereVolume` | display projection |
| Analytic lights / dust | `AnalyticalVolLight`, `ProceduralDust` | beam integrals, deterministic motes |
| Continuous Art | `ContinuousArt` | color/edge/glow/silver dust **from** `FogRayResult` only |

Quality presets `fast` / `enhanced` / `physical_reference` change sample count,
froxel size, temporal/spatial reconstruction, occupancy skip, and whether the
Beer cache is used. They do not change the physical profile.

## Simulation

1. SceneWind ticks with a finite response rate (no wind jumps).
2. Interactors rasterize sphere/capsule/OBB proxies, clear interior density,
   and stamp windward / leeward / tangential wake onto MAC faces.
3. Forces + wind, velocity advection, pressure projection, density advection.
4. Density advection uses bounded grid traversal so thin proxies are not skipped.
5. Wall-clock `dt` accumulates into a fixed substep (`1/60` by default, cap 0.25 s).

CFL = `maxSpeed * dt / cellSize`; reports `stable` when ≤ 1.

## Rendering

Camera rays intersect a sky shell, height layer, or local OBB, then sample
**world-space** density. Extinction uses Beer–Lambert; scattering uses dual-lobe
Henyey–Greenstein. Stable media segments use a closed-form transmittance step.

Froxel mapping reconstructs world centers with the same `invViewProj` mix as
`AtmosphereVolume::injectHeightFogFrustum`, injects MAC density **additively**,
optionally skips empty cells, and integrates Beer visibility at those same
world points. Display lighting for the demo path is `Volumetric::integrateFroxel`
after frustum height fog + emissive proxies.

Analytic beams are capped cone/pyramid frustums clipped by scene depth, using
2 / 4 / 8 short segments. Dust Fine/Mid motes are hashed in world space with a
continuous turbulence offset — no persistent particle buffer, no screen trails.

## Ownership

`RealtimeFog::newSystem` transfers a `FogSystem` to the caller. The module owns
no live systems. `BeerLightCache` and froxel contents are projections: changing
density revision, light direction/intensity, or quality invalidates them.
