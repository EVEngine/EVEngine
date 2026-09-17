# Canonical material schema v15

Schema v15 adds the required top-level Boolean `alphaToCoverage`. It records a
material's request to derive multisample coverage from fragment alpha. The
field owns no graphics resource and defaults to `false` only while reading the
single supported legacy input, `eve.material/14`.

The TVE 12.6 importer maps `_RenderCoverage` to this field only when
`_RenderClip` is enabled, matching `TVEUtils.SetMaterialSettings`: disabling
alpha clipping forces the hidden `_render_coverage` pipeline value to zero.
Both properties must be integral zero or one. Generic Unity and glTF material
producers write `false` explicitly.

Vulkan enables `alphaToCoverageEnable` only for a masked PBR draw on a
multisampled target. Opaque and transparent modes ignore the request, and a
single-sample target remains unchanged. Pipeline-cache identity includes the
coverage state so materials cannot reuse an incompatible pipeline.

Migration accepts only a structurally versioned v14 material without an
existing `alphaToCoverage` member, preserves all other fields and writes
`false`. Runtime loading rejects v15 definitions that omit the required field.

TVE's `_RenderDirect`, `_RenderAmbient`, and `_RenderShadow` are deliberately
not mapped by this schema. In all four supplied Plant/Prop Lit shaders they are
declared but never referenced by generated shader calculations. TVE's own
property filter exposes them only to its separate Vertex Lit family, which is
not present in the admitted shader set. Inventing PBR lighting multipliers for
these names would not reproduce the inspected source.
