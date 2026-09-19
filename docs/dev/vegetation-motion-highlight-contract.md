# Plant motion highlight and surface stage contract

This is a source trace and integration gap record, not completed functionality.
Source is the supplied TVE 12.6 Universal 6000.0+ Plant Subsurface Lit shader.

At lines 1145-1148, the vertex stage writes the following scalar to its fragment
interpolator:

`abs(complexNoise.a) * windPower * motionFade * vertexColor.a`

Complex noise is the two-phase advected sample blend at line 884. The source wind
power at lines 916-919 is `1 - (1 - mixedWindZ)^2`, after the editor override mix.
Motion fade at lines 1043-1045 is the saturated remap of world-space camera
distance from `TVE_MotionFadeEnd` to `TVE_MotionFadeStart`, with the source 0.0001
denominator adjustment. Mesh height at line 1075 is vertex color alpha. These
values are evaluated before fragment interpolation; recomputing them per pixel
would change the source behavior on large triangles.

At lines 1627-1631, final base color is wetness-stage RGB multiplied by
`1 + _MotionHighlightColor.rgb * interpolatedHighlight`. The variable named
RimLight aliases this result in this variant. The translucency color at lines
1768-1775 uses the earlier wetness-stage RGB, so highlight must affect only the
final base-color factor of the per-light translucency multiplication.

Current native gaps established by code inspection:

- `VegetationMotion.cpp::deformLocal` computes the advected RGBA noise, nonlinear
  wind power, and flutter fade but emits position only. `VegetationGeometry`
  carries geometry/frame streams, with no highlight output. A highlight stream
  must be evaluated at the authoritative rest vertex, not at the finite-difference
  samples used to reconstruct the deformation Jacobian.
- `updateVegetationMesh` currently uploads positions/normals/UV and adopts tangent
  frames; it does not carry a highlight stream. Its update failure contract must
  remain transactional when a new stream is introduced.
- `applyVegetationSurface` currently darkens by a scalar before overlay mixing.
  This differs from the inspected shader, which mixes overlay first and then
  interpolates that RGB toward its square for wetness. Applying that operation
  only to a CPU tint would also miss the per-fragment textured albedo.
- `PbrSurface` translucency currently sees identical wetness and final base color.
  The GPU stage boundary must preserve separate values when highlight is wired.

Required evidence includes source-matching vertex values at near/far distances,
zero wind and nonuniform authoring height; interpolation across a triangle;
actual textured wetness/overlay composition; and a pixel test proving highlight
is applied once rather than twice in transmitted lighting. The global field,
material color and per-vertex stream must keep distinct ownership. Source mesh
channel preservation, native and browser backend contracts, and persistence/API
documentation must accompany implementation rather than being inferred from the
existing material-only quad probe.

## Implementation in progress

The CPU deformation now returns an owning `motionHighlights` scalar stream with
one value per rest vertex. A private owning local result carries position and
highlight together, reusing the same noise, wind and fade computation. Normal
Jacobian evaluations read only that result's position; their perturbed samples
cannot overwrite the authoritative highlight. Nonfinite output fails the whole
existing Result transaction. The output is transient geometry data and adds no
persistent schema or new clock/global owner.

Tests now cover constant noise alpha with three authored height weights, the
nonlinear default wind power, far-distance fade and zero wind. The code compiled;
afterward eight dependent translation units were touched for the header layout
change and a rebuild/test run was started. Results and the new architecture gate
are pending. The scalar has not yet been wired through mesh upload, GPU varying,
material tint, or script projection, so rendering completion is not claimed.

The dependent rebuild and all 32 vegetation tests now pass, including the new
motion-highlight case. Evidence: build/vegetation-highlight-tests.log. The
architecture gate remains in progress; GPU integration remains outstanding.

The highlight test also passes at an intermediate camera distance with amplified
bending and repeated evaluation. It checks the fade against the authoritative
rest position, guarding against accidental reuse of deformed or Jacobian sample
positions. Evidence: build/vegetation-highlight-rest-tests.log. Inspection of
the PBR uploader confirms its existing storage buffer carries optional tangent
data; a distinct typed scalar stream and ownership contract are still required
before adding highlight interpolation. UV channels must retain their existing
texture-coordinate meaning.

Mesh now has a separate owning motion-highlight stream, with checked validation,
nonallocating ownership transfer and a read-only borrowed view. Empty clears;
otherwise count must match vertices and scalars must be finite/nonnegative.
Vegetation mesh updates prevalidate the stream before backend geometry mutation
and adopt it afterward alongside the tangent frame. The existing render-thread,
no-callback update contract applies. Tests for invalid count, infinity, negative
values and clearing have been added. All 159 Mesh-dependent translation units
were touched to invalidate stale layout users; rebuild and test execution are
in progress. The shader and GPU uploader have not yet consumed the stream.
The preceding CPU-stage architecture source check passed; its fixtures and a
fresh post-Mesh gate remain to be completed before handoff.

The Mesh-stage suite completed with 33 passing vegetation tests. Vulkan now
appends the typed highlight scalars to its per-draw storage upload, with separate
offset/presence metadata in the unused tangent-info components. The vertex
shader reads one scalar by vertex index and interpolates it at location 16;
absence supplies zero. PbrSurface owns a nonnegative linear highlight RGB value,
default zero. CPU/vertex/fragment uniform definitions now agree at 1056 bytes.
Final albedo is multiplied after the earlier translucency-albedo snapshot.
Both shaders compile successfully.

The pixel test compares highlighted and unhighlighted transmitted-light deltas:
a 0.5 scalar and unit RGB must multiply the delta by 1.5, not 2.25. All 143
PbrSurface-dependent units were touched and the GPU-stage rebuild/test run is
pending. The preceding 33-test result does not establish this new shader path.
Material import/persistence and backend parity remain outstanding.

The GPU-stage rebuild completed and all 33 vegetation tests passed, including
the 1.5-versus-2.25 transmitted-light pixel test. Evidence:
build/vegetation-highlight-gpu-tests.log. Additional cases for HDR color
validation, absent streams and nonuniform vertex interpolation were added after
that compilation and are running separately; their result is pending.

The additional interpolation, absent-stream and HDR validation cases now pass
under Vulkan validation (two focused cases, 3.40 seconds), recorded in
build/vegetation-highlight-interpolation-tests.log. The asset creation path was
then found to attach only tangent data; it now also adopts highlights and rolls
back the new mesh on failure. The existing composition test compares both newly
created and updated mesh highlight streams against independent asset evaluation.
That creation-path rebuild/test is pending.

The creation/update composition test now passes under Vulkan validation
(5.68 seconds), including exact highlight-stream comparisons after both paths.
Evidence: build/vegetation-highlight-create-tests.log.
