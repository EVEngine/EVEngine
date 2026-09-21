# Canonical material schema v10

Schema v10 extends the optional `vegetationFields` object from v9. Its exact
nine fields are `colorsLayer`, `colorsUsePivotPosition`, `extrasLayer`,
`extrasUsePivotPosition`, `motionLayer`, `vertexLayer`, `globalSize`,
`sizeFadeStart`, and `sizeFadeEnd`. Layers are integers in `[0,8]`;
`globalSize` is finite in `[0,1]`; fade distances are finite, nonnegative, and
`sizeFadeEnd >= sizeFadeStart`.

The material owns layer selections and static TVE controls. Array textures,
per-layer usage flags, fallback values, atlas coordinates, revision tracking,
and publication remain runtime state owned by `VegetationField` and its GPU
projection. Loading a material therefore never enables GPU deformation by
itself. The renderer must explicitly choose
`PbrVegetationDeformationSource::GpuFields` after attaching a current nine-layer
Vertex atlas. `CpuDeformed` declares geometry produced by `deformVegetation`;
the two deformation paths cannot be active for the same draw.

`eve.material/9` is the sole migration input. Migration preserves existing
field selections and adds motion/vertex layer zero, global size one, and a
`[0,100]` size fade interval. A malformed v9 `vegetationFields` object is
rejected before publication. Unknown fields remain invalid.
