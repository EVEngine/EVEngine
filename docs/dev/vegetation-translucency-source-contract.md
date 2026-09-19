# TVE translucency integration contract

Source: the supplied TVE 12.6.0 package, extracted locally under
`build/tve-reference/nested/Universal 6000.0+`. This records inspected source
semantics and outstanding implementation requirements, not completed support.

## Required lighting operation

`Prop Subsurface Lit.shader`, lines 1522-1535, evaluates each light with:

1. Light RGB times distance attenuation, multiplied by
   `lerp(1, shadowAttenuation, _TransShadow)`.
2. Distorted direction `lightDirection + worldNormal * _TransNormal`.
   This intermediate is not normalized.
3. View lobe `pow(saturate(dot(viewDirection, -distortedDirection)),
   _TransScattering)`.
4. Direct and baked-GI terms combined as
   `viewLobe * _TransDirect + bakedGI * _TransAmbient`.
5. Multiply the result by translucency RGB, final BaseColor RGB,
   `_TransStrength`, and the attenuated light RGB; add to the lit result.

The ambient term occurs within the light contribution. Moving it to an
unconditional global ambient add changes behavior, including a scene without
active lights. Shadow influence must remain independent of ordinary diffuse
shadowing. Source defaults and material values must be read separately: the
hidden `_Trans*` controls are populated by the package's material utilities.

## Surface stages that must remain distinct

`Plant Subsurface Lit.shader`, lines 1768-1793, forms translucency RGB from:

- `_SubsurfaceColor.rgb` times wetness-stage albedo;
- `_SubsurfaceValue` times global `TVE_SubsurfaceValue`;
- overlay influence `lerp(1, TVE_OverlaySubsurface, Overlay_Value)`;
- mask influence `lerp(1, Blend_Mask_Remap, _SubsurfaceMaskValue)`;
- source multiplier 10.

Final BaseColor uses the variable named rim-light-stage albedo (lines 1626-1631),
but this particular shader variant performs no rim-light calculation there:
the value aliases wetness albedo multiplied by
`1 + _MotionHighlightColor.rgb * IN.ase_texcoord9.z`.
The vertex stage writes its motion highlight value to that interpolator at
lines 1147-1148. Therefore the lighting multiplication uses both wetness-stage
and final motion-highlighted albedo. Replacing wetness-stage albedo with final
albedo would apply the motion highlight twice. Other shader families require
their own source trace; the variable name alone does not establish a rim effect.
The Prop family must retain its own mask contract rather than inherit Plant's
mask merely because both families expose similarly named properties.

## Native integration obligations

Current `PbrSurface` and `pbr_surface.frag` do not implement these controls. The
fragment shader has final base color and direct-light visibility, but no separate
wetness-stage albedo or baked-GI source equivalent. Implementation must introduce
explicit stage values and document any backend-specific GI mapping. A scalar
transmission approximation is insufficient evidence of TVE fidelity.

Material values belong to the owning PBR surface snapshot; global and sampled
field values belong to the vegetation field/render adapter. Import must preserve
their distinction and fail transactionally on malformed values. Any persistent
material change requires the repository's schema/migration policy, and all GPU
uniform producers and consumers must change together.

Verification must cover distorted-direction non-normalization, independent
direct/ambient/shadow controls, zero-light behavior, Plant mask interpolation,
distinct albedo stages, imported source material values, and actual rendered
front/back lighting. Cross-backend evidence must identify the backend actually
executed. These obligations remain open.

## Initial native rendering integration in progress

PbrSurface now owns a PbrTranslucency value with linear color, intensity,
strength, normal distortion, scattering exponent, direct/ambient/shadow weights,
mask influence and global/overlay multipliers. Validation rejects malformed
factors and requires a linear ORM image when an active mask is requested.
Vulkan's CPU uniform and both shader declarations changed together to 1024
bytes. The fragment contribution follows the inspected per-light formula and
preserves the unnormalized distorted direction. Shader compilation succeeds.

The present surface pipeline has no wetness or rim-light stage yet, so the two
albedo stage values currently coincide. Native ambient RGB supplies the GI term;
this is an explicit native mapping, not baked-lightmap parity. The other backend
continues to return Unsupported for extended PBR through its existing interface.
Material persistence/import, full stage composition, backend parity and shadow
mask rendering proof remain outstanding. A build is recompiling the transitive
PbrSurface users before executing the new actual GPU test; compilation alone is
not reported as rendering evidence.

The visible Plant controls have defaults different from the hidden template:
power 2, angle 8, direct 1, normal distortion 0, ambient 0.2 and shadow 1.
TVEUtils.cs lines 1288-1293 maps these visible `_Subsurface*Value` properties to
the hidden `_Trans*` controls. The importer must use the visible source values
and defaults. New persisted semantics require a material schema version that
old readers reject; additive unknown fields in material/3 would otherwise be
silently ignored and are insufficient.

The initial rendering tests now execute and pass after CMake regeneration
included the new test file. One case validates malformed factors and active
mask requirements; the other renders through Vulkan to an engine-owned canvas,
compares the RGB increment against an independent backlight/ambient reference,
and verifies that a zero-radiance light adds no ambient translucency. The
distortion case would differ substantially if the intermediate light direction
were normalized. Evidence: build/vegetation-translucency-tests.log and
build/vegetation-translucency-validation.log. This proves the initial direct/GI
formula, not shadow-map, mask, source-material import or complete surface-stage
parity. The structure owns only values, existing checked submission remains the
publication boundary, and no architecture exception was taken.

The related asset/PBR/vegetation regression selection also passes all 177 cases
with Vulkan validation enabled. Module layering and whitespace checks pass.
The public-structure architecture contract check and fixtures are running for
this change; their completion is not yet claimed. Persistent material migration,
trimmed-profile coverage and the remaining rendering contracts are still open.

The GPU case now additionally verifies an ORM-alpha translucency mask while the
independent color-mask feature stays disabled, isolated direct and ambient terms,
and multiplicative global/overlay attenuation. Fixed numerical references are
compared against actual canvas readback. Both focused tests pass again under
Vulkan validation (build/vegetation-translucency-mask-tests.log). Shadow-map
influence, complete surface stages and imported material composition remain
unverified. The architecture main check has passed; fixtures remain in progress.

Translucency now owns its own maskMinimum/maskMaximum range. It no longer
borrows colorMaskMin/Max, so the future persisted translucency object can express
its own contract without duplicate/conflicting color-mask state. Both shader
uniform declarations and the Vulkan producer now agree on 1040 bytes. The GPU
test intentionally leaves the color-mask range at its different default while
checking the independently configured translucency mask. It passes after all
142 transitive PbrSurface translation units were invalidated and rebuilt;
the related 177-case regression also passes. This preserves the checked owning
snapshot boundary. Material/4 parsing, migration and import have not yet changed.

Material/4 integration now exists; its contract is documented in
canonical-material-v4.md. The reader accepts v4/v3, rejects unversioned
translucency in v3, validates the nested object, and defers resolved texture
checks until upload. Migration v3-to-v4 preserves identity/unknown root fields
and updates dependencies. Standard Unity/glTF producers emit v3; TVE emits v4
and maps admitted Subsurface shader controls, including independent Plant mask
range and mask-free Prop behavior. Runtime global/overlay values are not stored
as material facts.

New tests verify v4 owned parameters, malformed/unversioned input, required mask
identity, and Unity Subsurface material import through EVA/cook/evpack decoding.
All 179 related tests pass with Vulkan validation. Two initially stale Standard
Unity v2 assertions (asset version and typed dependency) were updated to v3.
Layering and whitespace checks pass. The new original-material import and the
material/4 architecture gate are running; original-source v4 rendering, trimmed
profiles and the remaining stage/backend contracts remain open.

The GPU test now includes a Unity-to-pixel composition path. A small in-tree
Subsurface material is imported, archived, cooked to material/4, parsed by the
runtime reader and supplied with images uploaded through EvpackImageLoader.
Rendering uses its imported PBR surface and gamma-to-linear tint on the synthetic
quad, then compares against the independently configured lighting reference.
Both focused tests pass under Vulkan validation; uploaded image resources are
explicitly released after readback. Evidence:
build/vegetation-translucency-composition-tests.log. This verifies material and
image wiring with a deterministic fixture, not original mesh geometry or all
alpha/shadow/stage combinations.

The original-source validator is prepared as
build/tve-reference/validate_original_translucency.py. It compares all material
fields against original Unity source files, checks the Plant/Prop mask distinction,
requires v4, verifies unchanged preceding material fields, and compares image
definitions and payloads against the prior original import in bounded chunks.
Original material import remains running; no result from this validator is
claimed yet.

The original material import has now completed, and the validator passes: 32
material definitions, including 15 Subsurface materials, match original visible
source controls and the shader-family mask rule. All 172 image assets and their
payloads remain unchanged from the preceding authored-normal import. Results:
build/tve-reference/original-translucency-validation.json. The full v4 runtime
pack cook has started; its completion and original-material GPU rendering remain
pending.

The material-v4 architecture contract gate and all nine fixtures have completed
successfully (635.756 seconds for fixtures), recorded in
build/vegetation-material4-architecture.log and
build/vegetation-material4-architecture-fixtures.log. No architecture exception
was introduced. The runtime reader documentation now names its actual 3/4
compatibility window.

An original-material GPU probe is being built as
eve_unity_material_graphics_probe. It loads a caller-selected material and its
images from the pack, preserves source surface/alpha parameters, and compares a
textured quad with translucency enabled and disabled. A positive pixel delta
proves that source material wiring reaches rendering; it does not establish
original geometry, shadow, or complete visual parity. Execution is pending the
pack cook and probe build. The independent cooked-definition comparator is
build/tve-reference/validate_runtime_translucency.py; only its Python syntax has
been checked so far.

The original-material probe, including optional side-by-side PPM readback
(disabled translucency on the left, source translucency on the right), now builds
successfully. Test discovery remains valid for 617 sources. The cook process is
still live; original-material execution remains pending. Reinspection of the
original Plant shader also corrected the stage description above: motion
highlight is the actual post-wetness color operation in this variant.

Original Sword Fern material rendering now passes on Vulkan. A diagnostic archive
retains its material, three image assets, and all seven definition/payload files
byte-for-byte from the full source import. Its dependency closure and import
report mappings are reduced with a recomputed report key; the original complete
archive and its ongoing cook are untouched. The four texture uploads include
separate ORM/AO bindings. With original alpha cutoff and surface parameters on a
test quad, 584 sampled pixels increase by more than 0.005 when translucency is
enabled; maximum RGB channel increase is 0.278431. The engine-owned side-by-side
readback was visually inspected: clipped fern contours are present, with a
visible transmitted-light increase. This is original material/texture evidence,
not original mesh or scene parity. Evidence:
build/tve-fern-translucency-gpu.log and
build/tve-fern-translucency-comparison.png. The probe exits successfully; logged
validation messages are unused vertex-attribute performance warnings.

The complete original material-v4 cook finished: 376 chunks, 735507260 bytes,
build id b1c22cd1-7a28-5a42-ad80-e1a9e93db4b9. The independent validator now
handles the material chunks' Zstd compression using Python's standard codec,
verifies decoded length and SHA-256, decodes EVDEF and compares every material
definition to the source archive. All 32 materials, including 15 Subsurface
definitions, compare equal. Evidence:
build/tve-reference/runtime-translucency-validation.json. This validates cooked
material data; full original scene/geometry rendering remains outstanding.

The original Sword Fern probe also passes when reading the complete 376-chunk
pack: four uploaded texture bindings, 584 changed sampled pixels, maximum
translucency delta 0.278431. This matches the reduced diagnostic archive result.
Evidence: build/tve-fern-full-pack-gpu.log. The full-pack probe binary predates
the motion-highlight changes and is evidence for the material-v4 translucency
path only; highlight GPU evidence comes from the rebuilt unit tests.
