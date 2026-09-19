# Canonical material v7: TVE global Colors

This document records the historical `eve.material/7` global-colors addition.
The current version is v8; see `canonical-material-v8.md`. When v7 was current, version 6
definitions preserve every existing field during migration. When they contain a
`vegetationSurface`, migration adds the v7 Colors defaults; a v6 object that
already spells a Colors field is rejected because that data had no versioned
meaning in v6. All built-in glTF and Unity material producers emit v7 directly.

Version 7 retains the seven v6 `vegetationSurface` fields and adds exactly:

- `colors`, `colorsMask`, and `colorsVariation` in `[0,1]`;
- `colorsIntensity` in `[0,2]`;
- `vertexOcclusionMinimum` and `vertexOcclusionMaximum` in `[0,1]`, including
  reversed ranges when their epsilon-adjusted denominator is nonzero;
- `invertVertexOcclusionColors`, independently from overlay occlusion inversion.

The object owns material-local controls. `applyVegetationSurface` supplies the
sampled field RGBA without changing the material base tint. The Vulkan fragment
stage evaluates Colors after base-color and ORM sampling. It applies the field
alpha's `1-(1-a)^2` grayscale influence, the linear-space source multiplier
`4.594794`, ORM-alpha mask remapping, mesh variation, separately remapped vertex
occlusion, textured luminance, and the source `0.1..0.2` final remap. Overlay,
wetness, translucency, and motion highlight then consume the colored albedo in
their existing order.

The sampled field and mesh streams are frame-local inputs. `PbrSurface` owns a
value snapshot and retains no callback or external pointer for these controls.
Publishing and rendering continue to follow the graphics render-thread contract.
