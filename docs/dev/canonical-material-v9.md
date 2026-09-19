# Canonical material schema v9

Schema v9 adds optional `vegetationAlpha` and `vegetationFields` objects.
`vegetationAlpha` owns `global`, `variation`, and `detailFade`; the numeric
values are finite values in `[0,1]`. `vegetationFields` owns the Colors and
Extras layer selections and pivot-position switches. Layers are integers in
`[0,8]`. Absence of `vegetationAlpha` disables TVE global-alpha processing.

The Extras array and its per-layer usage and fallback values remain runtime
global-field state. They are deliberately excluded from the material document,
so `VegetationField` remains their only mutable authority. A material keeps only
the selected layers and coordinate-space choices.

For masked materials, Vulkan evaluates global alpha after secondary-detail alpha
blending. With an active Extras array it clamps the sampled alpha, applies mesh
variation, applies the detail fade mask, and clips the minimum of the adjusted
alpha and the current fade result at the source threshold `0.01`. Without an
active global field, the established canonical alpha-cutoff behavior remains.

`eve.material/8` is the sole migration input. Migration changes only the schema
version because absence of `vegetationAlpha` is the disabled v9 state; a v8
document containing that field is rejected as unversioned data. All material
producers emit v9 so non-vegetation assets remain compatible with the current
schema registry. A v8 document containing either new object is rejected as
unversioned data.
