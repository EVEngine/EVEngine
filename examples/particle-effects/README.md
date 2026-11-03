# Particle effect reference pack

JSON effect assets for `particles.newEffectFromFile`, not a standalone
`eve run` project. Load one of the files from a game or from the particle
tests (`test/unit/particles/particles_tests.cpp` / render-presentation).

Runnable Phase 1 showcase: [`../unity-vfx-port-lab/`](../unity-vfx-port-lab/).

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

Provenance: [`SOURCES.md`](SOURCES.md). Schema is `eve.particle-effect` v1 or v2
(`eventRoutes`, `timeline`). See `docs/usr/modules/particles.md` for loaders,
timeline cues, and GPU fallbacks. Porting process:
`docs/dev/superpowers/specs/2026-11-03-unity-vfx-porting-plan.md`.
