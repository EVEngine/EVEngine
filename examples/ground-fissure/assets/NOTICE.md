# Asset sources

- `dirt_diffuse_1k.jpg`: **Dirt**, Charlotte Baglioni / Poly Haven.
  Source: https://polyhaven.com/a/dirt
  File: https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k/dirt/dirt_diff_1k.jpg
  License: CC0 1.0, https://creativecommons.org/publicdomain/zero/1.0/
  Unmodified 1K diffuse texture. No network access is needed at runtime.
- `dust.tga` and `lava.tga`: procedural opacity/noise card and heat texture made for this example.
  Rebuild with `python assets/generate_dust.py` (Pillow required for authoring).
  Ground meshes, rock meshes, crack paths and animation are generated in EVEngine.

Technique references (no tutorial assets redistributed):

- Gabriel Aguiar, Ground Cracks / Fissure / Hole:
  https://www.youtube.com/watch?v=qiAiVa0HtyE
- 1MAFX Ground Crack breakdown:
  https://www.1mafx.com/blog/ground-crack-vfx-breakdown
- SideFX prefracturing and constraints:
  https://www.sidefx.com/docs/houdini/dyno/fracturing.html

This implementation uses displaced Voronoi cells with real sidewalls rather than
the tutorial's stencil mask. Debris is a bounded, deterministic ballistic animation;
it is visual presentation and does not modify gameplay collision or terrain.
