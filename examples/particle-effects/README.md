# Particle effect reference pack

JSON effect assets for `particles.newEffectFromFile`, not a standalone
`eve run` project. Load one of the files from a game or from the particle
tests (`test/particles_p2_p3.cpp`).

| File | Contents |
|---|---|
| `fire.effect.json` | Additive looping fire with intensity timeline |
| `smoke.effect.json` | Soft rising smoke |
| `impact.effect.json` | Burst + sparks via event routes |
| `trail.effect.json` | Distance-emission ribbon trail |
| `weather.effect.json` | Rain / snow style overlay |

Schema is `eve.particle-effect` v1 or v2 (`eventRoutes`, `timeline`). See
`docs/usr/modules/particles.md` for loaders, timeline cues, and GPU fallbacks.
