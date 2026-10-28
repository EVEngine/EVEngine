# Ported material & post shaders (Unity / Godot style)

A curated set of "mature" game-engine shaders, exposed through the existing
`stylize` module so they share its `StyleInstance` / `StylePass` / `StyleRecipe`
runtime and the same Squirrel API as `cartoon`, `ink`, `xray`, etc. No new
module, manifest entry, or binding is required.

## What was added

**Mesh material shaders** (reuse `graphics/mesh3d_toon.vert`; params arrive as
push constants; albedo at binding 1):

| style     | technique                    | inspiration                                    |
|-----------|------------------------------|------------------------------------------------|
| `rim`     | fresnel rim light + lambert  | Unity built-in *Rim Lighting* shader           |
| `dissolve`| noise dissolve with burn edge| Unity *Dissolve* / URP community shaders       |
| `hologram`| fresnel + scanlines + glitch | Godot community *Hologram* shaders             |
| `snow`    | snow accumulation on normals | Godot community *Snow / Ice* shaders           |

**Post-process shaders** (full-screen passes over the scene color):

| style      | technique                     | inspiration                             |
|------------|-------------------------------|-----------------------------------------|
| `vignette` | edge darkening                | ubiquitous engine vignette              |
| `chromatic`| radial + per-channel dispersion| Unity Standard *Chromatic Aberration*  |
| `grain`    | luminance-adaptive film grain | filmic grain / dither techniques        |

## Licensing note

These are **original GLSL implementations** of widely-used, technique-level
effects — not verbatim copies of Unity or Godot shader source. The Unity
built-in shaders and many community shaders carry restrictive or mixed licenses,
so nothing here was copied wholesale. The underlying math (value-noise / fbm,
fresnel, smoothstep blending, film-grain dither) is standard public-domain
technique. Godot's engine is MIT-licensed, but the specific ports above were
written fresh against EVEngine's push-constant / binding layout.

If a specific commercial look is required, replace the GLSL in
`src/modules/stylize/shaders/` and recompile via
`scripts/compile_stylize_shaders.py`.

## Adding a shader (in 4 steps)

1. Drop a `*.frag` in `src/modules/stylize/shaders/` following the push-constant
   convention in the header comment (`layout(push_constant) uniform Externals
   { float data[32]; } u;`, index = `declareFloat` order).
2. Append it to `FRAGS` in `scripts/compile_stylize_shaders.py` and run it to
   produce the embedded `*_frag_spv.inc`.
3. In `src/modules/stylize/EffectShaders.cpp` add a `StyleDefinition`, a param
   table, and branches in the count / at / bind / create dispatchers.
4. Optionally add a test case in `test/stylize_effects.cpp`.