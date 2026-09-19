# TVE overlay and wetness source contract

This records the inspected TVE 12.6 Universal 6000.0+ Plant Subsurface Lit
fragment path and native gaps. It is not a completion claim.

The source orders the relevant RGB stages as follows:

1. Produce `Blend_Albedo_Colored` after main texture, HDR material colors,
   global color field, variation and occlusion (lines 1522-1538).
2. Form material/global overlay strength as `_GlobalOverlay * extras.z *
   overlayVariation` (lines 1562-1565).
3. Multiply overlay strength by normal-facing projection, luminance-derived
   shading and vertex-occlusion influence; remap 0.1..0.2 with the source
   `+0.0001` denominator, then saturate (lines 1599-1619).
4. Interpolate colored albedo to global `TVE_OverlayColor` by that mask
   (lines 1620-1621).
5. Form wetness as `extras.y * _GlobalWetness`, then interpolate overlay RGB
   toward its component-wise square using `TVE_WetnessContrast * wetness`
   (lines 1622-1626). The shader does not use a scalar darkening multiplier.
6. Use this wetness-stage RGB for translucency. Motion highlight is applied
   afterward to final base color, as documented separately.

The former `applyVegetationSurface` path evaluated a CPU approximation before
texture sampling. The current path publishes distinct stage inputs and leaves
texture-dependent color, normal, metallic, and roughness changes to the fragment
shader.

Native integration must keep ownership distinct: material `_GlobalWetness` and
overlay controls belong to the material snapshot; sampled extras belong to the
field sample; global contrast/color and enabled state belong to the render-field
adapter; normal, texture albedo and luminance shading are fragment data. A CPU
tint cannot become a second authoritative color source.

Implementation requires an explicit validated PBR vegetation-stage value and
matching CPU/Vulkan/WebGPU contracts. Verification must cover texture-dependent
RGB, component-wise squaring, top/side normal projection, luminance and vertex
occlusion masks, zero/one field values, material multipliers, highlight ordering,
roughness/normal coupling and provider-present/provider-absent configurations.

## Initial native color-stage integration

`PbrSurface` now owns validated overlay RGB/amount and wetness amount values.
Vulkan's CPU and both shader uniform layouts advance together to 1120 bytes.
After base texture and alpha clipping, the fragment shader blends overlay RGB,
interpolates that result toward its component-wise square for wetness, snapshots
the wetness-stage albedo for translucency, then applies motion highlight to final
base color. Shader compilation succeeds.

`applyVegetationSurface` now publishes overlay and wetness stage inputs rather
than baking a scalar darkening and overlay into the CPU tint. `wetDarkening` is
replaced by `wetnessContrast`, matching the source operation.

The native path now owns interleaved authored variation/occlusion pairs on Mesh
and transports them through vegetation create/update without recomputing them.
The Vulkan fragment shader evaluates the source luminance, variation, projected
normal and occlusion influence before the 0.1..0.2 epsilon remap. Missing mesh
factors are neutral. GPU tests compare the factor result to an independently
precomputed tint and verify that a side normal suppresses overlay at full
projection.

Canonical material v6 preserves the local overlay/wetness multipliers,
variation, projection, occlusion alpha and inversion in a strict optional
`vegetationSurface` object. The runtime reader keeps those material-local values
separate from dynamic field samples. The Vulkan stage applies the source global
defaults (0.5) for overlay normal, overlay smoothness, wetness contrast and
wetness normal, forces overlay metallic to zero, and derives roughness from the
source smoothness equations. GPU comparisons isolate the overlay smoothness,
wetness smoothness and overlay normal-scale formulas. The translucency intensity
also interpolates toward the source global overlay-subsurface default (0.5) using
the pre-projection overlay amount, matching source lines 1768-1776. Complex
overlay textures remain pending, so complete color-stage parity is not claimed.
