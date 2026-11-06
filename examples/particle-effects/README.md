# Particle effect reference pack

JSON effect assets for `particles.newEffectFromFile`, not a standalone
`eve run` project. Load one of the files from a game or from the particle
tests (`test/unit/particles/particles_tests.cpp` / render-presentation).

Runnable Phase 1–3 showcase: [`../unity-vfx-port-lab/`](../unity-vfx-port-lab/).

| File | Contents |
|---|---|
| `fire.effect.json` | Additive looping fire with intensity timeline |
| `smoke.effect.json` | Soft rising smoke |
| `impact.effect.json` | Burst + sparks via event routes |
| `trail.effect.json` | Distance-emission ribbon trail |
| `weather.effect.json` | Rain / snow style overlay |
| `bonfire.effect.json` | Layered campfire (Unity Bonfire intent, reauthored) |
| `portal.effect.json` | Ring suction portal (Unity Portal intent, reauthored) |
| `fireworks.effect.json` | Shell → stars fireworks (VfxPyro intent, reauthored) |
| `smoke_flipbook.effect.json` | 6×6 atlas smoke puff + wisps (Phase 3) |
| `fire_flipbook.effect.json` | Atlas-tinted flame tongue + embers (Phase 3) |
| `ribbon_dissipate.effect.json` | Dissipating distance ribbon + mist (Phase 3) |
| `textures/smoke-flipbook-atlas.png` | Shared flipbook atlas (from `smoke-flipbook` bake) |

Provenance: [`SOURCES.md`](SOURCES.md). Schema is `eve.particle-effect` v1 or v2
(`eventRoutes`, `timeline`). See `docs/usr/modules/particles.md` for loaders,
timeline cues, and GPU fallbacks. Porting process:
`docs/dev/superpowers/specs/2026-11-03-unity-vfx-porting-plan.md`.
