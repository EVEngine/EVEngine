# Canonical material v4

Historical format contract: the current format is
[material v6](canonical-material-v6.md). V4 is outside the current read/migration
window and cannot carry `motionHighlightColor`.

`eve.material/4` retains v3 semantics and adds the optional `translucency` object.
Its owning CPU definition is decoded before image resolution; only a fully
validated surface is published by the graphics loader. Decoding is reentrant and
retains no references to the input Value. GPU submission follows the existing
graphics-thread contract.

Known optional object fields and defaults are:

- `color`: three finite nonnegative linear RGB values, default [1,1,1].
- `intensity`: [0,1], default 0 (disabled).
- `strength`: [0,50], default 1.
- `normalDistortion`: [0,1], default 0.5.
- `scattering`: [1,50], default 2.
- `direct`, `ambient`, `shadow`: [0,1], defaults 0.9, 0.1, 0.5.
- `maskAmount`: [0,1], default 0.
- `maskMinimum`, `maskMaximum`: [0,1], both default 0. Reversed intervals are
  valid, but maximum-minimum+0.0001 must be nonzero.

Unknown nested keys and type coercions are rejected. Unknown additive root fields
remain ignored by the runtime reader and preserved by migration. Active intensity
with positive maskAmount requires `metallicRoughnessTexture`; its linear alpha is
clamped to [0.0001,0.9999], remapped by the independent translucency interval,
saturated, then interpolated from one using maskAmount. Color-mask settings are
independent. The CPU reader verifies logical identity; resolved texture validity
is checked again before rendering. Global/overlay coefficients belong to runtime
field evaluation and are not persisted in this material object.

The compatibility window is v4/v3. V3-to-v4 migration preserves identity and
unknown root fields, recomputes definition hashes and rewrites local typed
dependencies. A v3 definition containing `translucency` is rejected as unversioned
semantics. V2 and earlier require the preceding migration tool. V3 runtime packs
remain readable with disabled translucency; v4 chunk and definition versions must
agree. Older readers reject v4 rather than silently dropping its lighting.

Standard Unity and glTF importers emit v3, which cooking migrates to v4. TVE
materials emit v4 directly. Only the admitted Plant/Prop Subsurface shader GUIDs
enable imported translucency. Visible TVE controls supply power, angle, normal,
direct, ambient and shadow values; their source defaults are explicitly written
and differ from the generic native defaults. Plant uses its subsurface mask;
Prop has no such mask. Subsurface RGB follows the importer color conversion.

Vulkan implements the initial per-light contribution. Native ambient RGB is its
explicit GI input; baked-lightmap parity, complete wetness/rim stages, field
composition, shadow-map validation and WebGPU PBR remain outstanding. This format
integration is not a claim of complete TVE shader parity.
