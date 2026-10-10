# Independent UDS-style sky

Run with `make run/win32-debug GAME=examples/uds-sky` (or the corresponding host
platform). Requires the Vulkan graphics provider and daynight module.

`sky-procedural.json` is the default version-6 configuration. It needs no imported
assets and starts at 05:00, advancing 0.1 sky hours per second. Change the seed or
clock in this file and restart. The camera uses 1280x720, 75-degree horizontal FOV
and a 15-degree upward view from two metres. External pack usage is described below.
The example explicitly selects the graphics-owned `filmic` display mode. This
does not change the default presentation mode of other examples or legacy skies.
It also enables photographic cosine-fourth vignette at the reference intensity
of 0.4, applied before the final display transform and excluded from HDR captures.
Bloom uses the shared `gaussianPyramid` filter with six weighted Gaussian scales,
intensity 0.675 and threshold 0 (whole-scene input).

The script prepares an independently owned sky, then attaches it to existing
main-view and reflection contributors. It does not initialize or update the
legacy DayNight simulation. Native resources are released with the owned script
object; `sky.detach()` explicitly removes its contributors before collection.

This example displays atmosphere, thin clouds, sun, stars, a lunar disk and analytic
background height fog. Volumetric clouds, precipitation and full reference
post-process parity remain in development. It is not evidence of 99% UDS pixel similarity.
`sky-animated.json` is the version-4 alternative. Select it with `skyProfileFile`
at the top of `main.nut` and restart. It reuses the same imported resources and
keeps noon fixed while cloud morphing advances at the source default speed 0.35.
Its `cloudMotion` controls are phase, speed, timeScale and windMultiplier; zero
speed freezes clouds. Versions 1--3 explicitly preserve their frozen phase.
The returned sky frame exposes cloudTime and normalized wispsMorphPhase.
Cloud morphing uses injected simulation time and does not advance when a second
view/reflection renders. Weather-driven material color, cloud advection and
volumetric cloud updates remain in development.

### Solar disk calibration

Select `sky-sun.json` through `skyProfileFile` to enable the imported clear-noon solar disk.
The `eve.sky-wisps/3` asset adds linear HDR `sunDisk.color` and `sunDisk.shape`
(radius in radians, positive softness exponent, nonnegative reflection multiplier).
Older assets retain a disabled disk. This profile freezes the source noon color;
daytime color curves, the moon and stars are not implemented by this profile.
The original camera looks away from the sun; aim towards native (-0.5,0.8660254,0)
to inspect the disk. The source noon reflection multiplier is zero.

`sky-sun-cycle.json` uses `eve.sky-wisps/4` and advances from 05:00 at 0.1 sky
hours per injected second. Its imported RGB elevation curves use four keys per
channel, flattened in RGB order as `[height,value,arriveTangent,leaveTangent]`.
Height is `(1 + sunDirection.y) / 2`; cubic interpolation is unweighted and
endpoints are constant. `sunDisk.color` is the color at the final curve key;
intermediate values scale relative to that endpoint. The renderer consumes the
existing solar snapshot without a separate clock. The curve is evaluated per
vertex and its uniform color is passed flat to the fragment shader.
Weather attenuation, eclipses, space-mode color and non-default curve adjustments
are not implemented by this profile. Existing version 1/2/3 assets remain unchanged.

`sky-daylight.json` adds profile version 5 directional fog fading.
`heightFog.directionalElevationRange` uses sun-direction Y, not degrees: `[0,0.2]`
fades the authored directional fog color from zero at the horizon to full strength
at Y=0.2, following the source sky-atmosphere fog branch. Equal endpoints disable it;
versions 1–4 retain their constant color. The reference profile selects the version-7 lunar/daylight asset described below.
The earlier material-only asset retains clear-noon optical coefficients; schema 6 below adds the optical cycle.

### Source daylight material and tiling stars

`assets/wisps-daylight.json` (`eve.sky-wisps/5`) retains the material-only stage.
The new chain imports the 21 linear angle/RGB keys from `Fog_Scattering_Color`,
source day/dawn/night cloud tints and the original `Tiling_Stars` / `Stars_Noise`
textures. `assets/daylight-provenance.json` records the source and output hashes.
The current `sky-daylight.json` extends this asset through schema 6 below; older assets retain their previous behavior.

The single runtime clock publishes a normalized solar-day fraction alongside the
sun and cloud phase. The shader derives the manual moon orbit (pitch, yaw,
vertical offset and orbit offset), source twilight falloff, squared night filter,
cloud base color, sun/moon cloud gradient, contrast midpoint, absent-light intensity
boost, night glow and its contribution to background fog. Stars use the source
per-vertex polar UVs, per-day UV translation, color/intensity and frozen noise twinkle.
Sky glow follows cloud composition and precedes the parent contrast transform;
reflection captures use the explicit glow multiplier and the same time snapshot.

Version 5 requires the `daylight`, `stars` and `starsNoise` objects. Unknown fields,
invalid ranges, reordered curve keys and truncated texture payloads fail before
publication. Versions 1 through 4 leave this chain disabled. Star textures declare
RGBA8 linear/sRGB explicitly; upload consumes owned decoded bytes synchronously.

This implements the compared clear-sky/manual-orbit material branch. It does not
implement astronomical stars, lunar-disk rendering, animated twinkle, weather-driven
optical coefficients, moon atmospheric multiple scattering, or the original runtime
star mip chain (the exported source texture contains only mip zero). These are
remaining parity limits, not successful 99-percent pixel acceptance.

### Dynamic optics, moon scattering and runtime star mips

`assets/wisps-optics.json` preserves the optical-cycle baseline (`eve.sky-wisps/6`).
The current `sky-daylight.json` selects `assets/wisps-moon.json` (`eve.sky-wisps/7`).
The previous version-5 `wisps-daylight.json` is retained as the material-only
configuration. Version 6 additionally requires five authored optical color/scale
vectors and explicit mip counts on both star texture descriptors.

Rayleigh scattering follows the original three-color time blend. Absorption
follows the original twilight blend and HSV hue rotation by 180 degrees, preserving
saturation and value. Preparation bakes 81 height slices over sun Y [-0.08,0.32]
into shared immutable 3D transmittance/multiple-scattering textures (about 22 MiB).
Outside this interval the original clear-sky coefficients are constant. Runtime
sampling is continuous between slices; the expensive CPU bake/upload happens once
in `SkyAtmospherePass::prepare`, never in `setFrame` or on each frame. Tests bound
half-slice transmission/multiple-scattering error by 0.002 against direct integration;
this is an explicit numerical approximation, not an exact analytical LUT solution.

The sky-view integration now includes the source manual moon direction, its
horizon intensity filter and the source light's sRGB8 color quantization. Moon
single/multiple scattering and distant fog ambient light consume the same immutable
optical volume and day snapshot as the sun. Phase participates in the view-cache
key. Main views and reflections never advance the simulation clock.

`stars-runtime.rgba16f` stores 12 original GPU-sampled mip levels, largest first,
in linear RGBA binary16 (44,739,240 bytes). These include the reference's Sharpen1
mip generation and runtime compression. They were read at native texel centers
through an explicit-LOD unlit material, with no exposure or tone mapping; the
engine reuses its existing mip upload and trilinear sampler. Provenance is in
`stars-runtime-provenance.json`. The supplied plugin assets remain hash-identical.
The extraction tool is `tools/sky-reference/ue_star_mips.py`.

The remaining scope is still clear weather and manual celestial motion. Lunar
surface rendering, volumetric clouds, astronomical star placement, animated
scintillation, full weather optics and exact UE post-processing are not implemented
by this profile. Completing these three former gaps does not establish complete
UDS parity or 99-percent acceptance for all times/views.

### Lunar surface

Schema 7 adds the original lunar color/alpha and phase-normal runtime mip chains.
The disk follows the existing daily orbit; its manual texture orientation,
fixed phase, earthlight, day/night intensity and glow follow the source material.
The lunar silhouette masks stars and the solar disk, while atmospheric sky stays
additive. Thin clouds, parent contrast, fog, bloom and display encoding follow it.
`moonDisk.lighting[2]` is an authored phase in days (0 and 29.53 full, 14.765 new),
not a second simulation clock. Changing this surface setting does not automatically
change the separately authored moon-light fraction in `daylight.controls[2]`.
Real astronomical lunar simulation and eclipses remain outside this manual-orbit preset.

Both 1024-square resources have 11 mips read from the original GPU texture,
including its SHARPEN4 mip generation and compression. RGB is linear; alpha is
read independently without tone mapping. `moon-runtime-provenance.json` records
source settings and hashes. Old manifests keep the lunar surface disabled.

## External sky packs and procedural default (2026-10-09)

The default `main.nut` uses `sky-procedural.json` and needs **no downloaded assets or Python at runtime**.
The native generator prepares an independent sphere, seamless cloud density, seeded stars/noise,
and a mottled lunar surface/phase normal once. Existing atmosphere, optical cycle, cloud morph,
sun/moon compositing, fog and presentation remain shared. This is a similar-style default, not a
pixel-match replacement for UDS source textures or an astronomical moon map.

To inspect a fixed hour with the development launcher:

```powershell
python tools/sky-reference/run_sky.py --hour 0 --seed 2026
python tools/sky-reference/run_sky.py --pack "D:/SkyPacks/my-sky" --hour 12
```

Run these commands from the repository root. `--engine` accepts a built eve executable; `EVE` is
also honored. Without `--hour` the profile's day cycle runs. Direct `eve run examples/uds-sky`
uses the same native procedural default and does not need the Python launcher.

A replaceable pack is a folder with `sky.json` (`eve.sky-profile`) whose nonempty `wispsAsset`
points to an `eve.sky-wisps/1..7` manifest relative to that folder. Mesh/texture paths inside the
manifest are relative to the manifest. For example:

```
my-sky/
  sky.json                  # wispsAsset: "assets/sky.json"
  assets/sky.json            # density, stars, moon, mesh, material controls
  assets/dome.json           # eve.sky-mesh/1
  assets/clouds.r8
  assets/stars.rgba16f
  assets/moon-color.rgba16f
  assets/moon-normal.rgba16f
```

The launcher copies only scripts and configuration to a temporary game, mounts the pack read-only
at `sky-pack`, and rewrites the profile reference to that VFS prefix. No source textures are copied
into the repository. It checks the pack entry path; the engine validates all nested manifests and
sizes. A supplied missing/malformed pack is an error, not permission to use generated assets.
Changing a pack requires preparing a new sky; this is not live texture hot-reload.

The previous UDS assets remain private outside the repository. `assets/`, `packs/`, local selections
and raw texture/archive extensions in this example are ignored. Keep imported resources outside Git;
only engine/generator code, config templates, documentation and synthetic tests belong in commits.
Legacy `sky-wisps.json`, `sky-daylight.json` and other comparison configurations require their
matching external source pack; they are not the default standalone example.
