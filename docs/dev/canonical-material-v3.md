# Canonical material v3

Historical format contract: the current format is [material v6](canonical-material-v6.md).
V3 remains its N-1 input/runtime compatibility format; the v3/v2 window described
below belongs to the preceding tool version.

`eve.material/3` retains material v2 fields and adds the following parameters.
The definition is the authoritative CPU value. The runtime decoder returns an
owning value with image identities and no GPU objects. Prefab loading resolves
images and validates the complete surface before publishing the drawable.

- `albedoTextureStrength`: optional finite number in [0,1], default 1. Mixes
  white with sampled linear albedo RGB before tinting; does not change alpha.
- `colorMask`: optional object; presence enables RGB color blending. Its exact
  required keys are `secondary` (three finite nonnegative linear RGB values),
  `minimum` and `maximum` (finite numbers in [0,1]). Unknown nested keys fail.
  It requires `metallicRoughnessTexture`. Its filtered alpha is clamped to
  [.0001,.9999], remapped by `(value-minimum)/(maximum-minimum+.0001)`, and
  saturated. That factor blends secondary RGB toward `baseColor` RGB.
  Reversed intervals are valid; a zero denominator fails validation.
- `normalEncoding`: optional string, default `tangent-xyz`. Additional values
  `tve-rg`, `tve-rag`, and `tve-ag` select TVE filtered RG, R*A/G, or A/G
  reconstruction. These require `normalTexture`, linear texture sampling and
  finite `normalScale` in [-8,8]. They remap XY, multiply by signed strength,
  and use Z=1 before tangent transformation and final lighting normalization.
  Unknown encodings fail. The CPU definition retains logical image identity;
  the resolved GPU surface is validated before publication.
- `baseColor` RGB now permits finite nonnegative HDR values. Alpha remains
  in [0,1]. The color mask does not blend secondary alpha or change clipping.

Unknown additive root fields remain ignored by the runtime reader and preserved
by migration. Known fields never coerce types. Failed decoding/loading returns
a checked diagnostic without publishing partial state. CPU decoding is reentrant;
texture creation and drawable use follow the graphics factory thread contract.

The v3-era current/N-1 window was v3/v2. Migration v2 to v3 preserves identity and fields,
recomputes definition hashes, and updates local typed dependencies. V2 definitions
containing any of the new fields are rejected as unversioned semantics. V1 is outside
the window and must first be migrated with the preceding tool version; unknown
future versions are rejected. Unity Standard and glTF producers emit v2 and are
migrated during cooking. Existing v2 runtime packs remain readable with original
defaults and normalized base colors. Older readers reject the v3 type rather than
silently ignoring color semantics.

This is the native RGB material contract used for vegetation work. It does not
claim TVE material import, secondary opacity, detail/subsurface shader parity,
or a WebGPU PBR implementation. Vulkan's existing PBR surface applies the fields.
