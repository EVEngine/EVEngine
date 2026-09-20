# Canonical material schema v14

Schema v14 adds the optional `vegetationGradient` object and completes the
existing TVE vertex-occlusion surface data with `vertexOcclusionColor`. The
gradient's exact members are
`colorOne`, `colorTwo`, `minimum`, and `maximum`. Colors are finite,
non-negative linear HDR RGB triples. Endpoints are finite values in `[0,1]` and
must keep the source denominator `maximum - minimum + 0.0001` nonzero.
Presence enables the TVE 12.6 height-gradient stage.

The fragment stage reads mesh height from the canonical vegetation factor
stream, saturates `(height - minimum) / (maximum - minimum + 0.0001)`, and
interpolates from `colorTwo` to `colorOne`. It then interpolates from white to
that color with TVE's blended blue mask and multiplies base color. When the
detail layer is active, the blue mask follows the same detail blend that TVE
uses for its albedo layers. The gradient executes before global Colors-field
tint and overlay/wetness, matching the generated Plant Standard shader.

`eve.material/13` is the sole migration input. Migration validates that no
unversioned `vegetationGradient` member exists and changes only
`schemaVersion`; it preserves v13 emission and never invents a gradient for a
legacy asset. Validation and runtime loading reject malformed objects before
publishing a material.

`vertexOcclusionColor` is a finite, non-negative linear HDR RGB triple inside
`vegetationSurface`. The fragment stage clamps mesh green to
`[0.0001,0.9999]`, applies the same epsilon remap endpoints used by the Colors
and Overlay masks, and interpolates from the authored occlusion color to white.
It multiplies the base color after Gradient and before global Colors. The
existing occlusion alpha continues to control Colors and Overlay, including
their independent invert switches.

The same `vegetationSurface` object stores `backfaceNormalMode` as the TVE
`Flip` (0), `Mirror` (1), or `Same` (2) enum. Back-facing fragments multiply
their final tangent normal by `(-1,-1,-1)`, `(-1,-1,1)`, or `(1,1,1)` before
the tangent-to-world transform. Overlay projection is evaluated before this
facing branch, matching the generated shader. `_RenderSpecular` maps to the
canonical `specularFactor`; zero therefore removes the dielectric specular
lobe without changing metallic or roughness data.

The top-level `cullMode` member stores TVE `_RenderCull` exactly as `none`,
`back`, or `front`. It must agree with the compatibility `doubleSided` member:
only `none` is double-sided. Vulkan PBR pipeline selection uses all three
states, including TVE's front-face culling for leaf and grass cards. Missing
`cullMode` remains compatible with older canonical producers by deriving
`none` or `back` from `doubleSided`.

RenderSystem3D resolves that state once per collected material draw and carries
it into the forward, cascaded-shadow, and GBuffer passes. Vulkan and WebGPU
shadow and GBuffer pipelines provide the same three variants for opaque and
alpha-cutout draws; Vulkan also applies them to the skinned variants. Legacy
and explicit extra-pass draws retain the previous no-cull default. A Vulkan
attachment-readback test draws all three GBuffer variants side-by-side and
confirms that `front` removes the front-facing triangle while `none` and `back`
retain it.
