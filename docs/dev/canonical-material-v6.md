# Canonical material v6: vegetation surface parameters

Version 6 introduced the first vegetation surface contract. The current version
is v7; see `canonical-material-v7.md`. When v6 was current, v5
definitions migrate by preserving every field and advancing the version. A v5
definition containing `vegetationSurface` is rejected because that field had no
versioned meaning before v6.

V6 added the optional `vegetationSurface` object. It is present only for imported
TVE material families and has exactly these fields:

- `sourceFamily`: the string `tve-12`;
- `overlay` and `wetness`: material multipliers in `[0,1]`;
- `overlayVariation` and `overlayProjection`: source controls in `[0,1]`;
- `vertexOcclusionAlpha`: source occlusion influence in `[0,1]`;
- `invertVertexOcclusion`: whether overlay uses one minus the mesh channel.

The object owns material-local values. It does not own the sampled vegetation
field, global overlay RGB/wetness contrast, or mesh-authored variation and
occlusion. The runtime reader returns it separately as an optional owning
`VegetationSurface`; `wetness` maps to its material `wetnessCoverage`, while
the independently owned global `wetnessContrast` defaults to 0.5. Callers combine it with a field sample through
`applyVegetationSurface`. Static material loading therefore does not interpret a
wetness multiplier of one as an already wet surface.

Vulkan carries authored variation/occlusion pairs in a Mesh-owned stream. The
fragment stage evaluates normal projection and luminance after texture and normal
sampling, applies variation and vertex occlusion, remaps the combined source mask
from 0.1 to 0.2 with the source epsilon, then applies overlay and component-wise
wetness before motion highlight. Missing mesh factors use neutral values.

The Vulkan surface stage also applies TVE's overlay and wetness normal scales,
sets overlay metallic to zero, blends roughness toward the overlay smoothness,
and applies the source wetness smoothness curve. The current canonical object
does not serialize those global controls; the render-field adapter owns their
source defaults independently of the material-local v6 values.
