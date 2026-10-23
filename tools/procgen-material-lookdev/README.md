# Procedural material lookdev

This scene compares the 13 everyday generators with the saved AI reference. It
refuses any device other than a discrete NVIDIA GPU. Generated maps are shared
by the glTF PBR path; the final canvas is converted from linear light to sRGB
in a display shader. Image compositing must only crop, resize and label frames.

1. Build `export.cpp` with the current procgen texture, Params and ImageData
   sources and normal engine dependencies. Run `export <game-folder>/assets`.
   This exports deterministic seed-42 1024x1024 maps for all 13 families.
2. Run `python3 tools/procgen-material-lookdev/prepare_scene.py <game-folder>`
   (Pillow required). It packs AO/roughness/metallic, creates the sphere meshes,
   and copies the shaders for the six-wall studio reflection environment. A hard-edged cylinder with
   separate side/cap normals is included as the neutral PBR pedestal.
3. If editing shaders, rebuild with `glslangValidator -V <file.frag> -o
   <file.frag.spv>`; the checked-in SPIR-V must match its source.
4. Run `eve run --mcp-port=7600 <game-folder>` on the host display with
   `VK_DRIVER_FILES=/usr/share/vulkan/icd.d/nvidia_icd.json` and null audio if
   needed. Check the `LOOKDEV_GPU` log, then use engine MCP `eve_screenshot`.
   Retry `no-presented-frame` only after another presented frame.

The before/after stage fixes camera, exposure, environment, geometry, map size,
and coating settings. Wood has no extra clearcoat. Room and pedestal geometry
are included in both the opaque color capture and a packed RGB24 eye-depth pass.
The glass optical probe computes two sphere interfaces and traces the exiting ray
against that depth; uncovered/offscreen rays use the captured backdrop. It is
perspective-only, uses a finite 128-step trace, and still assumes a unit solid
sphere for thickness. It does not implement arbitrary mesh transmission or caustics.
Custom shaders only read the stable mesh Frame prefix through ambient; later
fields differ between engine revisions. Eye depth comes from perspective clip.w.
`scripts/tests/test_material_lookdev.py` checks source and compiled SPIR-V layout.

The engine canvas renderer still omits general shadow and screen-space AO passes.
The pedestal shader supplies scene-specific analytic sphere occlusion and a
32-sample soft shadow; glass shadow attenuation is an approximation. This is a
fixed studio lighting probe, not an engine-wide shadow fix. The room shader applies
static pixel dither for the RGBA8 canvas to avoid quantized gradient bands.

The latest review uses the current workspace CPU generator and current shaders
with a cached hardware engine host. It does not establish that a complete build
of this workspace or a new GitHub release has passed.

Compose the fixed-camera comparison with `python3 tools/procgen-material-lookdev/compare.py
--reference <reference.png> --before <before.png> --after <after.png> --output <folder>`.
This only crops, resizes and labels captures. Keep the original engine screenshots.
The saved colored-stripe optical diagnostic disables reflection to show scene-depth
refraction independently; it is not a material beauty render.


The room uses six actual cube walls on capture mask 1; balls and pedestals use
mask 2. The probe captures only walls, filters the resulting HDR cubemap and
publishes it before material creation. Room bounds and light panel directions
are shared push parameters, including the glass box projection. The checker
floor and framed back wall supply identifiable reflected/refracted features.

The sphere has twice as much physical distance per longitude UV unit as latitude.
Except for directional wood, polished marble and glass, the glTF assembly scales
all surface texture UVs by (2, 1) to keep grains/yarns approximately isotropic.
Paint also binds its normal map to the clearcoat normal slot. Rubber and cloth
use KHR_materials_specular factors 0.22 and 0.15 to reduce the native dielectric
white grazing lobe; these are lookdev assembly settings, not new generator API
parameters. Compare map changes only with the same room and assembly settings.
