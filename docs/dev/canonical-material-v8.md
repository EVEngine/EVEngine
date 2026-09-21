# Canonical material schema v8

Schema v8 adds the optional `vegetationDetail` object and three named image
references: `detailAlbedoTexture`, `detailNormalTexture`, and
`detailMaskTexture`. The detail object owns TVE secondary-layer UV selection,
colors, strengths, channel masks, and remap endpoints. Texture references are
runtime-required dependencies and are resolved by the graphics asset loader.

The runtime evaluates the TVE 12.6 order after primary albedo and normal
sampling: select UV0, authored detail UV, or world XZ; sample secondary maps;
remap the selected texture mask and vertex-blue/normal-projection mask; combine
them into the detail blend; then apply albedo, alpha, normal, metallic,
smoothness, and occlusion with that same blend. Reversed remap endpoints are
valid and use the source `0.0001` denominator epsilon. A missing secondary mask
samples canonical white.

`eve.material/7` is the sole migration input. Its migration changes only the
schema version because absence of `vegetationDetail` is the v8 disabled state.
A v7 document that already contains the new object is rejected as unversioned
data. Public readers retain older direct-read support for existing runtime
fixtures, while archive migration keeps the global N-1 policy.

The detail value snapshot owns no external state. Its texture pointers are
borrowed from the graphics image factory and must outlive queued draws, matching
the existing PBR texture contract. Static prefabs hold and release the image
leases. The Vulkan draw copies all scalar state into its fenced per-draw uniform
and binds the three detail images in a dedicated descriptor array.
