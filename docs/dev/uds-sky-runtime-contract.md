# Independent UDS-style sky runtime

The new sky path is owned independently of `DayNight`. The legacy module is not
its clock or weather authority. `daynight/sky/SkyTimeline` currently implements
the simulation state. `graphics/sky/SkyAtmospherePass` prepares an independent
HDR atmospheric draw and registers with existing main-view and reflection
contributors. `daynight/sky/SkyRuntime` connects the single simulation clock to
solar direction and atmospheric light intensity. `SkyProfile` validates authored
atmospheric settings. Complete UDS rendering remains in progress; this document does not claim
visual parity.

## Simulation contract

One caller-owned `SkyTimeline` owns its clock, active weather transition and cloud
travel. `SkyFrame` is a copied projection for rendering and reflection captures;
consumers must not independently advance it. A paused clock (zero hours/second)
does not pause wind or weather. The caller pauses all simulation by supplying zero
duration. No legacy `DayNight` state is read or mutated.

Time enters through `eve::Duration`. Accumulated time uses checked integer
nanoseconds; negative steps and overflow fail without mutation. Weather ramps
are evaluated from their start time, and wind is analytically integrated over
the ramp plus any constant-speed tail. Changing targets mid-transition samples
the current weather and travel before replacing the transition. Zero-duration
changes preserve cloud position. Equal commands at equal simulation times are
frame-subdivision invariant to floating-point evaluation tolerance, not a claim
of bitwise GPU or cross-platform equality. Ordinary-duration CPU tests use 1e-9.

Coverage retains UDS's 0–10 domain. Fog, rain and snow are normalized 0–1 controls;
their conversion into renderer coefficients is not implemented here. Wind is
an X/Z vector in metres per second, avoiding angular wrap during transitions.
No random samples are taken by this layer; later stochastic consumers must use
explicitly supplied named streams.

## Architecture boundary

- R-FACE-1/2: capability input/output: none; ECS/Link: none; events: none;
  script/tool protocol: the existing daynight module exposes the owned runtime
  binding described below. C++ callers create a validated value,
  advance it and copy its evaluated frame. `SkyRuntime` additionally accepts a
  graphics provider and immutable atmosphere settings at preparation.
- R-BOUND-1/2: this implementation calls common `Duration` and `Result`; it adds
  no cross-module dependency to daynight's existing manifest. The runtime's
  implementation uses the already-declared graphics dependency through forward
  declarations and Pimpl; its public header includes no graphics header. No L2/L3/L4 trim
  claim is made from source inspection.
- R-MECH-1: one simulation owner and frequent frame reads use direct typed calls
  and copied values. There is no additional registry or per-frame string lookup.
- Ownership/lifetime: the timeline retains only values, on one simulation thread;
  it has no borrowed provider, callback, lock or destruction-order dependency.
  `SkyRuntime` owns a prepared graphics pass and checks the existing weak provider
  token before mutations; its pass handles both provider-first and owner-first retirement.
- Persistent schema/restore: runtime values are not saves. `SkyProfile` has the
  strict versioned authored format described below; live-state restore is not implemented.
- This is not an ECS System: there are no views, component writes, structural
  changes or ECS events.

`test/SkyTimeline.cpp` exercises transition interruption, immediate transitions,
clock wrapping/pause, state independence, invalid-command atomicity and a frame
crossing the transition endpoint versus 150 smaller frames. These tests prove
simulation behavior only; they do not prove a complete sky renderer.

`SkySolarOrbit` evaluates the reference's manual, non-astronomical solar path
from an explicit hour and immutable pitch/yaw. It owns no second clock: callers
pass `SkyTimeline::frame().hour`. The result points towards the sun, with Unreal
Z-up coordinates mapped to EVEngine Y-up. Thirteen measured UDS 5.8 samples from
UE 5.8.2 cover eight daily times, pitch 0/30/60, yaw 0/90 and a pre-dawn sample;
`test/SkySolarOrbit.cpp` compares directions within 1e-6 and unit length within
1e-12. This establishes the manual orbit only. Vertical offset, astronomical
sun/moon simulation and twilight intensity curves remain separate work; notably
the reference still emits almost full solar intensity at 05:30, so horizon
clamping must not substitute for its actual intensity curve.
Inspection of `Current Sun Light Intensity` confirms the atmosphere-mode branch
maps the cached light-forward Z from 0.157 to 0.113 onto zero to base intensity,
with clamping. The separate simplified-color branch samples
`Directional_Light_Intensity`; importing that curve into the atmosphere branch
would be incorrect. Eclipse, directional balance, dimming, inscattering and
interior multipliers are subsequent operations and are not yet implemented.

## Runtime composition

`SkyRuntime::prepare` validates the clock, weather, solar orbit and irradiance
before preparing GPU resources. The returned object is detached; `attach` uses
the existing pass's atomic two-contributor registration. `advance(Duration)`
evaluates a candidate timeline and solar light, publishes the light, then commits
the value-only timeline. Invalid time or provider retirement leaves simulation
and lighting unchanged. Rendering never advances time. The atmosphere-mode
solar intensity map described above is implemented; simplified-color and space
lighting are not selected implicitly. Optical tables remain prepared once.

`SkyRuntimeSettings` is a runtime value, not a persistent schema. Weather
commands feed the sole timeline, but currently do not change optical coefficients
or produce clouds/precipitation. The runtime does not own or modify a scene
directional light yet. `test/SkyRuntime.cpp` composes the real Vulkan atmospheric
capture with simulation, checks the measured 05:30 intensity, noon-to-midnight
progression, repeated captures without clock advancement, unregistering, and
provider-first failure atomicity. These are composition tests, not final UDS
image parity or full six-face reflection proof.

## Authored profile contract

`SkyProfile::decode(Value)` accepts schema `eve.sky-profile`, integer version 1 or 2,
and mode `atmosphere`. Every field is required. Every object rejects unknown
fields; vectors require exact dimensions. Numbers must be finite and representable
in their destination type, then satisfy the existing timeline, orbit and optical
validation contracts. Unsupported versions and modes fail explicitly; there is
an explicit version-1 migration with height fog disabled; no substitution with another rendering mode. A decoded
profile owns immutable copied values, independent of the input Value's lifetime.

Version 2 adds height fog in metre units to initial clock/weather/sun and optical configuration, not a
live-state snapshot, cloud preset or resource manifest. Distances in atmosphere
coefficients are kilometres; wind is metres/second in X/Z; hours use [0,24).
`sun.irradiance` is linear RGB. Decode performs no provider calls. A caller can
prepare a replacement `SkyRuntime` from `profile.settings()` and
`profile.atmosphere()` before replacing the active owner. Decode failure cannot
mutate that owner. Atomic hot swapping of attached runtimes is not implemented.

`examples/uds-sky/sky.json` is the explicit initial
configuration extracted from the resolved reference atmosphere, with time paused
at noon. It includes the resolved clear-noon height-fog coefficients but not UDS's additional wisps. The profile
test loads this actual file and exercises rejection and owned-data lifetime; the
runtime GPU composition test also prepares its atmosphere from the decoded file.

## Atmospheric pass

`SkyAtmospherePass::prepare` validates copied optical parameters, builds a GPU
program and immutable coefficient buffer, and returns an owning pass. `attach`
publishes its two contributor tokens only after preparation; failure removes
partial registrations. `setLight` atomically updates a copied, normalized light
direction and RGB energy. Draw uses a fixed push-constant block without string
lookups, reconstructs camera rays and writes only far-depth background pixels.
No DayNight time, lights, environment maps or fog settings are changed.

The current shader integrates Rayleigh and Mie scattering, Mie absorption
and a triangular ozone layer in a spherical atmosphere. It uses 64 view samples
and samples prepared transmission and multiple-scattering tables. These fixed
view counts are not a final performance setting. Sun/moon disks, stars, cloud layers,
fog composition, tone mapping parity and scene-scale validation remain required.
Optical coefficient defaults come from the resolved UDS clear-noon component.
Direct component queries also confirm the ozone layer's 25 km center, 15 km
half-width, and sRGB ground albedo 170/255, converted to linear 0.40197778.
Their presence in this pass is not a pixel-equivalence claim.

`SkyAtmosphereLuts` prepares a 256x64 transmittance table with ten ray samples and
a 32x32 multiple-scattering table with fifteen samples along each of the upward
and downward directions. These dimensions/counts and the two-direction mode were
queried from the loaded reference project's console variables. Medium samples
use the UE default 0.3 segment offset. The latter table includes ground bounce
and the five-term finite scattering series, independent of the current sun
direction and irradiance. Rayleigh uses its normalized dipole phase and Mie uses
Henyey–Greenstein. The CPU baker uses doubles for spherical geometry and publishes
RGBA32F tables; the shader samples them linearly. This is not a claim of bitwise
identity with UE's shader arithmetic or texture formats. The high-quality
64-direction multiple-scattering mode remains unimplemented.

Preparation is the expensive boundary: validation and baking complete before
the existing resource transaction uploads both tables and the coefficient
buffer. No table is rebuilt during draw. CPU tests cover vacuum transmission,
nonnegative finite energy and independent multiple-scattering suppression. The
GPU capture test additionally verifies that enabling multiple scattering raises
RGB radiance while preserving linear response to light energy.

Graphics owns mesh and shader GPU resources. The pass keeps the existing weak
resource-lifetime token and checks it before access; provider-first destruction
therefore never dereferences stale facade pointers. Pass-first destruction
unregisters both contributors, releases GPU resources, and deletes transferred
CPU facades. There are no locks, callbacks into scripts or new registries.
The pass is graphics-thread-affine and may not be attached, detached or destroyed
from contributor dispatch.

`test/SkyAtmospherePass.cpp` exercises the actual reflection-capture draw,
irradiance scaling, invalid-command atomicity, pass removal and provider-first
retirement. The `uds-sky` example additionally verifies the main-window path;
full six-face filtered reflection probes still need visual checks. Vulkan is implemented; other resource-program
providers return Unsupported. Provider-absent branches are not evidence of a
completed alternate-backend build.

Shader bytecode is rebuilt with `python scripts/compile_graphics_sky_shaders.py`.
Setting `EVENGINE_SKY_ARTIFACT` to a writable `.pfm` path emits linear HDR pixels
from the capture test for inspection; it does not apply exposure fitting.
`EVENGINE_SKY_BENCHMARK=1` selects the 1280x720 reference camera and records five
GPU offscreen-pass times in a sibling `.timing.json`. These exclude CPU readback
and do not represent clouds, weather or a full game frame. The current local
Debug preparation measured approximately 2.04 seconds once; warmed atmospheric
captures measured approximately 0.784 ms with validation disabled. These are
single-host measurements, not a cross-device performance guarantee.

## Script and main-view evidence

`daynight.prepareSky(gfx, profileJson)` parses at most 64 KiB through common
`Value::fromJson` and `SkyProfile::decode`, prepares detached resources, then
transfers the runtime through `makeOwnedSquirrelInstance`. Its Result payload
declares `ownership = owned`; garbage collection destroys the native owner.
`attach`, `detach`, `advanceSeconds` and `frame` use the canonical Result tables.
The seconds binding uses the current VM's native float input, then validates its
conversion to integer-nanosecond `Duration`. It does not introduce another clock.
Calling the standalone SkyRuntime constructor fails with a factory instruction.

`examples/uds-sky` loads the canonical `sky.json` through the normal filesystem
and game lifecycle. It uses only the daynight module plus engine boot services,
and never calls legacy `DayNight::init/update`. The example passed an eight-second
smoke run. The initial engine MCP `eve_screenshot` captured the actual 1280x720 main view,
after the documented first-call readback retry. That atmosphere-only capture shows a smooth
sky and black below-horizon region; it predates the background fog stage below.
The earlier clear-noon v2 comparison is invalid: Unreal's positional
Rotator constructor produced 15-degree roll instead of the intended pitch.
The capture worker now uses named arguments and the version-2 result contract
requires actual camera read-back verification. The corrected clear-noon v3
comparison fails with SSIM 0.7770391 and within-3/255 pixel fraction zero. This
is a baseline for subsequent work, not a perceptual similarity percentage or a pass.

## Corrected reference and layer isolation

The local `calibration-v3-correct-pose` set completed all five scenarios with
actual camera basis verification and repeated captures. Separate disposable
clear-noon runs disabled HeightFog, then additionally disabled the sky sphere
and volumetric cloud components. Their camera remained at 15 degrees pitch,
zero roll and 75 degrees horizontal field of view. Disabling HeightFog removes
the blue below-horizon fill; disabling the sky sphere also removes wisps. The
bare atmosphere still has a black ground region. This excludes recoloring the
atmosphere's ground rays as a substitute for the missing fog layer.

The native main view compared with this bare reference has SSIM 0.9585758,
within-3/255 fraction 0.1718045 and mean absolute channel error 22.2993/255.
It still fails the image gate, and the black region contributes many matching
pixels. This isolated result is not comparable to the complete-image score.
The explicit above-horizon region `[0,0,1280,580]` has zero pixels within
3/255 and mean absolute channel error 27.0725/255 despite SSIM 0.9659965;
this confirms that smooth gradients can inflate SSIM while color remains wrong.
The native presentation uses the existing fitted ACES curve; it is not yet the
same display transform as the reference. Do not fit irradiance to hide that
separate difference.

Resolved clear-noon HeightFog has density 0.00551000005, height falloff 0.06,
height -1.5 metres, start distance 73.4803369 metres, maximum opacity 1 and no
second density layer. Volumetric fog is disabled and
`r.SupportExpFogMatchesVolumetricFog` is zero. Its base inscattering is black;
the atmosphere ambient contribution uses the distant-sky integral at six
kilometres, multiplied by the atmosphere's 2.3 height-fog contribution. Thus
the reference's visible fog color cannot be taken from the black base-color
property alone.

`detail::bakeDistantSkyAmbient` now supplies the CPU preparation primitive for
that ambient term: 64 locally seeded stratified sphere directions, ten samples
per ray, isotropic scattering, existing transmittance/multiple-scattering
tables, and no ground-surface bounce. It returns linear RGB per unit incident
irradiance and is not called by frame update or drawing. It requires matching
immutable optical tables and an atmosphere extending above six kilometres.
The quadrature follows the installed reference shader and renderer source;
double-precision CPU evaluation is not a bitwise GPU equivalence claim. The GPU evaluates the same quadrature at each of the three full-screen vertices,
then passes flat ambient radiance to analytic background fog. This avoids per-pixel
quadrature and CPU work during frame updates, but a future cached compute pass
can reduce repeated work across main and reflection views.

## Analytic background fog stage

Version 2 profiles now supply `heightFog`; version 1 migrates to zero density.
Coefficients are stored in the prepared atmosphere's immutable uniform buffer.
The GPU computes distant-sky ambient radiance at the three full-screen vertices
and passes it flat to the fragment stage. Changing the existing solar snapshot
therefore updates fog lighting without CPU quadrature, allocations or registry
queries during frame updates. Directional scattering uses the same light and
ground transmittance, with Rec.709 luminance weights (the resolved reference has
legacy luminance factors disabled).

The fragment shader evaluates the exponential height integral, start distance,
maximum opacity and directional lobe. It uses a finite authored sky distance,
100 km in the example; the reference sky sphere is much larger. This is a
background-sky layer, not yet depth-based fog on opaque scene geometry, a
second height layer, fog volumes or volumetric lighting. Those remain required
for the complete system. Reflection contributors draw the same shader.

The Vulkan test compares an opaque downward fog ray with independently baked
CPU ambient radiance (2% relative tolerance), and verifies zero incident energy
produces no residual light. All 19 sky-related tests passed with validation
enabled; the fog test was repeated after the final luminance-weight correction.
A fresh engine MCP main-window capture at 1280x720 shows the fog filling the
former black region, with no Vulkan validation errors in the run log. Against
the complete corrected clear-noon reference, SSIM is 0.9583234, RGB MAE is
32.6854/255 and the within-3/255 pixel fraction remains zero. This still fails
the parity gate; wisps and display-transform parity are absent. The earlier
2.04 seconds / 0.784 ms preparation/draw measurements concern atmosphere without fog
and must not be reused as fog-stage performance evidence.

The fog capture test accepts `EVENGINE_SKY_FOG_TIMING=<output.json>`. After its
correctness assertions it selects the 1280x720 reference camera and records five
warmed offscreen GPU passes. With validation disabled, the local Debug build
measured 1.565–1.665 ms (mean 1.609 ms). This includes atmosphere and background
fog drawing, excludes CPU readback and display tone mapping, and is not a
cloud/weather/full-scene budget. GPU identity was not recorded in this initial
timing receipt, so this is limited single-host evidence. The updated example
also passed the repository's eight-second smoke test using an independently
named copy of the same executable to avoid terminating unrelated Windows runs.

## Filmic presentation stage

The reference read-back selects `TonemappingMethod.FILMIC`, with
`r.LUT.TonemappingMethod=-1`; the separate ACES version cvar is 2 but does not
select the standard ACES path for this view. Resolved film slope/toe/shoulder/
black clip/white clip are 0.88/0.55/0.26/0/0.04, blue correction 0.6 and gamut
expansion 1. White balance is neutral at 6500 K and grading vectors are neutral.

Graphics now exposes the explicit `Filmic` / `"filmic"` presentation mode. It
uses fixed Rec.709-to-AP1 conversion, gamut expansion, blue correction, the
anchored film curve and output conversion before the existing display encoding.
The default remains the original fitted `Aces` mode. The generic backend rejects
unsupported changes atomically; Vulkan implements the new mode. This is an
existing graphics-owned presentation value, not a second sky-owned authority.
The example selects it explicitly. HDR offscreen sky and reflection draws do
not pass through this final display transform.

The initial main-window filmic capture reduces clear-noon RGB MAE from 32.6854
to 12.8538/255 and raises SSIM from 0.9583234 to 0.9869596. Only 2.73524% of
pixels satisfy the 3/255 tolerance; the parity gate still fails. The reference's
default vignette (intensity 0.4), bloom, LUT interpolation and missing sky wisps
are not reproduced by merely selecting the curve. This mode is therefore not a
claim of complete reference post-processing or 99% sky similarity.

The filmic GPU test renders actual HDR input and output canvases and verifies
the 0.18 neutral anchor (absolute tolerance 0.002), neutral channels, black,
finite highlights, and distinct legacy/linear output. The fixture explicitly
draws its HDR source before sampling. It passes with Vulkan validation and no VUID errors. Existing
sky tests and script mode selection/rejection also passed. The implementation
does not introduce a new lifecycle, ECS owner or optional module dependency;
unsupported providers retain their prior presentation mode.
Two separate engine-owned main-view captures are pixel-identical with time
paused, and the updated example passed an eight-second smoke run.

### Thin-cloud source geometry evidence

The original sky sphere has 2,370 position control points and 4,736 triangles.
`Cloud_Wisps` samples UV channel 3, and raises painted vertex red to power 5
for its morph displacement. The two wrapped texture samples use complementary
coordinates, offset fractional phases, and a sinusoidal crossfade. The actual
actor binds `Textures/Sky/Cloud_Wisps`, not the material function's placeholder
`ParticleClouds` texture. Its color is vertex-interpolated and multiplied by
`Scale_Intensity_Around_Sun`; that function reads customized UV channel 2,
containing the sun and moon centered gradients. These attributes cannot be
replaced by a generic sphere UV or random noise without moving visible clouds.

`tools/sky-reference/sky_mesh.py` preserves all 14,208 polygon corners, including
UV seams and painted colors. The local ASCII export was decoded and 15 corners
were independently checked against Unreal's StaticMeshDescription position and
UV3 APIs: position error below 1e-5 cm and UV error below 1e-6. Seam-preservation,
axis/UV conversion, malformed counts, nonfinite values, bad indices and
unsupported-layout tests pass. The preceding filmic source-quality gate passed
187 tests. This extraction work does not yet render wisps; the reported image
comparison above remains the current result.

The desktop material arithmetic now lives in `sky_wisps.glsl`: original wrapped
dual samples, morph phases, sinusoidal crossfade, painted-red displacement,
sun-centered gradient with its vertical remapping, gated moon gradient,
vertex cloud highlight and interpolated celestial intensity scaling. Source
light directions are explicitly UE Z-up forward vectors; they must not be fed
the atmosphere pass's native towards-light vectors without conversion. Cloud
time is a caller-provided value, not a shader wall clock. Lighting must be
evaluated on original dome vertices before raster interpolation.

`SkyWisps.cpp` executes those functions on Vulkan and reads HDR output. It checks
opacity at phases 0, 0.25, 0.5 and 1, daytime moon suppression, explicit moon
disable, noon/night gradient endpoints and HDR highlights. The source is a
separately verified black/white ordinary canvas; using solid rectangles on an
HDR input canvas did not produce that fixture pattern. The corrected test
passed with validation enabled. Regenerate its bytecode together with the sky
programs using `scripts/compile_graphics_sky_shaders.py`.

This is a GPU material contract test, not a rendered thin-cloud completion
claim. Original dome geometry, texture resources, fog compositing, runtime
ownership and main/reflection integration remain to be connected. No public
API, module boundary, ECS owner or persistent format changed in this shader
step; no architecture exception was taken.

### Authored thin-cloud draw integration

`SkyAtmospherePass::prepare` now accepts an optional synchronous borrowed
`SkyWispsLayer`. Preparation validates triangle corners and finite bounded
material values, then copies mesh attributes, texture and constants into
graphics-owned allocations. Failure never registers a contributor. The pass
owns both geometry/shader handles and retires both with the same existing
provider-lifetime rules. It draws atmosphere followed by wisps inside one
main/capture callback, so contributor ordering cannot split the layers.

The vertex stream preserves source UV3 and painted red; UE local centimetres
convert to native `(x,z,y)` metres with explicit uniform dome scale. The
caller supplies the texture's actual color encoding. The fragment pass uses
`sky_height_fog.glsl`, shared with the atmosphere. Both source cloud and
background are fogged before alpha blending; cloud opacity is not multiplied
by fog transmittance. This remains background fog, not fog for opaque geometry.

The first Vulkan integration test renders an authored triangle through the
actual capture contributor. Opaque white density produces neutral HDR clouds;
zero opacity preserves the independent atmospheric baseline; invalid morph
period rejects preparation. Those tests and the existing fog/atmosphere tests
passed (four cases, validation enabled). The shader arithmetic test also remains
green. The public boundary is catalogued under the existing graphics sky
contract; no new module, upward dependency, ECS state or persistent schema was
introduced.

Remaining integration work is explicit: wire imported assets and animated
material snapshots into SkyProfile/SkyRuntime, capture the original dome and
texture together, and profile/optimize the ambient-light evaluation currently
performed on dome vertices. The ordinary example now prepares this optional
layer through the version 3 asset profile described below. Passing the synthetic geometry test does not
establish original-resource image similarity or complete weather support.

### First original-resource thin-cloud capture

The optional `EVENGINE_SKY_WISPS_ARTIFACT` branch of `SkyWisps.cpp` reads
`corners.f32` (little-endian, nine floats per corner) and a 2048x2048 `wisps.r8`
from a local evidence directory. It renders the original 14,208 corners with
height fog to a 1280x720 HDR canvas, resolves with the real Filmic shader and
sRGB encoding, and writes `wisps.ppm` plus a single offscreen GPU duration.
This is an explicit diagnostic input, not the future runtime asset schema.

The first actual image exposed a lateral-axis error missed by the symmetric
synthetic triangle. Native camera right is +Z when looking +X; UE camera right
is +Y. Correct import therefore uses `(x,z,y)`, not `(x,z,-y)`. Both geometry
and material direction conversion were corrected and the original-resource
capture passed Vulkan validation without VUID errors. The corrected image
has SSIM 0.9756689, RGB MAE 18.7211/255 and only 1.74273% of pixels within
3/255 versus the calibrated clear-noon reference. It still fails parity and
is numerically worse than the atmosphere-only image: missing layers must not
be mistaken for successful similarity.

The texture's declared sRGB flag is currently honored by expanding its source
R8 samples to RGBA8-sRGB before hardware sampling. Exact reference runtime
sampling, full parent-material contrast/composition and captured MPC lighting
values still require verification. A NullRHI map reload returned default MPC
sun/moon vectors and is not sufficient evidence for the warmed capture state.
The single validation-enabled offscreen sample was 1.4876 ms; it excludes
display conversion/readback and is not a production performance claim.

### Runtime asset preparation and warmed material evidence

`SkyWispsAsset` owns immutable decoded geometry, texture bytes and the layer
description. Its synchronous, game-thread `load` uses existing VFS snapshots;
the filesystem must already be initialized. `eve.sky-wisps/1` references a
relative `eve.sky-mesh/1` JSON triangle list and an exact-size R8 density file.
Unknown fields, unsupported versions, invalid paths, oversized inputs and
nonfinite numbers fail before publishing the candidate. GPU preparation
performs layer semantic validation and copies borrowed data; no VFS or CPU
asset handle is retained by the render callback.

`eve.sky-profile/3` requires `wispsAsset`; versions 1 and 2 retain the disabled
layer behavior. The example uses `sky-wisps.json` and the original dome's
14,208 corners plus 2048x2048 density samples. `sky.json` remains the version 2
atmosphere baseline. Asset ownership/malformed-input, profile, runtime and
script tests passed (five cases), and the actual version 3 example survived
the ten-second smoke window. These checks required normal Windows user
execution: a standalone probe proved the sandbox's user-profile API failure
also prevents PhysFS initialization; the identical probe succeeded outside
the sandbox. No engine filesystem workaround was introduced.

A warmed UE capture reproduced the calibrated clear-noon reference exactly
and recorded the actual dynamic material parameters: Overall Intensity 0.375,
Extra Contrast 0.1, Contrast Midpoint 0.85287606716, Wispy Cloud Alpha 0.2 and
Lit Intensity 0.8. CloudBTime was zero; weather MPC Sun Forward was
(0.5, approximately 0, -0.866025388), and Moon Forward was
(-0.5928401947, approximately 0, 0.8053201437). The asset uses that moon vector.
The parent contrast operation is nonlinear and follows layer composition;
it is not yet implemented. Applying it independently to alpha-blended layers
would not preserve the reference equation. Full parent material ambient fog,
contrast and composition remain required before claiming visual parity.

The version 3 example's engine-owned main-view screenshot also failed the
calibrated image gate: SSIM 0.9757019, MAE 18.7003/255 and 1.74273% within
3/255. Validation reported unused vertex-attribute performance warnings,
without Vulkan correctness errors. A second warmed material probe confirmed
Ambient Color alpha is zero in this clear-noon scenario: the parent material
ambient-fog lerp therefore preserves its input here. This does not disable
the separate exponential height-fog pass or establish fog parity in other
weather states.

### Authored parent-material composition

`eve.sky-wisps/2` adds a required three-component `contrast` value containing
extra contrast, midpoint and overall intensity. Version 1 migrates to neutral
controls and retains its unscaled physical sky. Version 2 enables the source
`Scale_Intensity_Around_Sun` operation on the physical sky before cloud
composition; it uses the same interpolated celestial gradients and
`Lit_Intensity` as the thin cloud. The source graph connects
`Base_Sky_Color * Scale_Intensity_Around_Sun` through parent Multiply_15.

The dome fragment now composes the physical sky and cloud in linear HDR,
applies `Contrast_Control`, then applies height fog. It writes opaque output
at far depth; applying contrast independently before hardware alpha blending
would produce a different result. A shared `sky_view_radiance.glsl` keeps
the physical integration identical to the atmosphere-only path. The base
fullscreen sky remains for authored geometry that does not cover a view.
This currently repeats physical integration where the dome covers it;
performance optimization must preserve the resulting radiance.

The GPU probe distinguishes post-composition contrast (.462) from separately
transformed inputs (.48525). The updated formal main-view capture achieved
SSIM 0.9905101, RGB MAE 9.80017/255 and 11.70497% of pixels within 3/255.
The complete image gate still fails. The intermediate capture without the
source sky-intensity multiplier was darker and worse (SSIM 0.9561295); it is
not the current implementation. UE vignette, remaining atmosphere/fog
differences and broader weather/mode coverage remain unverified or incomplete.

### Photographic vignette presentation

`setScenePhotographicVignette(float)` is a render-thread, Graphics-owned
setting in [0,1], default zero. Vulkan applies a cosine-fourth radial mask to
final HDR color before display mapping. Unsupported providers accept zero
and reject a nonzero value; invalid inputs leave state unchanged. No file
format, callback, ECS relationship or cross-module dependency is introduced.
Linear environment captures remain unaffected. The existing transition
vignette retains its independent formula and control.

For centered normalized screen coordinates p in [-1,1], the photographic
circle coordinate is p * (1,height/width) * sqrt(2/(1+(height/width)^2)).
Intensity scales that coordinate; the color multiplier is
1/(1+dot(coordinate,coordinate))^2. This matches the local UE 5.8
`PostProcessCommon.ush` cosine-fourth branch and its application before
tone mapping in `PostProcessTonemap.usf`. The example explicitly sets 0.4.

The photographic-vignette GPU and script tests passed on Vulkan. With the
effect enabled, the actual example's main-view capture reached SSIM
0.9946240, RGB MAE 5.71098/255, maximum channel error 10 and p99 error 9.
Only 0.00716146% of pixels were within 3/255, so the image gate still fails.
Improved structural similarity and bounded error must not be presented as
99% pixel agreement. The candidate is systematically darker than the
reference; a reference capture with Bloom disabled is being used to isolate
the missing contribution before changing atmosphere or color calibration.

The validated reference isolation with camera Bloom intensity overridden to
zero yielded 98.44618% of pixels within 3/255, RGB MAE 1.51601/255, maximum
error 4 and SSIM 0.9964713 against the same native image. This identifies
missing Bloom as the dominant remaining clear-noon difference. The isolation
is diagnostic only: the original Bloom-enabled reference remains the formal
gate, and the isolation itself still narrowly fails the 99% pixel condition.

### Photographic Gaussian Bloom pyramid

The existing Bloom component adds `GaussianPyramid`, exposed as
`setBloomFilter("gaussianPyramid", scatter, maxIterations, clamp)`. It reuses
the component's owned downsample/temporary canvases and composite path.
The original KarisTent default and GaussianScatter modes remain distinct.
The six scale sizes are 0.3, 1, 2, 10, 30 and 64 percent, with size scale 4;
neutral tint weights are 0.3465, 0.138, 0.1176, 0.066, 0.066 and 0.061,
divided by six. Camera intensity multiplies the final sum once. The source
is the local UE 5.8 Scene.cpp defaults and PostProcessBloomSetup.cpp stage
composition. Separable kernels normalize exp(-16.7*(offset/radius)^2), with
radius clamped to 31 texels; this follows PostProcessWeightedSampleSum.cpp.

The native implementation uses an initial bilinear downsample followed by
four offset bilinear samples in subsequent levels. Pyramid dimensions round
up at odd sizes. At radius 7 texels or greater, the horizontal intermediate
uses half width before the vertical pass reconstructs the original width.
Both GLSL and WGSL implement the same
stages; provider-specific runtime validation remains necessary. A zero
threshold passes the complete source; positive thresholds retain the existing
soft-knee semantics. The example selects six levels, intensity 0.675 and
zero threshold to match the reference's disabled Bloom threshold.

Per-stage uniforms are updated only after switching targets has submitted the
previous pass. Updating the next stage's additive flag before that boundary
incorrectly adds an unweighted source at the coarsest level; the new energy
fixture guards this ordering. Its HDR source explicitly submits a draw before
readback, because a clear without a submitted pass does not populate this
test path's image.

The corrected main-view Bloom capture reached 88.37185% of pixels within
3/255, RGB MAE 1.61927/255, maximum error 8 and SSIM 0.9965568 against the
original Bloom-enabled reference. The formal gate still fails. Downsample
and fast-blur differences, plus the residual already present in the no-Bloom
isolation, remain to be resolved.

The Vulkan weighted-DC/threshold fixture and settings rejection test passed.
The formal example also survived the ten-second smoke window. WGSL execution
has not yet been verified in this worktree. The UE source defaults use one
bilinear sample for the initial half-resolution image and four offset bilinear
samples for subsequent chain levels; both are now represented natively.

The Bloom quality run detected a comment falsely classified as a pointer API;
the prose was corrected without changing the checker. It also reported the
unmodified `src/engine/common/Object.h` as new relative to the advancing
`origin/dev` (759c3bb6b), which removed that header after this worktree's HEAD
(0182af046). That is separate baseline debt, not a new sky API. A diagnostic
architecture comparison against HEAD checks the uncommitted sky changes;
it does not replace or claim success for the origin/dev gate.

The HEAD-relative architecture audit passed. The odd-dimension 1281x721
Bloom fixture also exercises switching between GaussianScatter and
GaussianPyramid with equal level counts. The reference blur uses a black
border sampler for both filtered and additive inputs; duplicating edge
texels had overestimated the contribution near image corners. Pyramid blur
now performs bilinear interpolation with out-of-image texels equal to zero,
while downsample stages retain their reference clamp behavior. The edge
fixture checks attenuation and channel ratios instead of asserting the
incorrect edge-replicated DC value.

With black-border sampling, the formal clear-noon image reached 98.23828%
of pixels within 3/255, maximum error 4, RGB MAE 1.55764/255 and SSIM
0.9965573. It still fails the 99% pixel requirement. Vulkan tests passed for
odd-size reconstruction, expected border attenuation, threshold suppression
and same-count filter switching. This remaining error is close to the
no-Bloom isolation and calls for checking the reference display LUT and
physical-sky approximation rather than fitting Bloom intensity.

A diagnostic evaluation of a 32-cubed logarithmic Filmic LUT, including
10-bit quantization and trilinear interpolation, produced 96.94618% within
3/255, MAE 1.48986/255 and SSIM 0.9965485. It lowered mean error but worsened
the pixel gate. The eight-curve-evaluations-per-pixel probe was removed;
this experiment neither establishes the reference's actual LUT format nor
rules out a properly cached LUT later. Evidence remains under
`.local-debug/uds-lut-comparison-v3`. No fitted color offset was introduced.

The warm Unreal integration probe reproduced the formal clear-noon reference
byte-for-byte twice (SHA-256 d5eadcfa5149f724a1505889370b7192186b7f695d1a691f9062c281262e812e).
It resolved FastSkyLUT=1, 192x104, minimum samples=4, CVar maximum=128,
component trace scale=1 and maximum-sample distance=150 km. The renderer
caps that maximum by 32 times component scale, giving 32 effective samples.
The logarithmic display LUT is size 32 with shaper 0; format remains unprobed.

A source-matched quadratic segment distribution with 0.3 sample offset,
4-to-32 distance-dependent samples and the fractional final segment was
tested without sky-view LUT resampling. It reached only 97.02973% within
3/255, MAE 1.64964 and maximum error 5 (SSIM 0.9966429). Four atmosphere /
Filmic / provider-lifetime tests passed, but image parity regressed, so the
isolated integration change was removed. The retained renderer remains the
64-step implementation. A complete sky-view LUT implementation must include
the reference coordinate mapping, precision and filtering; changing just the
integrator is not demonstrated to improve the final image. Probe evidence:
`.local-debug/uds-reference/integration-settings-v3/run/state.json` and
`.local-debug/uds-variable-comparison-v3/comparison.json`.

The native optical camera now follows the reference minimum height of five
metres above the virtual surface; actual geometry and height-fog camera inputs
are unchanged. The below-ground capture regression verifies finite nonblack
sky. With only this correction, clear-noon measured 98.08550% within 3/255.

Sky-view parameterization was evaluated with four computed texels before
introducing storage. It includes the reference 192x104 grid, nonlinear horizon
mapping, camera-forward/radial-up frame (including the pole case), 4-to-32
quadratic integration and R11G11B10 rounding before interpolation. The direct
evaluation produced 97.04883% within 3/255, MAE 1.64087, maximum error 5 and
SSIM 0.9966534; seven regression tests passed. This is evidence of the remaining
parity gap, not a claim that the gate is met.

The implementation now generates those texels in a dedicated offscreen GPU
pass and samples the resulting Graphics-owned 192x104 RGBA16F Canvas. Packed
R11G11B10 values are exactly representable in that storage. Main and wisps
shaders share the view texture. The graphics-internal ViewPreparation phase
runs before the main forward target or an offscreen capture opens. Registrations
are graphics-thread-only, borrowed arguments expire at callback return, recursive
dispatch is rejected, and the first Result failure cancels the destination.
There are no ECS changes, persistent schema changes or additional module/service
lookups. This one-to-many ordered phase uses callbacks because providers are
registered ahead of time and consumers need synchronous readiness.

SkyAtmospherePass owns the preparation/draw registrations and cache validity;
Graphics owns the fixed-size Canvas through its existing Canvas lifetime.
Pass destruction detaches all registrations and releases shaders/meshes; provider
retirement is guarded by the existing weak token. Exact camera/light blocks
reuse the cached image; a changed view renders before publication. The existing
offscreen API synchronously drains its submission, so GPU timing and total
runtime cost still require measurement. No backend parity or trim claim is added.

The cached implementation passed nine Vulkan tests covering atmospheric capture,
provider retirement, thin-cloud composition, registration failure/removal and
preparation-before-target ordering. A ten-second `uds-sky` example smoke passed.
The cached clear-noon image measured 97.07389% within 3/255, MAE 1.64064,
maximum error 5 and SSIM 0.9966531. Compared with on-demand evaluation of the
same four LUT texels, 99.625% of pixels are identical and all remaining pixels
are within 1/255. Both are below the Unreal pixel gate; no parity claim is made.

On this host with Vulkan validation enabled, the 1280x720 atmosphere-only
fixture measured cached draw GPU times 0.03420--0.03492 ms. Five forced
light changes measured LUT generation GPU times 0.02748--0.02952 ms and
whole render-call wall times 0.7423--0.8366 ms. Initial resource preparation
was 173.046 ms. These exclude authored wisps, Bloom and general scene rendering.
Evidence: `.local-debug/uds-view-cache-full-cost.pfm.timing.json` and
`.local-debug/uds-view-cache-comparison-v3/comparison.json`.
The sorted HEAD-relative architecture audit passed. The full origin/dev
quality gate remains separately tracked; the prior run reported the unchanged
Object.h baseline mismatch and a catalogue ordering error, which was corrected
with the repository sorter before the rerun.

The sorted full `make check` rerun completed with only the two baseline
Object.h lifetime findings (lines 39 and 94). `git diff -- Object.h` remained
empty. The HEAD-relative audit passed; this does not waive the full gate.
A separate Unreal cloud-postprocess isolation disabled both UDS postprocess
components. Capture validation and repeatability passed, and the resulting
image was pixel-identical to the formal reference (maximum error zero).
The cloud-fog component was already disabled in this clear-noon scene, so
that path does not explain the current residual color error. Evidence:
`.local-debug/uds-reference/isolation-v3-cloud-postprocess/run/result.json`.

Profile version 4 introduces `cloudMotion` with phase, speed, timeScale and
windMultiplier. The source Blueprint `Increment Cloud Movement Cache` sets
Shared Time Offset = Time Delta * Time Speed, Clouds Time Offset = Cloud Speed *
UDW Wind Multiplier * Shared Time Offset, then accumulates Clouds B Time.
`Update Cloud Movement` interpolates that cache and adds Cloud Phase plus
Time Based Phase. The standalone default Time Speed is 1 and Cloud Speed 0.35.
This implementation covers constant explicit multipliers and independent elapsed
time; movement tied to time of day and weather-derived multiplier policies remain
separate pending work. WindXZ metre travel is not silently reinterpreted as a
source speed multiplier.

The timeline remains the sole time authority. Runtime cloud time is source
initial time + configured phase + elapsedSeconds * speed * timeScale *
windMultiplier. The material's morph rate/period reduce it to [0,1) in CPU double
before float publication. SkyAtmosphereFrame validates lighting and phase together;
invalid commands leave both unchanged. Main and reflection draws only consume
this snapshot. Morph animation does not rebuild the atmosphere-only sky-view LUT.
Version 1--3 profiles migrate to zero cloud speed; version 4 requires the complete
motion object and rejects unknown fields. The new sky-animated.json reuses the
existing resource manifest and freezes solar time while morphing thin clouds.
This does not yet add texture advection, volumetric clouds or weather-driven colors.

Cloud-motion validation passed 14 focused tests, including real authored texture
movement after a half morph cycle, exact HDR image restoration after a full
cycle, negative-time atomicity, version-4 validation, frozen legacy migration and
script projection. The diagnostic game uses the normal main.nut update path;
MCP only reads screenshots. Two five-second-separated captures changed 538713
of 921600 pixels (maximum channel delta 14/255), with cloudTime advancing at
0.35 source units per elapsed second. Visual inspection confirmed thin-cloud
morphing. These deltas prove motion, not Unreal animation parity.

The default version-3 frozen screenshot is byte-identical to the preceding
cached renderer (SHA-256 dbec76e793633cbfd1dac92efa8173d9496110a04125b2c831b67402aa39402b),
so its formal reference metric remains 97.07389% within 3/255. The ten-second
example smoke passed. The HEAD-relative architecture audit passed. The full
quality result remains recorded separately in `.local-debug/uds-cloud-motion-quality.log`.

## Clear-sky daylight material contract

`SkyAtmosphereFrame.dayPhase` is a copied, normalized [0,1) projection of the
existing `SkyTimeline` hour. `SkyRuntime` computes it before atomic publication;
rendering and reflection never advance it. Values that round to one in float are
bounded below one. Invalid phase/light/cloud inputs leave all fields unchanged.
`setLight` preserves both animation phases. No registry or string lookup occurs
in the per-frame path (R-MECH-1/4); no additional mutable clock is introduced.

`SkyDaylightLayer` owns only immutable authored coefficients. The graphics-owned
wisps asset owns both star texture byte arrays; borrowed upload descriptions are
copied to GPU storage during preparation. Failed validation/upload publishes no
contributors. The existing pass/provider retirement contracts remain unchanged.
No new module, ECS System, Link, optional provider or trim claim is introduced.

The persistence boundary is `eve.sky-wisps/5`, with strict field sets and bounded
21-key linear scattering data. Versions 1--4 explicitly disable the new chain.
The original UDS blueprint graphs establish twilight, night filtering, cloud colors,
manual lunar orientation and parent contrast; GPU contract probes check against
captured UDS material values at 00/06/12/18 rather than screenshot-fitted RGB values.
Reference working-space luminance uses Rec.709 factors. Frozen noise, source mip-zero
sampling and the clear/manual branch are explicit limits; no cross-backend bitwise
or complete UDS-equivalence claim is made.

## Reference implementation findings (clear/manual branch)

The UDS blueprint and material exports, rather than screenshots, define this pass:

- `SetCurrentFogBaseColors` samples `Fog_Scattering_Color` at the angular height
  of each light. The sun and moon use different source divisors (33 and 23.25),
  multiplied by 12 and their source intensities. A power-11 twilight filter
  attenuates each contribution near the horizon.
- `CurrentCloudWispsColor` mixes the source day/dusk/night tints with these
  contributions. The material then applies its original per-vertex directional
  gradients and source thin-cloud opacity, preserving the imported mesh colors.
- The squared night filter drives glow and stars. The contrast midpoint is half
  the Rec.709 luminance of sun, moon and night-glow contributions. Overall
  intensity also increases when neither light supplies illumination; using a
  fixed noon midpoint was therefore incorrect at night and around sunrise.
- The default reference uses the manual moon orbit and tiling-star material,
  not astronomical star coordinates. Its original star/noise texture bytes,
  polar vertex UV mapping and frozen noise are now consumed by the native pass.

The captured source component values also prove a separate remaining difference:
Rayleigh RGB changes from (0.168627,0.407843,1) at noon to
(0.241211,0.347328,0.609375) at midnight, before the 0.04 scattering scale.
Ozone absorption scale changes from 0.002 to 0.004, with a different RGB tint.
The 06:00 component has the intermediate 0.003 absorption scale. Native optical
LUTs still use the noon values. Completing this requires source-driven optical
parameter updates and their LUT invalidation/lifetime contract; changing only
cloud RGB cannot reproduce that mechanism. Lunar atmospheric scattering, lunar
surface rendering and the original star mip chain are also separate remaining
work. Hourly acceptance must not be inferred from the four material probes.

### Daylight material verification, 2026-10-09

The rebuilt Vulkan path passes all 24 focused sky tests. After replacing unused
legacy star bindings with one-pixel descriptors, all six affected atmosphere/wisps
cases pass again; the default example also survives its ten-second smoke run.
The source-owned 00/06/12/18 material probes validate cloud RGB, midpoint, intensity,
manual moon orientation, night glow and fog contribution. The new 24-hour native
capture batch has exact repeated pixels at every hour and no Vulkan validation errors.

With the previous fixed-camera Unreal captures unchanged, mean hourly RGB MAE
falls from 4.398026 to 1.668592 /255 (62.06% lower). Hours 07,08,09,15,16,17 pass
the comparison's 99% within 3/255 pixel gate and SSIM requirement; the other 18
hours do not. These are separate gates, not a claim that SSIM measures pixel parity.
The full day remains below the requested acceptance. Reference 04:00's process
exited abnormally after validated output; its camera, hashes and exact-repeat
checks passed, and this qualification remains in the report.

`make check` fails at the origin/dev-relative architecture gate on pre-existing
worktree/base differences across Object, b64, card/data and other modules, including
uncatalogued ECS declarations. The HEAD-relative diagnostic passes. No baseline,
allowlist or architectural exception was added to suppress these findings. This
change follows the single-clock authority, versioned strict import, synchronous
borrowed-upload lifetime, Result propagation and no-new-cross-module-dependency
rules; it introduces no new ECS System, Link or optional provider.

## Optical volume and reference mip completion

The preceding material-only findings are superseded for `wisps-optics.json` by
schema 6. Rayleigh and ozone coefficients are now derived from authored source
values through the source blend/complement equations. `SkyOpticalCycle.h` supplies
a pure CPU projection for baking and numerical oracles; the matching shader
projection supplies local extinction/scattering. The 81-slice volume has a declared
0.002 absolute interpolation-error bound at tested half-slice points near the
nonlinear transition. No per-hour screenshot colors are fitted. The immutable
volume snapshot is shared by the background, sky-view and wisps shaders via the
existing resource upload identity, with no new graphics/backend API or mutable
resource authority. Its preparation cost is documented on the existing builder.

The expensive prepare stage owns temporary CPU tables until synchronous upload;
partial failures do not register view/render contributors. `setFrame` still only
validates/copies light and phase, with no resource construction. Day fraction joins
the sky-view cache key, so changing moon direction invalidates the view even when
other inputs coincide. The new runtime integration test crosses midnight and then
advances to the initial hour; the HDR output restores exactly. Legacy schema 1--5
uses one optical slice and the previous single-light physical path.

The moon's source directional color is quantized through sRGB8 like Unreal's
DirectionalLightComponent before physical scattering. Its manual direction,
illuminated fraction, horizon fade and night brightness feed both the physical
sky-view integrator and distant fog ambient integration. Weather/eclipses and a
lunar surface disk are separate scope.

The original star texture uses Sharpen1 and platform compression. Schema 6 can
store its actual GPU-read mip chain as RGBA16F, with explicit level count and
strict packed byte extent. The mip extraction uses a diagnostic unsaved unlit
material in the reference project's separate namespace and does not modify the
plugin. Texture orientation is checked against the original source, and a second
independent extraction verifies reproducibility. The helper and hashes make this
asset transformation reviewable rather than an undocumented substitute texture.

Applied architecture rules remain single-clock authority, immutable ownership,
structured checked Result failures, strict version/unknown-field handling and
cost-visible preparation. No new System, Link, capability boundary, optional
provider or trim claim is introduced; no deliberate architecture exception is used.

### Optical/mip completion acceptance

All 26 focused sky cases pass with Vulkan validation enabled, including the new
source component oracle, half-slice optical interpolation checks, schema-6 mip
upload metadata and midnight/24-hour HDR restoration test. The default legacy
example passes the ten-second smoke. Two independent UE extractions produce
byte-identical payloads for all 12 star mip levels. The supplied reference content
fingerprint remains `3ac7326885c4e4686d26f29b36a301fda5c8691493f98b1fd7e64e57522c838d`.

The v3 24-hour capture repeats exactly at every hour, with no Vulkan validation
errors. Mean hourly RGB MAE is 0.954145/255 versus v2 1.668592/255 (42.82% lower).
Seventeen hours pass the pixel gate (99% of pixels with all channels within 3/255):
00--05, 07--09, 15--17 and 19--23. Only eight also meet SSIM >=0.99:
05,07,08,09,15,16,17,19. In particular, the near-black midnight image meets the
pixel tolerance while its SSIM is about 0.9757, so it is not a combined-gate pass.
Dawn/dusk and five daytime hours still fail the pixel gate. Full UDS acceptance
remains incomplete; exact post-process and remaining authored lighting/material
behavior need further source-level comparison.

The HEAD-relative architecture diagnostic passes. Full `make check` still fails
on the existing origin/dev-relative architecture differences described above;
no failure was hidden, baseline changed, or exception taken. The extraction helper
also passes Ruff, Python syntax checking and a real UE run. The offline comparison
artifact verifies 48 embedded images, 24 entries, exact repeat pixels, JavaScript
syntax and ZIP integrity; browser interaction was not independently exercised.

### Packed atmosphere LUT precision follow-up

The desktop reference uses `PF_FloatRGB` (R11G11B10) for both transmittance and
multiple-scattering LUTs (`SkyAtmosphereRendering.cpp::GetSkyLutTextureFormat`).
Native preparation now rounds each RGB texel to that storage precision before
subsequent integration/filtering, including 6/6/5 mantissa bits, subnormals,
round-to-nearest-even and finite saturation. Decoded values remain in the existing
RGBA32F optical volume; no public API, ownership or shader-resource ABI changes.
This adds work only to preparation, not the frame loop.

The optical interpolation test keeps its .002 physical-interpolation budget and
adds the independently calculated half-ULP storage bounds for each endpoint and
center. The former .002 total full-precision bound no longer describes packed
samples; it must not be reported as the current total error bound.

At 00/06/12/18, fixed-view RGB MAE changed respectively from
.457408/1.713194/1.640655/1.647883 to .458428/1.710472/1.578522/1.646095 (8-bit units).
The noon pixel-within-3 fraction increased from 97.07389% to 97.42622%; midnight
is very slightly worse. These are four targeted samples, not a new 24-hour sweep.
The v3 comparison artifact remains the preceding, complete 24-hour baseline.
The sun-facing noon view has MAE 1.634510 and 93.63346% within tolerance, so the
broader view coverage still does not meet the 99% target. All six native captures
(four fixed views plus sun/moon-facing views) repeat exactly.

A separate 32-cubed display-LUT interpolation experiment worsened midnight and
twilight results and was reverted. It is not part of the retained implementation.
The retained change introduces no architecture exception or new boundary.

The follow-up passes all 26 focused tests with Vulkan validation and `git diff
--check`; six native views have exact repeats and no VUID/Validation Error log
entries. No public API or boundary refactor was performed, and the earlier full
`make check` baseline failure is not claimed resolved.

A fresh validated UE moon-facing capture (00:00, pitch 56.2726252351 degrees,
yaw 180 degrees) exposes the still-missing lunar surface disk. Full-frame MAE is
.232582/255 and the pixel-within-3 fraction is 98.53060%, despite the small screen
area of the missing moon. This is an explicit remaining gap, not a parity pass.

### Lunar surface completion (schema 7)

The missing lunar disk described above is now implemented in `wisps-moon.json`,
selected by `sky-daylight.json`. The unchanged schema-6 manifest remains available
as the preceding optical baseline. Version 7 owns two linear RGBA16F 1024-square
textures with eleven mips each, read from the original runtime Moon_Color and
Moon_PhaseNormal textures (including compression and SHARPEN4 filtering). An
independent repeat matches all 22 mip payloads byte-for-byte. The repeat color
export returned 0xC0000005 during editor shutdown after all eleven files completed;
the first color export and both normal exports exited cleanly. Source package
fingerprints are unchanged; no source assets were saved by the readback helper.

The material follows the source Moon function: manual orbit/X-axis projection,
angular diameter and horizon scaling, RBG phase-normal swizzle, rotated lunar
phase vector, earthlight, day/night surface intensity, opposite-hemisphere mask,
color-alpha silhouette and glow. Only the space layer is occulted by the moon;
atmospheric sky remains additive. Wisps, contrast and height fog are composed
after the lunar contribution. Phase days are fixed authoring input, independent
of the injected daily clock and the separately authored physical-light fraction;
real astronomical lunar phase progression and weather/eclipses are not claimed.

The 1280x720 moon-facing midnight comparison now has MAE .130495/255, SSIM
.994746 and 99.98969% of pixels within 3/255 in every channel (previously
98.53060%). The documented 80x80 context region has MAE .375, SSIM .994947 and
99.71875% within tolerance. A tighter 32x32 disk region is only 98.63281% within
tolerance, so this is not a claim that every lunar edge pixel has reached 99%.
Metrics use original full-resolution images with no exposure fit or alignment.
The four fixed-view 00/06/12/18 captures are pixel-identical to the preceding
packed-LUT baseline and all five new native view captures repeat exactly, without
Vulkan validation errors. The earlier v3 24-hour artifact is intentionally not
relabelled as a new sweep.

All 27 focused sky tests pass, including source axis values at midnight/dawn/noon,
full/quarter/new phase shading, persistent dark-side occultation, opposite-view
rejection, legacy-disabled behavior, texture ownership/mip metadata and invalid
coefficients. `git diff --check`, extraction-helper Ruff and HEAD-relative
architecture contracts pass. Full `make check` was run and remains blocked by
pre-existing origin/dev-relative API/lifetime/ECS catalogue differences outside
this lunar change; no baseline or allowlist was changed to silence them.

Applied rules: R-FACE-1/2 versioned VFS boundary catalogue, R-MECH-1/4 direct
immutable upload with no hot-path service lookups, R-COST-1/4 explicit prepared
resource cost, checked Result failures, single daily-clock authority, synchronous
borrowed uploads and atomic immutable ownership. No new provider, ECS System,
Link or trim claim; no architecture exception.

The final rebuilt binary reproduces the measured lunar screenshot byte-for-byte
and repeats exactly. The default uds-sky example also passes its ten-second
smoke run (no error markers).

## Procedural-v1 and replaceable external packs

`eve.sky-profile` version 6 has the version 5 fields plus mandatory `proceduralSeed` (integer
0..4294967295). A null `wispsAsset` selects `SkyWispsAsset::generate(seed)`; a nonempty string selects
strict external loading. Empty strings, invalid seeds and missing/unknown fields fail atomically.
Versions 1/2 remain atmosphere-only; versions 3..5 retain their mandatory asset path semantics.
`prepareSky` reports `assetSource` as `procedural-v1`, `external-pack` or `atmosphere-only`.

The graphics-owned immutable asset has one owner for backing mesh/texture bytes and borrowed upload
views valid until its final copy is destroyed. `generate` uses independent hashed cloud/star/moon streams;
no global RNG, filesystem, simulation state, callbacks or GPU are touched. Cloud animation continues
through the existing injected SkyRuntime clock. GPU preparation synchronously copies/uploads data;
no extra per-frame resource generation, file reads or string lookups were introduced. Generation is an
explicit expensive preparation API returning nodiscard Result, including allocation failures.

Procedural-v1 has bounded 96x48 sphere tessellation, 512-square R8 cloud noise, 1024-square RGBA8 star
mips, 64-square RGBA8 noise mips and two 256-square RGBA8 lunar mip chains (roughly 6.3 MiB texture data,
plus mesh and existing optical LUTs). Surface and phase normals are generated independently from UDS
images. The cloud field wraps at texture boundaries; stars and lunar mottling are seeded. Mips are
box-filtered at preparation. Same-platform same-seed bytes are reproducible; cross-platform float
mesh/noise generation allows numerical rounding differences and does not promise bitwise parity.

Pack persistence reuses the existing versioned profile/wisps/mesh contracts. No new file format or
asset manager is introduced. Pack selection is a cold initialization decision; malformed supplied packs
fail without publication and never silently fall back. The example uses generated resources without
Python; the optional Python launcher mounts arbitrary external folders read-only through the existing
Filesystem binding. Unit tests synthesize minimal packs for versions 1..7 and exercise generated
resources in the existing renderer/clock composition tests, with no private asset dependency.

Applicable architecture rules: structured Result/nodiscard, immutable owned storage and bounded borrows,
strict version migration/unknown-field rejection, explicit preparation cost, one simulation clock,
provider-present/absent paths, no new module boundary or ECS authority. No exception requested.

Verification for the external-pack/procedural change: native MSVC build passed; all 28 focused sky
cases passed with Vulkan validation enabled; default asset-free example survived the 10-second smoke.
Seven fixed-view captures (procedural 00/06/12/18 and moon, external noon and moon) repeated pixel-exactly.
External noon and moon are byte-identical to their pre-migration screenshots. Existing unused-vertex
attribute performance warnings and the Optimus layer version warning remain; no validation errors were
reported. HEAD-relative architecture contracts passed. Full `make check` against `origin/dev` still
fails on pre-existing cross-module API/lifetime/ECS catalogue findings; it is not a full green gate.
