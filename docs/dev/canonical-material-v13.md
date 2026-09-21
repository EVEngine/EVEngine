# Canonical material schema v13

Schema v13 adds the optional `vegetationEmission` object. Its exact members are
`minimum`, `maximum`, `phase`, and `global`; every member is finite and in
`[0,1]`. Presence enables the TVE 12.6 emissive stage and requires an
`emissiveTexture` dependency. `emissive` stores linear HDR RGB and
`emissiveStrength` stores Unity's baked intensity value.

The fragment stage samples the emissive texture, applies its declared color
decode, remaps RGB with `(sample - minimum) / (maximum - minimum + 0.0001)`,
then adds `lerp(1, Extras.r, global) * phase - 1` and saturates. The resulting
mask multiplies the HDR emissive color and baked intensity. The Extras texture
and selected field layer remain runtime-owned borrowed state.

Unity Nits authoring uses `_EmissiveIntensityValue` directly when the serialized
internal `_emissive_intensity_value` is absent. EV100 authoring falls back to
`0.125 * 2^value`, matching TVE's editor utility. A present internal value is
authoritative because Unity serializes the value actually consumed by the
shader.

`eve.material/12` is the sole migration input. Migration validates that no
unversioned `vegetationEmission` member exists and changes only
`schemaVersion`; it never invents emission for legacy assets. Validation and
runtime loading reject malformed objects or emission without a texture before
publishing a material.
