# Procedural PixelFX (stylize)

Source: [patch-gg/pixelfx](https://github.com/patch-gg/pixelfx), revision
`a84159688af3ca37a4be05a0d422b94451b2e24e`. MIT notices are retained in
`src/modules/stylize/pixelfx/LICENSE.txt` and `NOTICE.txt`; the restricted gallery fonts are not imported. MIT code glyphs from core/font.js are included.

The compiled catalogue currently contains 244 native algorithms across fire,
projectiles, explosions, water, weather, lightning, ice, pickups, critters,
holy, impacts, scifi, arcane, all twelve Shadow effects, all thirteen Nature
effects, all ten Spawn effects, and all thirteen Status effects (Haste/Slowed/Bleeding/Stunned/Sleep/Confused/Charmed/Cursed/Burning/Poisoned/Rage/Petrified/Frozen), all twelve Melee effects, all twelve UI effects, all twelve Moves and transitions Blinds/CRT off/Pixel dissolve/Iris/Star iris/Diamond wipe/Glitch wipe/Slash wipe/Shatter wipe/Burn away. `PixelFx::catalogue()` and native
gallery search expose supported IDs from compiled descriptors. Unsupported
upstream IDs are rejected. Validation milestones below retain historical counts.
Source Act composition is implemented. Platform floating-point raster/occlusion ties
and final PR-head/platform validation remain explicit fidelity limits; see the port plan.

Halo reads the prepared actor pixels for head relighting. Its default is the upstream 24x38 dummy; native skeletal poses are available through PixelFxRig, described below. Body drawing is separate from the effect planes.

C++: include `stylize/PixelFx.h`; construct `PixelFx(kind)`, copy `settings()`, modify
and `configure(settings)`. Analytic effects use `render(absoluteSeconds)`; simulations use
checked advance(delta) and draw(), or explicit prepared seek. `color()` and `light()`
borrow instance-owned contiguous 0xAABBGGRR buffers until the next render or owner
mutation/destruction. Preserve their separation: alpha-composite color, add light.
`info()` supplies original logical dimensions, placement origin, duration and loop flag.
Display scale belongs to Sprite2D; use the same pivot on both sprites. Native
composition also supports local mirror/rotation through drawToCanvas, described below. There are no GPU pointers in PixelFx.

Input primary palettes contain 1..64 opaque colors and are resampled once during configuration
to eight steps for the original effects or nine for the fire props. Typed fire settings contain
wood/iron or root/wax/brass ramps, also normalized during configuration. All material colors are opaque. Configure rejects NaN/infinity, playbackSpeed outside
(0,64], lightGain outside [0,16], ray count outside [1,32], line count outside [1,256]
and length outside [1,100]. `rays` controls sunburst, `lines` controls impact frame,
`length` controls laser bolt. fire.height in [1,100] controls flame height;
EveScript uses setParameter("height", value) and setRampColor(name, index, r, g, b). Palette/seed/playbackSpeed apply across families; impact has no glow.
Invalid parameters throw `std::invalid_argument` before publication. Render rejects
seconds outside finite [0,1e12], or scaled effect time above 1e12, before changing either plane. These are programming
preconditions, not resource lookup results. No lastError or operation-result bool exists.

Absolute time is injected; no global RNG or wall clock is read. Upstream 32-bit hash,
mulberry32 sequence, drawing order and Bayer thresholds are preserved. Pure effects
are directly seekable; impact evaluates its frame mask from the seed, without mutable
simulation history or preallocated frame masks. Duration expiry is advisory: a one-shot
can still contain source tail pixels at or after that time. Analytic one-shot evaluation
clamps to duration; simulation state continues when explicitly advanced. Loop time is
canonicalized to [0,duration), so an exact endpoint reproduces time zero. Pure reference
frames use explicit absolute time to avoid accumulated clock rounding at raster edges. RGB gain uses saturated
addition and is independent of color coverage. Cross-platform floating raster edges
are not guaranteed bit-identical; the validated Windows reference samples were exact
before the endpoint.

EveScript: instantiate `eve.Stylize()` first, then `eve.PixelFx(id)`. `renderTo` takes
absolute seconds and two caller-owned distinct RGBA8 ImageData objects with matching
logical dimensions. It validates both destinations before rendering and copying pixels.
No images or VM references are retained. `gfx.updateTextureFromImageData` updates
resident textures; `Sprite2D.setBlend("additive")` draws the light plane. The runnable
example is `examples/pixelfx`.

## module-interface: stylize procedural PixelFX

- Capability input/output: none added. Existing stylize package owns this API; no new
  module, boot slot, optional provider or per-frame capability lookup.
- ECS and links: none in the CPU API. Example uses existing Sprite2D entities and
  Graphics-owned textures; it introduces no new entity hierarchy or cross-domain link.
- Events: none. Caller owns playback and re-trigger policy.
- Script: native `PixelFx` class registered by Stylize; typed C++ settings are the
  authority, bindings copy/validate before assignment.
- Tools: no new MCP family. Existing engine screenshot tools observe the rendered demo.
- Lifecycle/threading: one instance owns two buffers and copied settings, is affine to
  its caller thread, invokes no callbacks and retains no image/graphics reference.
- Configuration protocol: eve.pixelfx.configuration v1, strict unknown-field/version rejection.
  This initial version has no predecessor. Future schema versions require an explicit migration;
  no simulation snapshot/save-state format is currently claimed.
- Trim evidence: algorithm-only sources compile/link without graphics, image or VM in
  the native probe (L2 for this CPU surface). No claim is made that the entire stylize
  module can be built or run without its existing graphics/image dependencies.

Verification now runs in the isolated codex/pixelfx-native worktree. Root Windows SHARED
Debug build passed with its pinned, SHA256-verified dependency package. The ten-effect
example passed the real 6-second smoke. Initial four-effect MCP readback produced 24 actual
1120x720 frames and an animation under build/pixelfx-validation; this is separate from
the earlier CPU-only preview.

The native static/DLL contracts pass 25 cases and 1977 assertions per linkage. Python
coverage and spatial comparison tooling passes ten tests. Mathematical RNG/noise and
simulation checkpoints come from the pinned upstream implementation. Shared helpers cover
all twelve particle kinds, smoke generation and landing without retaining invalidated
particle references.

The prototype's original 32 pre-endpoint reference frames had 64/64 exact planes on the
validated Windows build. Checked-in portable statistics allow four boundary pixels and
1020 channel units per plane; they do not replace spatial comparison. Source-owned tools
and usage are documented in scripts/pixelfx/README.md.

The current compiled inventory is 126/244. Fire torch, candle and brazier each matched
18/18 raw planes at defaults and 18/18 with seed 123 (108/108 planes total). Spirit flame adds another 36/36 exact planes across the same two seeds.
The pure reference tool evaluates exact absolute sample times rather than accumulating
floating step error into a different time. Generalized parameters, actor integration, remaining
effects, final OBJECT build and cross-platform gates remain open. Core PixelFX cases live in stylize_tests.cpp, with cohesive fire, water, weather,
lightning and ice suites beside it. The module evidence gate passes without policy changes. make check passed in the isolated branch.

Fireball uses fire.radius [1,20] and fire.speed [1,500] (EveScript radius/speed).
These change the physical head/tail recipe independently of playbackSpeed. Tornado uses
fire.height. Their defaults and parameter variants matched 72/72 raw planes exactly.

The ten-effect host build and all 13 targeted cases passed. Engine-owned MCP readback
also verified key-event selection and default/variant rendering of fireball/tornado.

Fire breath supports spread [0,1] and physical speed [1,500]. Flamethrower supports
reach [20,200], physical speed [1,500] and an opaque pilot ramp resampled to three colors.
EveScript uses spread/reach/speed in setParameter and pilot in setRampColor.
Both defaults and seeded parameter variants matched 72/72 raw planes exactly.

The twelve-effect Windows SHARED build, all 15 host cases, make check and a six-second
native gallery smoke passed. Engine-owned screenshot readback verified the fifth page's
default and parameter-varied fire streams after key-event injection.

Native prepared configuration: call PixelFx::prepareConfiguration(kind, settings), check the
Result, then move its candidate into instance.commitConfiguration(candidate). Check that Result
as well. Preparation copies/normalizes; commit transfers ownership without allocation or
resampling. A candidate can outlive its source and be published to any same-kind instance once.
Moved-from/consumed or wrong-kind tokens produce structured rejection; wrong-kind rejection
keeps the token available for its correct target. Dropping a token cancels without publishing.
Existing pixels survive both preparation and commit until the next render call. configure remains
a compatibility facade over this path with the same preparation cost and exception projection.
Prepared tokens remain in-process owning objects and cannot be serialized. Both calls are owner-thread affine.

Metadata/default entry points/draw entry points now belong to immutable descriptors beside each
algorithm. Composition holds only accessor names; render calls a cached trusted function pointer.
Source-owned active parameter declarations drive the canonical Value codec. EveScript exposes
checked preparation/publication and its compatibility setters use the same codec.
The independent probe compiles the actual common Result/Status/Diagnostic sources in static and
shared forms; it does not replace the foundation with a mock.

Prepared configuration validation: 18 static/DLL cases with 1497 assertions per linkage;
288/288 saved fire reference planes matched through the native prepared consumer. The Windows
SHARED host build and all 18 focused cases passed, as did staged make check and a six-second
real-engine gallery smoke. Engine MCP verified key-event selection and the rendered script path.

Architecture applicability for this API change (module interface/cost specification):
- Sections 1/3: existing stylize ownership and six faces remain; common Result/Status introduces
  real foundation symbol references, verified by static and separate DLL composition. No new module.
- Section 2: instances own published state; tokens own pending candidates. No ECS type, Link,
  provider lifecycle, persisted data or duplicated mutable configuration authority is added.
- Section 4: one known compiled effect per instance, high-frequency frame calls, directly known
  observer and synchronous frame replacement select a cached C++ entry point. No runtime string
  lookup, capability lookup, callback bus or lock is added to frame dispatch.
- Section 5: preparation exposes bounded copy/resampling cost and returns an owning token.
  Publication transfers already validated state. The compatibility facade executes the same path.
- Optional-provider, persistent-format and backend-interface changes are not part of this step.
  Additional parameter kinds, actors and remaining effects remain unfinished requirements, not exceptions.

Canonical configuration protocol: an object with schema = eve.pixelfx.configuration,
version = integer 1, effect = the exact id, and options = an object of active parameters.
All four fields are required; unknown top-level fields and options reject. Missing options use
the effect's defaults, so this is replacement configuration rather than a patch. Palette values
are arrays of 1..64 opaque packed 0xAABBGGRR Int64 colors, resampled once at preparation.
Integer fields reject floating-point Values; numeric fields accept Int64/Double and reject
NaN/infinity. Diagnostics use escaped JSON-pointer paths such as /options/pilot/0.
Inactive legacy C++ settings slots do not affect the algorithm and are not serialized.

C++: configurationValue(), describeParameters(kind), prepareConfigurationValue(kind, value).
The descriptor's actual typed members determine parameter types. Bounds, ramp sizes and default
values come from those declarations, without a parallel parameter/JSON registry. Value/JSON
conversion runs only at the cold boundary; rendering retains typed state and direct dispatch.

EveScript: getParameters() and getConfiguration() return common Result tables with owning value
payloads. prepareConfiguration(configurationTable) returns a Result with an owned opaque
PixelFxPreparedConfig; commitConfiguration(candidate) returns a Result and consumes it once.
Check each .ok before accessing .value. Wrong-kind and consumed candidates reject without changing
the instance. Construct candidates through prepareConfiguration; the token constructor is not a
public construction path. Dropping an unpublished candidate cancels it. Existing setParameter,
setSeed, setPaletteColor and setRampColor are exception-based compatibility facades over this
same Value/typed preparation pipeline. These are configuration operations outside frame loops.

The two Value protocols are versioned; tokens and simulation internals are not persistent data.
Unknown field/version policy is strict rejection, not silent coercion. This adds the configuration
data face to the module interface; it does not introduce another mutable owner or a new module.

Native Value/JSON numbers retain their C++ precision. Squirrel numeric values follow the
configured SQFloat precision; integer parameter bounds are projected as Int64 to preserve
seed limits. The codec does not claim lossless arbitrary-double round trips through SQFloat.

The final codec milestone passed Windows SHARED build/link, 20 focused host tests,
20 static/DLL cases with 1759 assertions per linkage, final native smoke and make check.
The independent Value-path reference comparison matched 288/288 raw fire planes.
The official binding contract generator reports no unresolved bindings. Parameter querying,
configuration snapshot/preparation/publication and consumed-token rejection were exercised
by all 24 gallery instances through actual Squirrel execution, with engine-owned MCP readback.

Fixed-tick playback and prepared seek:
- isSimulated() identifies owning simulation kernels. advance(deltaSeconds) takes checked finite
  deltas in [0,60], scales by playbackSpeed once and retains fractional elapsed time. Time uses
  compensated summation; active simulation ticks use 1/120 second with a 1e-9-tick boundary
  tolerance. No accepted delta is silently dropped. Invalid deltas reject before mutation.
- draw() samples the current owner state without advancing/seeking. render(seconds) remains
  analytic-only compatibility sampling; simulations reject it before mutation. drawTo in
  EveScript validates caller-owned images before drawing and returns the common checked Result.
- effectTime()/getEffectTime query unwrapped time; activeTickCount()/getActiveTickCount query actual
  executed kernel ticks (zero for analytic effects). Explicit accepted advances execute all
  crossed fixed ticks, including after duration expiry or stop. done()/isDone only query
  duration expiry or completed stop fade; they do not suppress source tail rendering.
- stop() starts a 0.3-second fade once. Repeated stop preserves the first stop time.
- prepareSeek(effectSeconds) rebuilds and replays an uninterrupted timeline privately. Time is in
  effect seconds, independent of playbackSpeed. Active replay above one million ticks rejects
  before replay work for every simulated effect, including a duration-complete one-shot.
  commitSeek(candidate) checks immutable Program identity, rejects stale/wrong-owner/consumed
  candidates and publishes once. Old pixels remain until draw. Seek clears previous stop state.
- Immutable Program ownership is genuinely shared only by the owner and outstanding seek tokens.
  Mutable playback/kernel state has one unique owner. Config publication resets playback and
  replaces program generation; old seeks keep their inputs alive but cannot overwrite that generation.
  Owner moves transfer program identity; tokens can outlive destroyed owners without dangling data.
  Tokens are in-process only, not persisted identities. Both destruction orders are tested.
- Configuration preparation includes authored initial geometry; seek preparation exposes replay cost.
  Publication allocates/replays nothing but releases old owned state. Simulation allocation failure
  may throw std::bad_alloc; incremental advance does not promise rollback for process OOM.

Flame pillar is fire/flame-pillar (184x256, origin 92/232, 5-second one-shot).
height is [1,200] with default 140; stone is an opaque packed scalar color with default #4a3f69;
seed defaults to 5. The source recipe includes cracks, flame-column strands/pulses, independent
particles, falling/rotating rocks, one-time landing dust and light. RNG argument evaluation is
explicitly ordered, including sign-before-range sampling. Default and seed=123, height=105,
stone=#456a91 samples matched 36/36 raw planes against the pinned upstream.

EveScript playback methods: isSimulated, advance (checked Result), prepareSeek (Result containing
owned PixelFxPreparedSeek), commitSeek (checked Result), drawTo (checked Result), stop, isDone,
getEffectTime and getActiveTickCount. The token constructor is not a public creation path.
Legacy equal setters do no backend reconstruction and preserve outstanding seek validity.
The gallery's page 6 shows default/variant pillars side by side; hidden simulations are paused.
Script owns repeat-preview policy; native playback owns time, RNG and stop state. S stops repeat
and fades the current preview; R explicitly seeks all timelines to zero and reenables repeat.
These APIs stay in stylize; no ECS root, module, optional provider or new persistent state is added.

Playback validation: Windows SHARED build and 25 focused host cases passed, together with
25 static/DLL cases (1977 assertions per linkage), staged make check and the native smoke.
The full saved fire reference set matched 324/324 raw planes after the playback refactor.
Real key-event/MCP verification selected page 6, confirmed two identical paused rendered frames,
one-color background after stop, and restored visible simulation after R. These are engine-owned
readbacks, not desktop captures or test markers. The corresponding report and PNGs stay under build.

Architecture applicability extends the previous section: mutable elapsed clock and kernel state
have unique ownership; pending seek states remain private, sharing only immutable Program inputs.
Shared Program lifetime is intentional across pending transactions, with identity invalidation on
publication rather than a raw external handle registry. Native and Squirrel paths share Result
contracts. No ECS System, entity hierarchy, optional provider or persistent simulation format is added.
Config/seek preparation exposes geometry/replay cost; publication releases old storage without
replay or allocation. Active tick time and retained elapsed/sub-tick time have distinct roles.
No exception to the architecture rules is introduced; full actor work and final acceptance remain open.

Campfire is `fire/campfire` (128x128, origin 64/112, three-second loop). Its active
options include seed (default 3), height (default 22) and wood (six-color ramp), in
addition to palette, playbackSpeed and lightGain. Its crossed logs, coals, flame
layers, smoke and bouncing sparks are evaluated procedurally at the requested time.
Default and changed seed=123, height=35 and wood ramp samples match 36/36 raw
color/light planes. Gallery key 7 displays defaults beside a changed variant.

Actor inputs (initial consumer: halo)

The source-owned `actor` parameter has type `actor`. Its default and constraints are exposed
by getParameters(); configuration stores a nested `eve.pixelfx.actor` schema, version 1.
Every actor object requires schema, version and source. Sources none/hero/dummy derive all
geometry during preparation and reject extra fields. Hero is upstream 12x19; dummy is 24x38.
None clears actor geometry explicitly. Custom requires width, height, scale, x, y, head, chest,
eyes, mask and pixels. Width/height are integers 1..256, scale is 1 or 2, local coordinates
are finite within -4096..4096. Head/chest are two-coordinate arrays; eyes contain at most 64
mask-local integer pairs inside the rectangle. Mask and pixels each have width*height entries:
mask is binary; pixels are packed uint32 RGBA, including transparent cells. They are independent
input facts, not inferred from one another. Unknown fields/versions and invalid cells reject
with paths such as /options/actor/pixels/0. There is no predecessor schema to migrate; future
versions must provide an explicit migration rather than partially accepting unknown fields.

Typed PixelFxActor arrays are copied into the private configuration candidate, then transferred
into immutable Program ownership on publication. Stock geometry is derived from canonical source
rows; serialized stock input retains only the source tag. Editing or destroying the caller's
arrays, cancelling a token, or destroying the original instance cannot mutate a prepared actor.
No ECS entity, image, VM object, callback, raw cross-frame pointer or new provider is introduced.
Replacing actor input uses the existing config generation and invalidates earlier seek tokens.
Frame rendering reads typed arrays directly; schema and field lookup remain cold operations.

C++ drawActor(target) clears and fills a caller-owned effect-sized color plane, synchronously,
without altering effect color/light or timeline. A wrong-sized target rejects before modification.
EveScript drawActorTo(image) returns a checked Result for an RGBA8 image of the effect dimensions;
its image borrow ends before return. This explicit body refresh performs bounded allocation/copy
in the script adapter, so retain the resulting texture while actor input is unchanged. Body pixels
are composed behind the effect and its light. The common Squirrel Value adapter is reused with a
140000-element configuration budget, sufficient for two maximum-sized actor arrays and metadata.
getConfiguration uses the same budget and preserves a checked failure instead of a success/null
payload. Native/JSON numbers retain double precision; Squirrel coordinates use SQFloat precision.

Four halo datasets (default dummy, hero, custom fractional-position silhouette and no actor)
matched 72/72 raw color/light planes against the pinned upstream. Exact stock integer data hashes,
body placement/size rejection, one-row bounds, independent array ownership, JSON restore and invalid
nested-schema/cell rejection are included in the existing per-module tests. Gallery page 1 shows
stock dummy with the default halo and caller-authored procedural geometry with the variant.
Rig poses, target choreography, onBody remapping and act/chain remain unfinished; this static input
and head relighting step does not claim those features. No architecture exception is introduced.

Actor milestone validation passed 30 native host cases and STATIC/DLL contracts with 3375
assertions per linkage, the full Windows SHARED build, make check and six-second gallery smoke.
A real Squirrel boundary game prepared, published and restored both 65536-entry arrays without
truncation or shared input mutation, then drew the actor image. The saved fire regression suite
still matched 360/360 raw planes. Engine-owned actor-gallery.png documents stock/custom composition.
No actor geometry can be published by an effect without a source-owned actor declaration;
inactive typed actor fields are cleared during preparation and tested with malformed legacy input.

Native rig builder (initial explicit pose evaluation)

C++ includes stylize/pixelfx/Rig.h. rig::Pose owns scalar/optional pose and weapon inputs.
solve returns an owning y-up Solved state; skeleton returns twenty screen-space y-down joints.
Rasterizer is a move-only caller-thread-affine builder that allocates its source 300x240 scratch
planes once. render(pose) validates and produces owning sparse xs/ys/color/src/ink arrays with
bounds and mean depth. Returned snapshots outlive later renders, moves or builder destruction.
No process-global mutable raster or borrowed input reference is retained. This step evaluates
explicit poses without the upstream global quantized memoization; act integration/cache lifetime remain pending. No frame-time or performance equivalence is claimed yet.

The solver preserves fixed lengths, reach clamping, hip sinking, turn/lean/head orientation,
armed elbow pole search and two-handed grips. Body primitives share a Float32 depth buffer,
flat tone voting, seams and an ink ring. All eleven source weapon shapes are generated at render
time, including bow/string/nocked arrow and dual-hand/off-hand geometry. Source indices map each
posed body pixel back to the standing 24x38 actor for future onBody composition. No sprite frames
or weapon images are used. The reference runtime's normalized compensated three-term norm is
used to avoid changing boundary ownership; see the
[V8 numeric implementation](https://github.com/v8/v8/blob/main/src/builtins/math.tq).
The fixed-positive-denominator torso angle uses atan(lean/13); a measured MSVC atan2 difference
at lean=3 caused three raster/mapping cells to diverge before this equivalent form was used.

Pose documents have schema eve.pixelfx.pose, integer version 1 and a fields object. Missing fields
replace with the source STAND defaults. The typed member table owns field names and types;
weapons use none/sword/greatsword/dagger/spear/hammer/scythe/claw/bow/pistol/launcher/handle.
Booleans stay booleans; face is integer -1/1; optional coordinates must be numbers when supplied.
All numbers must be finite within -4096..4096, wl is 0..1, and two-handed poses require a weapon
with a source-defined second grip. Unknown keys, fields and versions reject. Initial v1 has no
predecessor; a future version needs an explicit migration. Invalid/singular or grid-exceeding
poses return structured diagnostics and cannot publish a partial snapshot. The builder is reusable
after rejection. Public rotate helpers require a finite angle and assert their parameter contract.

EveScript loads eve.Stylize then creates eve.PixelFxRig(). renderTo(poseDocument, image, originX,
originY) returns a common checked Result containing pixel count/bounds. Input images must be
RGBA8 with dimensions 1..2048 and integer origins within -4096..4096. Validation/pose rendering
complete before the image is cleared; failure retains the caller image. It then clears and clips
complete sparse pixels into the image synchronously. The image and VM inputs are borrowed only
for this call. getSkeleton(poseDocument) returns a checked owning joint-coordinate table. This is
an explicit potentially expensive builder operation; retain textures/snapshots until the pose
changes rather than recreating a builder and raster every frame.

Seventeen actual upstream public posed/skeleton fixtures cover stand, prayer, crouch, air, yaw/
face turn, dual wield and all eleven weapons. Every sparse coordinate, color, source mapping,
ink flag and bounds entry matches exactly on Windows; mean depth matches and joint error is below
1e-15. The strict rig comparator separately reports numeric, sparse-order and spatial differences,
and does not hide color or source-index errors behind equal channel totals. Portable module tests
check joint tolerances, raster structure/statistics, protocol rejection, ownership, moves and failure
recovery. Gallery key 8 renders stand and all weapons through these native script bindings.
This adds no ECS, provider, module dependency or universal actor root. The existing stylize six-face
interface is extended through its script/value boundary; all mutable scratch belongs to one builder.
All-effect act choreography, targets, onBody remapping and final acceptance remain open requirements.

Rig milestone evidence: the full Windows SHARED host build and 32 focused host cases passed;
static/DLL contracts passed with 70727 assertions each. All 17 strict sparse rig comparisons
passed. make check and the six-second native gallery smoke passed; engine-owned rig-gallery.png
was inspected. Runtime artifacts stay under build/pixelfx-validation and build/pixelfx-rig.
The effect catalogue remains 28/244; these rig primitives do not count as additional effects.

Independent rig parts and pose tracks

Rasterizer.renderParts(pose) returns an owning list in source part order. Each present part
(body IDs 1..22, primary/off-hand weapon 23/24) is rasterized alone with its own ink outline,
center and mean depth. Empty parts are omitted. Names borrow immutable process-lifetime literals;
all pixel arrays are independently owned. This explicit exploded-view build costs up to 24
complete filtered raster passes; it is not an inexpensive frame query. Failure returns no partial
part list and the builder recovers on its next call. All 387 present parts across the 17 source
fixtures match exactly. Contract tests verify snapshot lifetime and filter/failure recovery.

PoseTrack is a move-only immutable compiled input, prepared through a Result-returning factory.
Its eve.pixelfx.pose-track v1 document contains schema, integer version and keys. Each key is
[time, authoredFields, arrival?], with 1..4096 keys and finite times within -1e6..1e6. Times must
be nondecreasing; equal-time keys preserve source order and represent real source transitions.
Arrival is absent/null/smooth, in, out, line or snap. Unknown root/pose fields, easing names and
versions reject. Authored fields use the same typed Pose codec; failures identify the key and
field (for example /track/keys/1/1/armsFront). Initial v1 has no predecessor; future formats need
explicit migrations. ghost is a numeric source activation field, not a boolean flag.

The compiled keys retain which fields were authored. STAND numbers interpolate, boolean/weapon
metadata holds until the later key, and optional numbers retain the source first-appearance rules.
Unmentioned hx/hy return to zero and wl to one. Limbs are held on source 12-FPS steps counted
from each key; x/y use smooth segment time. Snap holds the entire earlier key. Before the first
and after the last key the endpoint pose is returned. sample(seconds) returns an owning effective
Pose and isHeld(seconds) returns a checked observation. Times must be finite within -1e12..1e12.
Sampling searches keys and visits fixed typed fields without allocation or per-frame string lookup.
The caller supplies time; there is no hidden clock, RNG, callback, mutable cursor or global cache.
Native track preparation/sampling is caller-thread affine by convention; moves/destruction require
exclusive ownership. The script adapter runs on its VM thread.
Samples and prepared tracks remain valid after source input/builder destruction; owner moves and
invalid preparation leave earlier samples untouched. No ECS model or cross-domain Link is added.

EveScript uses builder.prepareTrack(document), producing an owned PixelFxPoseTrack Result payload.
Its sample(time) and isHeld(time) return common checked Result tables. Track construction is only
through the factory. prepareTrack uses the common Squirrel Value adapter with a bounded 200000-node
budget, sufficient for maximum canonical key/field input. The native track retains no VM nodes.
The owning PixelFxRig builder's renderTrackTo(track, seconds, image, originX, originY) samples
native typed state and performs the same checked raster/image path as renderTo. Source-rounded
x/y motion is applied separately from held limb sampling. Image validation and complete raster
precede clearing; failure preserves the image. Returned bounds remain local rig bounds. This is
explicit raster work with sparse output allocation per call; reuse the builder and image/texture.

verify_pixelfx_tracks.py reproduces all 73 actual upstream authored tracks with 3993 exact-key,
adjacent and interval samples. The comparator checks effective numeric defaults, numeric ghost,
metadata types, optional-field presence, skeletons and shortfall independently. Maximum error
was 7.105427357601002e-15; metadata/presence matched. Source reference tools refuse dirty tracked
inputs as well as a wrong revision. This proves track sampling, not the effects themselves.
Gallery key 9 plays a prepared track procedurally; SPACE pauses its explicit time and R restarts.
The effect inventory remains 28/244. createAct hitstop, target dynamics, dust/ghosts, onBody
composition, sequential pose-cache policy and the remaining effects are still unfinished.

Track/parts validation passed the Windows SHARED host build, 35 focused host tests and static/DLL
contracts with 70912 assertions each, source quality and six-second native smoke. The actual
Squirrel adapter accepted 4096 keys with all 35 fields; a subsequent caller mutation/deletion did
not change the prepared last pose. A 64-bit out-of-range origin was rejected before narrowing.
Engine-owned captures showed 12992 changed pixels during track motion and byte-identical paused
frames. These artifacts remain local under build/pixelfx-validation. Final all-effect acceptance,
OBJECT/cross-platform/performance gates and full act composition are still open.


Fire Ring is `fire/fire-ring` (224x128, origin 112/92, 3.9-second one-shot).
Its source default seed is 6 and reach is 92; reach accepts 20..200. The native
fixed-tick kernel owns expanding ring sites, charge streaks, flame tongues,
scorch, embers, smoke, and lights. Gallery key 0 compares defaults with seed 123,
reach 70, slower playback and a blue palette. Act targets and choreography remain open.

C++ compositors can call checked `drawToCanvas(canvas, layer, placement)` with
Back, Front or Both. Both draws back before front. The borrowed color/light planes
must be disjoint and exactly width*height (dimensions 1..4096, origins within
-4096..4096). Placement applies local x/y, flip and angle without changing simulation
or retained pixels. Drawing preserves existing target contents and never advances time.
Invalid input rejects before writes; a completed stop fade leaves the target unchanged.
Duration expiry alone still permits drawing the current source state.
This is the canvas primitive for composition, not the complete upstream Act lifecycle.

The pinned reference probe accepts --layer back/front/both and --canvas '[w,h,ox,oy]'.
Native placement flags are --x, --y, --angle, --flip; the reference accepts the
corresponding source --options. All 15 effects passed the same offset, flip and rotation
fixture on larger canvases: 810/810 complete raw color/light planes were byte-identical.


Arrow (`projectiles/arrow`, 112x32, origin 92/16, 0.6-second loop) procedurally
composes its shaft, three flutter poses, steel bevels, air streaks, eddies and glint.
The native projectile settings own wood and steel ramps (1..64 opaque colors,
normalized to four) and an opaque packed wind color. The primary ramp has four steps.
Energy Disc (`projectiles/energy-disc`, 120x52, origin 92/26, 0.5-second loop) owns
independent rotating tooth geometry, spiral arms, seeded filaments, tangent sparks
and light. Its active size is 3..16, teeth integer 2..32, speed 1..1000; the primary
ramp has seven steps. Inputs are resolved at cold preparation; raster passes use
fixed typed fields. Old configurations omit these new options and retain defaults.
The document schema/version and rejection/atomic-publication contract are unchanged.
Gallery P displays both defaults and material/shape/motion/palette variants.
Default and variant reference probes match 72/72 complete raw planes. All 17 effects
also match 918/918 transformed back/front/both raw planes.


Bullet Tracer (`projectiles/bullet-tracer`, 200x40, origin 184/18, 0.6-second loop)
procedurally draws a banded hot slug, cooling broken trace, sparks and a merged smoke
thread. Active length is 8..180, speed 1..2000 and smoke is normalized to five colors.
Pistol Muzzle (`projectiles/muzzle-pistol`, 96x72, origin 44/66, 1.8-second one-shot)
owns a fixed seeded flash, sparks, curling smoke and a bouncing brass casing. Height
accepts 1..60, brass has four colors and smoke seven. Shotgun Muzzle
(`projectiles/muzzle-shotgun`, 160x88, origin 44/82, 3.2-second one-shot) owns an
independent three-stage fan, ten pellets, heavy smoke and delayed red-shell ejection.
Height accepts 1..75, shell has five colors and smoke seven. Both use nine-color fire
ramps. All material inputs are opaque 1..64-color ramps normalized once at preparation.
The source height is the muzzle's distance above its local floor, not display scaling.

Both muzzle kernels advance only through injected 1/120-second ticks, own their RNG
and smoke/particle storage, and draw without changing simulation state. The shared
cloud renderer retains Float32 density accumulation and blur rounding at the same
points as upstream; scratch arrays are local to each explicit draw. There are no
retained caller/VM references, callbacks, process-global mutable scratch or GPU resources.
Gallery G shows tracer, pistol and shotgun defaults on the left and parameter variants
on the right. R restarts their owning timelines, SPACE pauses and S starts their fade.
Dense raw source checks cover every 120Hz tick of both one-shots, including brief flash
frames and delayed shell ejection: pistol default/variant each 434/434 planes and
shotgun default/variant each 770/770 planes match byte-for-byte.


SMG Burst (`projectiles/muzzle-smg`, 112x72, origin 56/66, one-second loop) is an
analytic twelve-shot emitter. Independent named RNGs reconstruct each shot's smoke,
flash lobes, sparks and bouncing casing without retaining frame history. Active height
is 1..60, brass is normalized to four colors and smoke seven; the primary ramp has nine.
Plasma Shot (`projectiles/plasma-shot`, 144x60, origin 108/30, 0.8-second loop) evaluates
a compact ball field for its churning core, necking tail and detached droplets, with
seeded corona arcs and velocity-stretched motes. Active size is 2..12 and speed 1..1000;
the primary ramp has eight steps. Both support seed, primary palette, playbackSpeed
and lightGain through the same prepared versioned configuration contract.

Gallery B compares defaults and parameter variants. Source verification covers every
120Hz loop sample: SMG default/variant each 242/242 raw color/light planes, plasma
194/194 each. All 22 effects also match 1188/1188 offset, flip and rotation back/front/both
planes. These are Windows native CPU comparisons against the pinned clean source clone.


Magic Missile (`projectiles/magic-missile`, 208x112, origin 24/104, 1.9-second one-shot)
uses a seeded native kernel for gathering motes, a weaving orb, a twisting ribbon,
a burst flare/ring and delayed stars. Height accepts 1..95, reach 1..170; the primary
ramp has eight steps. Its source endpoint contains 28 nonzero light cells. Native
rendering preserves them even while done() is true, and further accepted advances
continue the tail. stop() controls only its 0.3-second fade and leaves simulation
advancement explicit. Gallery M compares default and height/reach/seed/palette variants.

Default and parameter variant each match 458/458 full raw planes over all 120Hz samples.
Seven endpoint/post-duration samples through three seconds match 14/14 planes. All
six implemented one-shots match 84/84 endpoint/post-duration planes. The full 23-effect
transformed back/front/both suite matches 1242/1242 planes on the validated Windows host.
Preparing a simulation seek above one million ticks rejects before creating/replaying
state; duration expiry no longer bypasses that work bound. Analytic sampling retains
constant seek work and clamps one-shot evaluation to D. The common effect clock stays
within 1e12 even when render(seconds) applies playbackSpeed.


EveScript also offers getDurationTicks() and prepareSeekTicks(integer). The duration
getter returns the first 1/120-second tick at or after D. The prepared-tick entry accepts
0..120000000000000 ticks and uses the same native seek implementation, replay budget,
opaque candidate, cancellation and commitSeek ownership checks as prepareSeek(seconds).
It provides an exact frame boundary across the current SQFloat32 VM, where a fractional
seconds getter can round below D. Existing floating getters retain their VM precision;
no Squirrel dependency or ABI change is introduced. Preparing remains an explicit cold
operation with work proportional to replayed ticks, not a per-frame sampling shortcut.
On gallery M, E prepares each effect's duration tick and pauses, retaining source tail
pixels while isDone() is true. R restarts through the normal owning seek workflow.


Cannon Shot (`projectiles/cannonball`, 208x116, origin 48/102, 3.8-second one-shot)
advances a gravity-driven iron ball, emits smoke by traveled distance, and owns seeded
muzzle flash lobes, sparks, wadding and a curling gunsmoke volume. Height accepts 4..90;
iron normalizes to four colors and smoke seven. The primary ramp has nine steps.
Rocket (`projectiles/rocket`, 288x76, origin 252/42, 1.2-second loop) builds its steel
body, ogive nose, painted band, fins, nozzle and cardinal outline from a constexpr
geometry recipe. Drawing combines a flickering shock-diamond jet, torn flame tongues,
seeded smoke, embers and sparks. Speed accepts 1..1000; metal has seven colors, paint
five, smoke seven and primary palette nine. No baked texture frames are loaded.
Homing Missile (`projectiles/homing-missile`, 248x140, origin 160/120, 3.8-second one-shot)
owns a cubic flight path and heading, distance-based smoke emission, an oriented missile,
a single impact explosion, debris/grit, rolling smoke, scorch and split ground rings.
Metal normalizes to five colors and smoke seven; stone is an opaque packed color.
All use common seed, palette, playbackSpeed and lightGain with checked prepared configuration.

Gallery C displays cannon and rocket defaults/variants, H the homing missile pair.
Every 120Hz sample matches the pinned source: cannon and homing each 914/914 raw planes
per default/parameter configuration; rocket 290/290 each. All 26 effects match 1404/1404
transformed back/front/both planes. The eight one-shots match 112/112 endpoint and
post-duration planes. These native CPU comparisons were verified on Windows.


Signal Flare (`projectiles/flare`, 152x232, origin 76/226, five-second one-shot) owns
its rising flight, a single burst, swaying/sinking burning star, dripping sparks and
star-lit smoke plume. Height accepts 20..195; smoke normalizes to seven opaque colors,
primary palette eight. Gallery F shows default and height/smoke/seed/palette variant.
Small Explosion (`explosions/small-explosion`, 300x240, origin 150/196, 5.4-second
one-shot) owns fire/smoke, sparks, embers, burning brands, landed blazes, debris, grit,
scorch and front/back shock rings. Its typed explosion settings expose size 0.25..2
and opaque stone color; primary palette has eight steps. Gallery X shows default
and size/ground-material/seed/palette variant. This is an independent ground-blast
algorithm, not a scaled alias of another effect.

Both kernels receive fixed time/RNG and own all mutable state. Landed-brand handling
borrows its particle RNG only during update, without storing callbacks or pointers
between owners. The source's RNG-dependent brand-loop bound is evaluated in the same
order. Every 120Hz sample matches: flare default/variant each 1202/1202 raw planes,
small explosion default/variant each 1298/1298. All 28 effects match 1512/1512 transformed
back/front/both planes; ten one-shots match 140/140 endpoint/post-duration planes.
Comparisons use the pinned clean reference clone and current Windows native implementation.


### Big Explosion

`explosions/big-explosion` owns the source three-stage ground blast, rolling fireball,
climbing smoke column, molten rock trails, updraft, shock dome, scorch and landed
fires. Canvas 360x300, origin 180/262, duration 7.2 seconds, non-looping, seed 11.
The primary eight-step palette and opaque `stone` color are configurable, along
with common seed, playback speed and light gain. There is no size option in this
upstream recipe. All geometry, particles and light are generated at runtime.

Gallery V shows defaults on the left and a seed/stone/palette/playback variant on
the right. R restarts, SPACE pauses, S fades the current effects. The internal
plume and column keep owning state, receive fixed simulation time and RNG, and
retain no callbacks or external pointers. The shared debris integrator also handles
molten rocks; their heat/trail state belongs to the effect. Inventory: 29/244.


### Barrel Explosion

`explosions/barrel-explosion` owns the drum's shaking and bulging, seam flames,
explosion, thrown lid and plates, fuel slicks, burning splashes and ground effects.
Canvas 300x240, origin 150/204, duration 5.2 seconds, non-looping, seed 8.
The `barrel` ramp accepts 1..64 opaque packed RGBA colors, normalized to six steps.
The primary fire palette and common seed, rate and light controls also apply.

Gallery J shows defaults on the left and a blue barrel/fire, seed and rate variant
on the right. R restarts, SPACE pauses, S fades. All state and geometry are native
and procedural; the existing debris integrator owns plate motion and the effect
owns heat and lid dynamics. The material ramp is part of existing version-1 options;
omitting it restores source defaults. No additional persistent schema is introduced.
Current inventory: 30/244. Other effects and full Act composition remain open.


### Dirt Explosion and Implosion

`explosions/dirt-explosion` owns the buried charge, V-shaped dirt fountain,
bouncing/breaking clods, landing dust, settling plume and crater. Canvas 260x240,
origin 130/206, duration 3.6 seconds, non-looping, seed 3. Its seven-step primary
palette controls earth; the separate `fire` ramp accepts 1..64 opaque packed RGBA
colors normalized to eight source steps. Gallery K shows defaults and a material,
seed and playback variant.

`explosions/implosion` owns spiral grit, accelerating inward streaks, contracting
rings and void sphere, the trembling compressed core, still beat and outward pop.
Canvas 208x184, origin 104/92, duration 1.65 seconds, non-looping, seed 2. Its
seven-step palette and common seed, playback speed and light gain are configurable.
Gallery L shows defaults and a seed/palette/playback variant. Both algorithms retain
only owning native simulation state and use the same supplied fixed tick and RNG
contracts as the other effects. R restarts, SPACE pauses, S fades the selected page.
Current implemented inventory: 32/244; remaining effects and full Act composition
are not yet supported.


### Firework Burst and Firework Rocket

`explosions/firework-burst` owns the slowing shell climb, sphere of outer colored
stars and gold core, retained gold-to-color trails, drooping glitter and delayed
crackle pops. Canvas 240x248, origin 120/100, duration 3.6 seconds, non-looping,
seed 9. The seven-step primary palette controls colored stars; the separate `core`
ramp accepts 1..64 opaque packed RGBA colors normalized to seven steps. Gallery T
shows defaults and a seed/core/palette/playback variant.

`explosions/firework-rocket` owns ignition, rocket climb/coast, heavy palm fronds,
thick cooling trails, glitter and tip crackle. Canvas 300x300, origin 150/288,
duration 3.8 seconds, non-looping, seed 12. Its primary palette has seven steps;
`tips` accepts 1..64 opaque packed RGBA colors normalized to six steps. Gallery U
shows defaults and a seed/tips/palette/playback variant. Both accept common playback
speed and light gain. R restarts, SPACE pauses, S fades the selected page.

Every star/frond owns its trail history. Palette selection is a value flag, with
no caller memory or retained borrowed palette pointer; fixed time and named RNG
remain effect-owned. Existing version-1 configuration options encode both ramps.
Current implemented inventory: 34/244; remaining effects and full Act composition
remain open.


### Plasma Explosion and Cartoon POW

`explosions/plasma-explosion` owns the boiling/hollowing plasma shell, tilted split
ring, shock rings, per-strike branching lightning, motes and sparkles. Canvas
224x200, origin 112/100, duration 1.7 seconds, non-looping, seed 7. Its primary
palette has seven steps; `accent` accepts 1..64 opaque packed RGBA colors normalized
to five steps. Gallery O shows defaults and a seed/accent/palette/playback variant.

`explosions/cartoon-pow` owns the jagged boiling burst, slam/shake/squash timing,
letter-by-letter extruded pixel text, action lines and snap-away stars. Canvas
184x144, origin 92/72, duration 1.05 seconds, non-looping, seed 5. The primary
palette has seven steps. Its owning `text` option defaults to `POW!`, accepts
0..64 Unicode scalar values in strict UTF-8, and uses the original big pixel font.
Characters outside that font retain the source blank spacing. Gallery Y shows
`POW!` defaults and a `BAM?` seed/palette/playback variant.

Set text through getConfiguration, change value.options.text, prepareConfiguration
and commitConfiguration; each Result must succeed. The existing v1 Value schema is
unchanged. Parameter descriptions expose type `string`, UTF-8 encoding, length unit
`unicode-scalars` and limits 0/64. Empty text is valid; malformed encodings, wrong
Value types and excess length reject atomically. Prepared text and per-letter
layout are owning; no script string or temporary buffer survives as a borrowed
pointer. Both effects accept common playback speed/light gain; R restarts, SPACE
pauses and S fades the selected page. Current inventory: 36/244; remaining effects
and full Act composition are still open.


### Grenade and Smoke Bomb

`explosions/grenade` owns lob/bounce/roll, the turning pineapple body and fuse,
last-moment shudder, shrapnel, fireball, smoke and scorch. Canvas 300x200, origin
160/170, duration 4.2 seconds, non-looping, seed 6. Its primary palette has eight
steps; `body` accepts 1..64 opaque packed RGBA colors normalized to five steps.
Gallery N shows defaults and a seed/body/palette/playback variant.

`explosions/smoke-bomb` owns the canister's calibrated throw, bounce, fuse and pop,
rolling low smoke, rising core, depth-sorted cel-shaded cloud, thinning wisps and
spent canister. Canvas 260x176, origin 130/156, duration 5 seconds, non-looping,
seed 3. Primary palette has seven steps; `canister` normalizes to five and `sparks`
to four. All accept 1..64 opaque packed RGBA colors. The four-step spark rule
matches the upstream public resolveOptions path, including one/three/five-color
inputs. Gallery D checks a five-color input publishes as four steps through the
actual configuration transaction. Common seed/playback speed/light gain apply;
R restarts, SPACE pauses and S fades the selected page.

All motion, canister/body geometry and smoke lobes are native and owning. Bounce
callbacks are synchronous and not retained; simulation time and RNG are explicit.
Current inventory: 38/244; remaining effects and full Act composition remain open.


### Nuke

`explosions/nuke` owns the blinding wash, lifting torus cap, inner glow/dark hollow,
thin feeding stem, condensation veil/bell, floor dust surge, afterwinds and late
rim fraying. Canvas 400x320, origin 200/284, duration 9 seconds, non-looping, seed 5.
Its primary palette has eight steps; `dust` is an opaque packed RGBA color.
Common seed/playback speed/light gain apply. Gallery Q shows defaults and a
seed/dust/palette/playback variant. R restarts, SPACE pauses and S fades.

Fray callbacks borrow RNG and the callback only during the current fixed step;
neither is retained. Their RNG expression order follows JS explicitly. The original
haze and bounded shock light use the same transformed painter and caller-owned
canvas contract. No postprocess correction or source-specific pixel replacement.

The approved narrow floating raster tolerance is exercised here: this Windows
build's exp differs from Node by one ULP for some cap heights. At an independently
traced sample all 124 column particle states match exactly; cap y differs by
2.84e-14 pixels, affecting a few blended RGB low bits. Across 1081 frames, defaults
have 12 RGB pixels differing by one level and the variant has 18. Coverage, alpha
and light are exact. Strict comparison remains the default; explicitly selecting
`--color-tolerance 1` accepts only one RGB level with identical coverage/alpha and
strict light. Reports retain exact=false, every difference and its bounds.
Current inventory: 39/244; other effects and full Act composition remain open.


## Water ripples and rain splash

`water/ripples` owns the dark translucent pond surface, slope-lit expanding rings,
back/front crest trains, rebound beads and seeded glints (136x52, origin 68/30,
two-second loop). `water/rain-splash` owns eight seeded drop locations and crown
phases, ten-finger crowns, detached ballistic beads, rebound jets, interference
between surface swells, falling fine rain and a wobbling lamp reflection (176x80,
origin 88/50, 2.4-second loop). Both are analytic native algorithms with the source
8-step water palette and `seed`; common playback speed and light gain also apply.
No source actor is required. All configuration is owned and validated through the
existing v1 codec; palette inputs contain 1..64 opaque colors and resolve to eight.

The internal water helpers borrow ramps only during the synchronous draw and retain
no state, pointers or callbacks. Work scales with surface cells and crest circumference;
reuse caller-owned target planes across frames. Time and RNG come from the owning
PixelFx instance. Back/front layers and external placement use the canonical painter.

Gallery W shows both effects, with defaults on the left and palette/seed/playback
variants on the right. Analytic and simulated gallery entries now both use their own
checked advance/draw clock, so pause, stop/fade and restart apply to every page.

Independent 120Hz comparisons cover all 241 ripple and 289 rain frames, including
loop endpoints, for defaults and seed/palette variants. Ripples is byte-exact.
Rain's translucent surface mixing has at most one RGB LSB difference; coverage,
alpha, opaque crests/reflections and all light remain exact. A single-cell trace
measured native hypot 46.818556683174933 versus Node 46.818556683174926, propagating
through the swell field to blend alpha 0.11865727087442117 versus
0.11865727087442217. The explicitly selected RGB-only comparison contract retains
these raw differences and exact=false; the renderer does not correct pixels.


## Drip and underwater bubbles

`water/drip` owns the wet ceiling dripstone, swelling and stretching release bead,
falling tail, translucent puddle, ballistic crown flecks, rebound bead and two
layered ripple trains. Its 72x88 logical canvas has origin 36/78 and loops over
1.6 seconds. `height` is a finite 1..256 logical-pixel release height (default 62);
`source` is a strict boolean (default true) controlling dripstone drawing. The
source recipe declares seed 1 but does not use it to randomize this deterministic
drip; palette, height, source, playback speed and light gain provide variation.
Use a larger caller canvas when a large release height exceeds the logical frame.

`water/bubbles` owns a three-mouth pebble vent, seeded rising bubbles that expand
and wobble, translucent bubble interiors with shaded rims, highlights and
refraction flecks, surface domes and pops, drifting specks, light shafts and floor
caustics. Its 104x112 canvas has origin 52/104 and loops over four seconds. Seed
(default 4), eight-step water palette and common playback/light controls apply.
Both effects use native analytic draw/back functions and the canonical local
placement and caller-plane composition; no actor or JavaScript runtime is needed.

`PixelFxWaterSettings` owns height/source. Native, JSON and script configuration
use the existing v1 preparation/commit transaction. Boolean metadata reports
`type: "boolean"` and a boolean default, with no numerical bounds; numeric 0/1,
strings and null are rejected at `/options/source` before publication. Missing
options use defaults. The numeric legacy `setParameter` remains numeric; set source
with a boolean in `getConfiguration().value.options` and prepare/commit the edited
configuration. The A page performs this transaction and verifies the published
script boolean and height. It shows a default dripstone on the left and a lower
source-free drip on the right, with default/seeded bubble variants beneath.

The algorithms borrow ramps only during drawing, retain no callbacks and derive
time/RNG from the owner. Per-frame work scales with water cells, active bubbles,
crest circumference and filled bubble cells; reuse the target planes. Independent
120Hz raw comparisons cover defaults and palette/seed/height/source variants plus
a fractional low release height. All 3082 color/light planes match byte-for-byte,
including alpha and loop endpoints, without a tolerance flag.


## Simulated water splash

`water/splash` owns the incoming stone, white surface hit, scalloped crown with a
rolled lip and twelve tearing fingers, ballistic droplets, secondary landing
crowns, rebound jet, noise-field foam, soft mist and late air bubbles. Its 176x100
logical canvas has origin 88/78, duration 2.5 seconds and one-shot playback.
Seed (default 3), eight-step water palette and common playback/light controls
apply. `stone` is a strict boolean (default true); false hides the incoming stone,
while the source's collision, crown and air-release simulation still runs.

`PixelFxWaterSettings::stone` uses the existing typed preparation/commit codec.
Gallery Z displays defaults beside a seeded palette/rate variant with stone=false.
The effect uses 120Hz fixed updates, owning drop/ripple/mist/particle storage and
compiled one-shot cues; no frame-time string lookup or runtime JS is involved.

Water drops update in reverse insertion order. A landing callback receives an
independent local snapshot after the parent is removed, may append children and
must not otherwise mutate or reenter the updater. New children start moving on
the next tick. All callback/RNG borrows are synchronous; no reference escapes.
The crown's bounded numeric cells preserve source insertion order and replace a
cell only when the new rank is strictly greater. Translucent sheet cells blend;
bright crests write hard pixels. The fixed source geometry bounds this temporary
scratch storage to [-32,32) in each axis, before external placement is applied.

Work scales with live water objects, crest circumferences and covered sheet/mist
cells; reuse target planes and prepared configurations. Independent default and
variant sequences cover every 120Hz frame including the one-shot endpoint: all
1204 color/light planes match byte-for-byte, without tolerance. Physics fixtures
separately check delayed activation, landing order, child timing, ripple age and
mist damping; full spatial comparisons also cover these states' rendered output.


## Fountain and water orb

`water/fountain` owns a stone basin and spout, separate rear/front basin rims,
translucent pool with caustic light, twelve seeded parabolic arcs, streaming beads,
column bloom, seeded glints, landing rings/crowns and fine mist flecks. The near
basin rim is drawn last over the falling arcs and pool. Its 120x104 logical canvas
has origin 60/96 and a two-second loop. Seed (default 3), eight-step water palette
and common playback/light controls apply. The source's asymmetric clamp before
basin-joint asin is preserved, rather than changing the authored stone pattern.

`water/water-orb` owns a wobbling translucent sphere, rim and refracted highlights,
three tilted internal currents, seeded bubbles, seven back/front orbital motes,
floor caustic, pulsing focal glint and repeated belly drips with landing rings.
Its 100x108 canvas has origin 50/100 and a three-second loop. `radius` defaults to
19 logical pixels and accepts finite 2..40; seed defaults to 2. The bounds keep
the skin radius positive and the release point above the water throughout the
cycle, so the analytic landing square root stays finite. Palette/playback/light
remain independently configurable. This is a procedural pixel lens appearance;
no scene texture or graphics object is retained by the CPU effect.

Both algorithms use analytic source time, stack-owned arc/orbit samples and the
canonical painter for larger caller canvases, placement and back/front layers.
They borrow palette/target spans only during drawing and retain no callbacks or
external clock/RNG. Work scales with basin/orb cells, arc segment counts and
crest circumference; reuse prepared configuration and target planes across frames.
`PixelFxWaterSettings::radius` is decoded only for active water-orb declarations;
adding it to a fountain configuration is an unknown-option error.

Gallery I shows fountain defaults and a seeded palette/rate variant above default
and larger-radius water orbs. Independent complete 120Hz comparisons cover fountain
defaults and variant (241 frames each), plus water-orb defaults, radius/seed/palette
variant and both radius bounds (361 frames each). All 3852 color/light planes,
including alpha and loop endpoints, match byte-for-byte without tolerance.


## Whirlpool and searchable gallery

`water/whirlpool` owns a log-spiral water field with differential winding, three
interfering foam streak sets, a deep eye with lit far wall, foam on the eye rim,
seeded inward flecks, conditional glints and cool surface light. Its 128x84 logical
canvas has origin 64/42, duration three seconds, loop=true and seed=5. Source palette,
seed and common playback/light controls apply. It is an analytic native recipe,
with synchronous borrowed palette/target storage and supplied time/RNG; no runtime
JavaScript or external scene/graphics pointer is retained. Work scales with surface
cells and seeded live flecks; reuse prepared inputs and target planes.

Defaults match every raw 120Hz plane strictly. A five-color resampled palette
variant deliberately exercises repeated adjacent ramp colors. Native/Node hypot
rounding differs at (-4.5,-29.5): 0.82286948997448195 vs 0.8228694899744821; its
secondary blend alpha is 0.012283735476285531 vs 0.012283735476285371. Reblending
identical channels may therefore truncate 112 to 111. The explicit RGB-only
one-LSB comparison contract preserves the raw differences and exact=false;
coverage, alpha and all light must still match exactly. No pixel correction or
replacement math implementation is used by the renderer.

The native gallery now has an on-screen ID search. Press Tab (or /) to start
editing; type an ID substring, including category separators. Up/Down cycles
matching IDs, Enter/Tab returns to playback controls, and Escape leaves editing;
Escape again restores the previous page layout. In catalogue playback, Space
pauses, R restarts and S fades only visible instances. Existing page keys restore
their original layouts. No-match filters hide every effect and show a clear message.

Search IDs are sorted/deduplicated from the actual created effect entries, rather
than maintained in another registry. A selection shows its default and parameter
variant on equal fitted canvases, with ID, match count, dimensions, duration,
loop/one-shot and analytic/simulation labels. Inactive effects do not advance.
The HUD uses the existing licensed DejaVu font asset and Font/Graphics bindings;
its notice ships beside the example. Effect planes remain procedural and owned
independently of this presentation font. Additional authoring controls and the
remaining native effects are still part of the full integration goal.


## Pressurized water jet and waterfall

`water/water-jet` owns a banded pressurized core with traveling surges, periodic
roughness/gaps, a nozzle flare, broken beads, ballistic spray and soft mist.
Its 188x80 canvas has origin 14/32 and a one-second loop. Length defaults to 118
logical pixels and accepts finite 8..512, including fractions; seed defaults to 3.
Palette and common playback/light controls remain independent. Use a larger caller
canvas to inspect the far end of streams longer than the original logical frame.

`water/waterfall` owns a river curl, wet boulder outcrop and cracks/trickles,
accelerating sheet with translucent back lanes and split white strands, churning
foam/billows, ballistic landing spray/rings, drifting mist, pool caustics and a
broken reflection. Its 144x136 canvas has origin 72/118 and a two-second loop.
Height defaults to 92 (finite 32..256), half-width to 13 (finite 1..40), ledge to
true (strict boolean), seed to 5. Height and width both accept fractions. Large
heights may extend beyond the original frame; the caller-canvas API remains valid.

The waterfall's rock IDs and Float32 lighting are immutable configuration-owned
geometry. The canonical prepare/commit operation builds this map once; draw and
analytic seek share it through the immutable program. It is not a simulation and
large analytic seeks do not replay fixed steps. Ledge=false prepares no outcrop
map. No global string cache, duplicate clock, external pointer or callback is
retained. Source typed-array index/undefined behavior for fractional geometry is
represented explicitly with optional cells, not silently rounded into a different
rock map. Width=13.25 and height=64.25 are included in independent full-frame proof.

Other effects retain their compiled direct-draw/simulation routes. A prepared
analytic descriptor owns its sole draw route, guarded at cold construction. Old
seek tokens retain their old program safely but reject publication after a new
configuration; cancellation/destruction releases their owned geometry. Time and
RNG remain supplied by the owner; per-frame work scales with drawn sheet, pool,
foam and mist cells, while outcrop preparation scales with the bounded six-lump
map once per configuration. No per-frame ID lookup is required.

Search water/water-jet or water/waterfall in the native gallery. The right stream
is shorter; the right waterfall changes height/width/seed/palette/rate and hides
the ledge. Default and meaningful/fractional variants compare strictly against
the pinned source for every 120Hz sample through the loop endpoints.

## Breaking wave

`water/wave-crash` is an owning fixed-step simulation (224x104, origin 112/86,
3.2 seconds, one-shot, seed 6). A swell steepens, throws a shaded curled lip,
then crashes into ballistic spray, foam billows, rings and drifting mist. The
local numeric cell table retains the source lip's insertion/replacement order.
Each instance owns its clock, RNG, droplets, particles and transient foam;
rendering borrows the target synchronously. No actor or external clock is retained.

The eight-color water ramp, seed, playback speed and light gain are configurable
through the existing checked configuration transaction. The right gallery variant
changes seed, ramp, speed and light. Search `water/wave-crash`; pause, restart and
stop controls apply to both variants. Duration completion is advisory; simulation
can continue until emitted remnants disappear, while explicit stop applies the
shared 0.3-second fade.

The pinned upstream reference and native implementation match strictly across
385 fixed-step sample times for both defaults and a changed seed/five-color ramp:
1540 complete color/light planes, including transparent pixels. No tolerance is
used for this effect.

## Geyser

`water/geyser` completes the twelve water effects. It owns a fixed-step
simulation: 184x168, origin 92/146, five-second one-shot, seed 4. The pool builds
with burbles and short spurts, erupts into a banded column and churning foam
head, sheds droplets and steam, then slumps and sputters out. All transient
state, time and RNG belong to the instance; painter and landing callbacks are
borrowed synchronously with no retained external pointers.

`height` is a finite numeric option from 20 to 256, default 100; fractional
heights are accepted. The source canvas stays fixed, so high columns may extend
outside it; use a larger caller-owned canvas to observe them. Seed, fitted
eight-color palette, playback speed and light gain use the existing checked
configuration transaction. Search `water/geyser` in the gallery; the right
variant changes height to 72.5 together with seed/ramp/rate/light.

Defaults, a fractional-height/seed/five-color variant and both height boundaries
each match 601 independent fixed-step reference samples: 4808 complete
color/light planes strictly identical. The source half-pixel row below the vent
evaluates a negative fractional power; its NaN comparison semantics are retained
explicitly, including Math.max propagation. No raster tolerance or correction
image is used.

## Meteor

`fire/meteor` completes the twelve fire effects. This owning fixed-step
simulation is a five-second one-shot (272x232, origin 150/208, seed 9). The
burning rock accelerates from the upper left, sheds a comet of flame/smoke,
then impacts in a flash, expanding ring, fireball, debris and burning brands.
The exposed core and crater cool and fade. Instance-owned RNG, time,
particles, debris and blazes retain no external pointers. Landing callbacks
are borrowed synchronously; drawing does not mutate the simulation.

The nine-color flame `palette`, opaque `stone` ground color and four-step
`rock` ramp are configurable. Input ramps accept 1..64 opaque packed colors
and normalize at the cold configuration boundary; empty or translucent
materials fail atomically. Existing seed, playback speed, light gain and
prepared seeking apply. Search `fire/meteor`; the right variant changes
seed, fire palette, rock material, playback rate and light.

Defaults and a seed/five-color-flame/stone/three-color-rock variant each cover
601 fixed-step times: 2404 strictly identical complete color/light planes. Flying flame offsets are explicitly
sequenced to preserve JavaScript RNG order across C++ compilers. No source
JavaScript executes in the engine.

## Layered snowfall

`weather/snow` is an analytic twelve-second loop (320x180, origin 160/170,
seed 6). It includes cold haze, far specks, drifting flakes, rotating near
crystals, periodic gusts, settling/melting flakes and a sparkling snow bank.
The eight-color palette, seed, playback rate and light gain are configurable
through the existing atomic configuration interface. No external time/RNG or
actor references are retained. Search `weather/snow`; the right variant changes
seed, palette, rate and light.

Weather writes are clipped to the source half-open canvas in local coordinates,
including color, blends and light. Rotation tests inverse-transformed screen
cell centers; the unrotated path retains upstream rounded rectangle bounds.
The borrowed clipped painter is a value and leaves its original view unchanged.
Clipped glows traverse the bounded local cells and transform their light writes.
The per-pixel weather pass uses the same primitive write path for unrotated
and rotated views. Existing effect painters have clipping disabled by default.

Default and seed123/five-color variants each compare 241 independent time
samples: all 964 complete color/light planes match strictly. The owning weather suite also covers
loop/cadence, seed/palette normalization and rejection, huge analytic seeks,
stop fade and transformed color/blend/light write boundaries.

## Fog banks and moonlit clouds

`weather/fog` is a twelve-second analytic loop, 320x180 with origin 160/170,
seed 5. Three noise-shaped banks drift and boil at different depths; translucent
bands, lit crests and faster front streaks retain the source Float32 arithmetic.
Five immutable noise maps are prepared with the configuration. Work scales with
map area and octave count at preparation; frames and prepared seeks share them.
There is no global map cache or mutable shared row storage.

`weather/clouds` is an independent twelve-second analytic loop, 320x180 with
origin 160/180, seed 7. Three layers of growing/drifting/fading puff volumes pass
a shaded moon. Silver top edges, inner lobes and illuminated moon crossings are
procedural. Weather clipping now derives local bounds from each effect's own
metadata, including the different cloud origin. Snow uses that same path.

Both expose their eight-color palette, seed, playback speed and light gain via
checked configuration transactions. Search their full IDs in the gallery; the
right variants change seed/palette/rate/light. Caller-thread drawing borrows
targets synchronously and retains no callbacks or external actor/clock data.
Defaults and seed123/five-color variants each match 241 independent time samples
for both effects: 1928 strict complete color/light planes, without tolerance.
Independent Float32 map hashes and wrapped sample fixtures check accumulation,
normalization and bilinear sampling.

## Sun shafts and aurora

`weather/god-rays` is a ten-second analytic loop (320x180, origin160/170,
seed3). Six breathing and swaying light shafts contain Float32 fine strands,
posterized gold/white light, floor pools, a sun glare and drifting dust motes
that become visible inside the beams. Its seven-color palette, seed, rate
and light gain use the existing checked configuration interface.

`weather/aurora` is a twelve-second analytic loop (320x180, origin160/180,
seed3). Three folded curtains accumulate green-to-violet ray light, bright
hems and selected thin strands over a twinkling starfield, with a two-pass
Float32 bloom. Its immutable noise map/profile are prepared once per program;
intensity, hue, blur scratch and ray records belong to one synchronous draw,
with no shared mutable globals. Scratch scales with logical area per draw.
The separate `upper` material accepts 1..64 opaque packed colors and fits seven
steps. Empty/translucent entries reject atomically. Search either full ID;
the aurora variant also changes its upper hue ramp.

Sun shafts default/variant cover 201 independent samples each: 804/804 strict
color/light planes. Aurora default covers 241 samples: 482/482 strict planes.
The seed123/five-color-green/three-color-upper variant is 481/482 strict and
482/482 accepted under the approved RGB-only tolerance of one. At t3.6 two
pixels differ by one RGB channel; all alpha, coverage and every light plane
remain exact. Source/native peak x, hem, height, envelope and sampled noise
match. The breathing coefficient differs by one ULP: V8 0.90815965515267139,
native 0.90815965515267127; strand alpha then differs by about 5.6e-17 before
integer color truncation. The raw differences remain in local reports. No
image correction, native math bias or broad tolerance is applied.

## Layered rain

`weather/rain` is an eight-second analytic loop (320x180, origin160/170,
seed4), with four depth layers of slanted two-tone streaks. Far drops fade
in the air; close drops catch light, land in crowns or puddle rings and throw
small droplets. Wet floor strips, sliding glints, overcast haze and drifting
low mist complete the source recipe. Two immutable gust/mist fields and five
pool strips are prepared from the seed; drawing and seeks share them.

`density` is a finite multiplier from0 to4, default1; fractional values round
each source layer's slot count. Density0 retains haze, wet floor and mist.
The eight-color palette, seed, rate and light use the existing checked
configuration path. Invalid/NaN density rejects before publication. Search
`weather/rain`; the paired variant uses density0.6 and changed seed/palette/
rate/light. Source time quantization to1/30000 second is retained.

Defaults, seed123/five-color/density0.6, density0 and density4 each match241
independent reference times:1928/1928 strict complete color/light planes.
The shared crown helper additionally matches21 independent complete rasters
and lifetime results, covering small/large crowns, droplets and expiration.
No rain tolerance, baked frame or source JavaScript runtime is used.

## Blizzard and shooting stars

`weather/blizzard` is an eight-second analytic loop (320x180, origin160/170,
seed9). Three layers of nearly horizontal two-tone snow streaks cross the
canvas at independent rates. Gusts stretch/brighten them, Float32 wind sheets
produce a whiteout, and fast spindrift/grains stream off a shaded snow bank.
Three immutable noise fields are prepared with the configuration and shared
by draws/seeks; column scratch is local to each pass.

`weather/shooting-stars` is a ten-second analytic loop (320x180, origin160/180,
seed4). Seven source events include burning heads, cooling/twisting trains,
large flares, scattered glints and fragments. Trains cross loop boundaries.
The starfield includes a dim milky band and twinkling stars. `sky` accepts
1..64 opaque colors and fits five steps; the effect palette fits six. Empty
or translucent sky materials reject atomically. Both effects expose seed,
palette, playback rate and light. Search their full IDs in the native gallery;
the shooting-star variant also changes the sky ramp.

Defaults and seed123/five-color variants (with a three-color sky for the
shooting-star variant) match the original30Hz samples:241 samples each for
blizzard,301 each for shooting stars,2168/2168 complete color/light planes
strictly identical. No new tolerance or image correction is used.

## Autumn leaves and cherry blossoms

`weather/falling-leaves` is a ten-second analytic loop (320x180, origin160/170,
seed8). Beech/maple/willow shapes follow source pendulum paths, with faster
drop through the swing, tilt/flip, pale backs, outlines/midribs and occasional
glints. Landed leaves rest and fade over a prepared litter. `reds` and `golds`
are separate material ramps; each accepts1..64 opaque colors and fits six,
as does the main palette. Invalid/empty materials reject atomically.

`weather/cherry-blossoms` is a ten-second analytic loop with the same canvas,
seed12. Near/far notched petals spiral, flip, flash, land and shrink. Whole
five-petal flowers spin around a gold center. The plum haze and fallen scatter
retain source shading; palette/seed/rate/light use the existing configuration
path. Search each full ID in the gallery; the leaf variant also changes red/
gold materials, and both paired variants change seed/palette/rate/light.

Litter and scatter are computed from seed/material during cold preparation as
ordered local pixel records. Draws replay that exact overlap order through the
clipped/rotated painter, rather than flattening it into a differently ordered
image. The immutable program owns the records; no global cache or source
JavaScript runtime is retained. They are procedurally generated, not imported
frame assets. Dynamic leaves/petals still evaluate their full analytic paths.
Defaults and seed123/five-color variants (with additional red/gold materials
for leaves) each cover301 original30Hz samples:2408/2408 complete color/light
planes strictly identical.

Fireflies evaluates all three flight depths, independent one/two-flash rhythms,
wingbeats, lantern glints, short light trails and two swaying meadow rows.
The owning `grass` option accepts1..64 opaque colors, fitted to six source tones;
configuration preparation validates it before publication. Flight and grass-tip
lighting use supplied time and seed hashes without a mutable global RNG.
Search `weather/fireflies` for paired default/material variants.

Storm completes the twelve weather effects. It includes the stepped leader,
first stroke/restrike, branch sheaths and cores, cloud-base crawler, sheet/far
lightning, lit cloud rolls, two rain depths, splash droplets, wet reflections
and the ground strike spray/steam. The owning `bolt` ramp accepts1..64 opaque
colors fitted to six source tones through ordinary configuration preparation.

Noise fields and seeded bolt paths are prepared once per immutable program.
Drawing borrows the target synchronously; active ground-strike frames rebuild
a private particle set for at most1.8 seconds of source120Hz replay. This is
more expensive than an ordinary analytic draw but bounded independently of
absolute seek time; no global cache or mutable retained replay is introduced.
Search `weather/storm` for paired default/bolt-material variants.

Spark arc implements a climbing Jacob's ladder between hot steel rods: ribbed
insulators, bowed/restruck arcs, root nodes, top snap, anticipation, ozone and
ballistic molten sparks. Ball lightning implements a wandering layered orb,
cooling wake/motes, corona/crackles and the floor zap with bouncing sparks.
Both expose `sparks` through the ordinary configuration interface:1..64 opaque
colors are fitted to four source steps, matching upstream resolveOptions.

Lightning inking preserves first-hit order and per-pixel halo/body/core priority.
Each call owns a grid clipped to the source1024-square domain and the path
bounds; no mutable global scratch or target pointers are retained. Cost scales
with path traversal and its clipped bounds rather than the full shared grid.
Search each complete ID for paired seed/palette/sparks/rate/light variants.

Plasma ball implements a glass globe with wandering/restruck front/back filaments,
contact fingers, electrode, shell reflections and illuminated pedestal. `tendrils`
is an integer in0..64; zero keeps the globe and light with no filaments.
Thunderclap implements the anticipation, body flash, eight branching bolts,
landing scorch, expanding floor/air rings, dust/grit and plasma spray. `reach`
is8..512 logical pixels; `sparks` fits four source steps. Its actor defaults to
Dummy and accepts the shared Hero/Custom/None snapshot contract. None explicitly
omits the body flash; no body pixels or callbacks are retained from callers.

These are the effect-plane implementations. Full source Act poses, hit-stop,
target reactions and presentation composition remain part of the ongoing plan.
The gallery shows the stock body on the left and a custom body on the right
for thunderclap, alongside the parameter variants.

Zap hit includes the incoming contact arc, overload knot, growing Lichtenberg
fern, kinked sparks with cooling trails and ozone/strobing light. Lightning
strike includes the charging sigil/night veil, stepped leader/streamer, three
strokes/darts, heated ground cracks, debris/dust, crumbling channel and scorch.
Both use owned120Hz simulation state and source seed streams; palette, seed,
playback speed and light gain use the common configuration API. Lightning
strike also exposes the four-step `sparks` material. Search their complete IDs
for paired defaults and seed/palette/sparks/rate/light variants.

Tesla coil includes shaded copper windings/steel torus, front/back arc volleys,
corona/brush discharges, floor zaps, scorching, smoke and bouncing sparks.
The torus Float32 depth/normal projection and seeded event plan are owned by
the immutable prepared program, built once per configuration and reused by
draws/seeks. No shared mutable map is retained. `copper` and `steel` materials
fit six/seven source tones; `sparks` fits four.

Storm cloud includes breathing/fusing billows, ragged scud, discharges lighting
the cloud from inside, slanted lit rain/splashes, belly crawler, floor bolt and
sparks. Its density/gradient/internal-light fields are call-owned Float32
values, preserving each source accumulation without shared mutable scratch.
`cloud` and `rain` fit seven/four tones; `sparks` fits four.1..64 opaque colors
are accepted per material. Both effects use supplied time and source seed/hash
streams; preparation/temporary fields retain no borrowed material or painter.
Search their full IDs for paired source defaults and material/seed variants.

Electrified traces the configured actor silhouette with a broken aura, edge-to-
edge arcs, three periodic palette-relit jolts, grounded feet and spark sprays.
The prepared program owns its edge samples; actor replacement rebuilds them
atomically. Dummy/Hero/Custom use the common owning actor snapshot contract;
None and an empty custom silhouette explicitly produce empty effect planes.
This absent-actor behavior is a native contract; upstream requires nonempty
actor edges. `sparks` fits four source colors. The gallery pairs stock/custom
actors and material/seed/rate/light variants. Source Act poses remain pending.

Charge up includes inward spiraling motes/arcs, an accelerating pulse and beat
rings, an edge-lit actor, layered core/corona, release flash/eight bolts,
air rings and spark/star/smoke spray. `charge` accepts0.05..8 seconds and
`sparks` fits four colors. The fixed2.7-second metadata duration is advisory:
long charges continue simulating and release after it. The owning actor
contract supports Dummy/Hero/Custom; None omits edge/body relighting and mask
flash while retaining the independent core and release effect. Source Act
pose/hit-stop/dust presentation remains pending.

The simulation owns its actor/edges, RNG, pending arcs/beats, particles and
lights. Parameter/actor changes publish a newly prepared simulation atomically;
old seek candidates reject. Search `lightning/charge-up` for stock/default
and custom/short-charge paired variants.

Chain lightning implements the source three-hop chain from the caster hand
through fixed local nodes, sequential hot-head leaps, restruck bowed links,
node flashes/crawlers, grounding sparks/scorch and stuttering break/pop/smoke.
Palette, seed, four-step `sparks`, playback and light gain are configurable.
The source node positions/timing are retained; they are visual nodes, not
engine gameplay targets or damage ownership. The actor snapshot supports
stock/custom/None presentation; it does not alter the source link simulation.
Full Act caster poses and target reactions remain pending. Search its full ID
for paired default/custom-actor and material/seed/rate/light variants.

### Rotating ice crystal

`ice/ice-crystal` is an analytic three-second loop. A projected six-sided prism
turns through one sixth of a rotation per loop; visible facets, silhouette rim,
diffuse lighting and specular glints are computed from its geometry each frame.
Back rendering owns the floor shadow/cold pool and rear motes; front rendering
owns the crystal and foreground motes. No frame atlas is involved.

The eight-step palette, seed, playback rate and light gain use the checked
configuration API. Source `height` (default34, accepted0..256 logical pixels)
sets the floating center; bobbing is evaluated from injected time. Native typed
settings own `ice.height`. Invalid values reject before publication. There is no
actor option in this source effect. Search the full ID in the gallery to compare
the default with a changed palette/seed/height47.5/rate/light variant.

### Snowflake burst

`ice/snowflake-burst` owns a120Hz one-shot simulation: inward frost gathers,
then a white pop/ring/flare throws out seeded six-arm snowflakes, frost motes
and delayed stars. Each large flake owns its branch/plate/fork design, decaying
spin, drag, drift and twinkle phase. Flakes settle on the floor and shrink into
melting glints. Geometry is generated procedurally, with source10-degree turn
quantization. There are no imported frames or actor dependencies.

Source `count` (default9) accepts integral1..128 at configuration preparation,
with atomic rejection of invalid or fractional values. Typed settings own
`ice.count`; seed/palette/rate/light use the existing checked interface. The
eight-step palette also derives particle materials. The native gallery pairs
defaults with count13 and changed palette/seed/rate/light. The3.2-second duration
is metadata; particle life is trimmed using the source endBy rule rather than
freezing simulation at that time.

### Ice lance and faceted blade shading

`ice/ice-lance` is a0.8-second analytic loop. A faceted spear carries a moving
ridge pulse/top glint, three shaded fletching blades and shearing rings; cold
light, peeling soft puffs, tumbling chips and speed lines form its wake. All
geometry and particle phases are generated natively from the injected time and
source RNG. There is no actor option or frame atlas.

The source `length` parameter defaults to40 and accepts16..256 logical pixels,
including fractions. Seed/palette/rate/light use the checked owning configuration
API. The gallery variant sets length55.5 and changes seed/palette/rate/light.
Shared internal ice blade shading preserves the source signed32-bit grain,
facets, notch, glint, dimming, white flash and alpha; inputs are synchronous
borrows and no shading callbacks survive a draw.

Windows/Node reference measurement found a one-ULP difference in `exp` for the
closed-form puff drag: identical input at age0.09583061238129931 gives
0.8099138886797865 in V8 and0.80991388867978664 in the Windows CRT. Centers differ
by about1e-14; translucent blending can truncate one RGB unit differently.
Reference reports retain strict differences and accept only RGB<=1 with exact
coverage/alpha/light, as specified in the migration plan. No picture correction
or global comparator relaxation is used.

### Ice spikes and owning panes

`ice/ice-spikes` owns a120Hz one-shot simulation. A running frost head grows
clusters of jagged blades into a crest, a glint travels along them, then the
blades tremble, flash white and split into toppling pieces. Falling panes tumble,
bounce, skid, break on hard impacts or crumble after resting; splinters, frost
dust, glints and the fading frosted floor complete the source recipe.

Source `reach` defaults to172 and accepts16..512 logical pixels; `height`
defaults to56 and accepts4..128. Both accept fractions and live in owned typed
`ice.reach`/`ice.height` settings. Seed/palette/playback/light use the checked
configuration API. The gallery variant uses reach141.5 and height43.5 with
changed seed/palette/rate/light. There is no actor or gameplay-target authority.

Shared internal panes own their polygon and motion/rest/brittle/crumble state.
Ground and event callbacks are borrowed only for a synchronous fixed update,
never retained; compiled callbacks may emit into the kernel's other owning
systems but cannot mutate the pane list or retain fragment references.
Polygon cuts return independent owning pieces; projected-corner snapshots
survive pane destruction. Drawing preserves source glass blending, opaque
facets, edge light, flashes and traversal order. Source duration3 seconds is
metadata; source endBy trims particles/debris/panes rather than freezing time.

### Falling icicle

`ice/icicle-fall` owns a120Hz one-shot simulation. Meltwater swells and drops
from the rippled icicle; rime sheds during trembling, the root snaps, and the
icicle falls with trailing frost before striking the floor. Random polygon
cuts across its length and through large pieces produce centroid-centered
shards, which tumble, bounce, skid and crumble with splinters, dust and glints.
The source rime/stubs, shadow, front/back frost band and floor melt are retained.

The source has palette and seed options; native playback rate and light gain
remain common controls. Its root height, width and length are fixed source
layout, with no invented physical-size options. The paired gallery variant
changes seed/palette/rate/light. Invalid inactive options reject atomically.
Shared polygon centroids use signed area for the center and unsigned area in
the result; near-degenerate polygons use the source arithmetic mean with zero
area. Inputs are synchronous borrows and results own their scalar values.
Centroid evaluation is pure and has no thread affinity while the caller keeps
its input stable; it executes no callbacks. Simulation instances remain
caller-thread affine.
Source duration2.6 seconds is metadata; endBy trims particles/debris/panes.

### Ice block shatter and fracture paths

`ice/ice-shatter` owns a120Hz one-shot simulation: a faceted ice block with
internal veins/bubbles takes a hit from the left, jolts, grows two crack waves,
shivers and flashes white. Recursive cuts split each face into tone-preserving
chunks, thrown outward into bouncing/skidding panes, splinters, frost dust and
glints. Sliding chunks scuff the floor; scuffs age independently and fade.

Its source options are palette and seed, plus common native playback/light
controls. Block geometry and hit location are fixed source layout; no invented
size/target/actor options are published. The gallery pairs defaults with a
changed seed/palette/rate/light variant. Source duration2.8 seconds is metadata;
endBy trims particles/debris/panes while scuff lifetimes keep their source logic.

Shared internal fractures own all vertices and cumulative distances. Their
compiled inside predicate and RNG are borrowed synchronously during construction,
with depth limited to two and child paths emitted before their parent. The
default angle samples RNG only when absent. Engraved growing drawing uses two
source-ordered passes with a dark offset and pale/hot tips. Path/ramp/target
borrows do not survive drawing, which never advances RNG. Construction cost is
bounded by authored path count/length and is paid when preparing/resetting state.

### Actor freeze

`ice/freeze` owns a120Hz simulation of frost climbing the supplied actor,
turning its pixels to ice and adding rim feathers. Thirteen panes seal around
it, sweep with light, crack, flash and burst into tumbling glass; the frozen
character thaws from the top down. Floor frost, shards, dust, glints and melt
follow the source recipe. Source duration4 seconds remains metadata.

The actor snapshot, seed, eight-step palette and common playback/light controls
use owning checked configuration. Default is Dummy; Hero and validated custom
pixels/masks/anchors are supported. Flood distances/reach retain source Float32
writes, open-list tie order and stable sorting. Only mask cells connected to
bottom seed cells participate; disconnected upper islands remain unfrozen.
The native None option omits frozen pixels/feathers while retaining the fixed
block/floor effect; source null actors are undefined, so this absence behavior
is an explicit native contract rather than a source null-parity claim.

Actor and facet/edge/material caches belong to each instance. Preparation/reset
scans bounded actor cells and the source-ordered open list, cached across frames;
seek uses the existing prepared replay API. No actor/pixel/painter references
survive synchronous calls. Full authored Act onBody poses, hitstop and composed
body ordering remain part of the still-open full presentation work. Search the
full ID for a default/custom-actor and seed/palette/rate/light pair.

`ice/frost-nova` owns the source charge snowflake, cubic wavefront, alternating
back/front crystal clusters, floor frost and melting debris. Its `reach` is
104 native pixels by default and accepts16..512; seed, fitted palette, rate
and light retain the common transaction contract. The effect planes do not
consume actor pixels; authored caster poses, target hits and dust remain in
the open full Act composition scope.

`ice/frost-breath` is a 1.2-second analytic loop with source tiled turbulence,
back/front soft billows, snow, wind streaks, tumbling ice and mouth glows.
Its typed ice settings own `spread` (default0.21,0..1 radians) and `power`
(default1,0..4). Power changes only source billow velocity; zero does not stop
other source layers. Palette, seed, playback speed and light use common APIs.

Four variants (145 samples each) match1160/1160 accepted planes,839 strict;
3553 RGB pixels differ by at most1, while alpha/coverage/all light are exact.
The source sin(-0.20973308703862129) is -0.20819884329582508 in V8 versus
-0.20819884329582505 in MSVC; identical puff inputs yield y=-15.044755397150146
versus -15.044755397150142. Source/CRT exp values also differ one ULP for some
ages. These tiny center shifts affect normalized transparent color truncation.
Raw strict comparison evidence is retained; no numeric bias or picture repair
was added. Multi-loop transformed layers match150/150 accepted,138 strict.

`ice/blizzard-vortex` is a two-second analytic snow funnel with source radius
and axis sway, front/back wind streaks and orbiting crystals, translucent body,
foot puffs, floor drift and mouth cloud. Typed `ice.height` defaults80 and
accepts4..256 source pixels. The source128x116 viewport stays fixed; use a
larger caller canvas when needed. Seed/palette/rate/light use common APIs.

Five241-sample variants (including minimum height4) pass2410/2410 raw planes,
2214 strict.414 RGB pixels differ at most1, with exact alpha/coverage/all light.
At t=1/120, source foot-puff angle9.011344725511417 gives sin0.4017556641209109
in V8 versus0.40175566412091096 in MSVC; y=-3.641816977652719 versus
-3.6418169776527183. The tiny shift changes normalized transparent color
truncation. Strict evidence is retained; no numerical bias or image correction
was added. Multi-loop transformed layers pass150/150,147 strict, and height256
on384x512 caller canvas passes150/150,145 strict, under the existing RGB1 limit.

`pickups/coin-spin` is a 1.6-second analytic thick-coin loop with source embossed
face, reeded side band, sweeping specular flash, glints, hover and floor shadow.
Only source seed/palette plus common rate/light/placement controls are exposed;
radius7, thickness3 and hover17 stay authored source dimensions. The source's
shared mutable32x32 Layer becomes bounded call-local owning storage; unrelated
instances cannot overwrite each other's scratch colors or outline. Temporary
pixel-center sampling and four-neighbor outline order remain source-exact.
Four193-sample variants match1544/1544 color/light planes strictly. Transformed
quarter-turn/edge/loop phase layers match150/150 strictly. No new tolerance.

`pickups/gem-spin` is a 2.4-second analytic brilliant-cut gem. Its49 source
planes are owned immutable preparation data; ray intersections, normals,
half-vector specular light, pavilion shading and deferred ridge recoloring
remain procedural. Per-draw scratch color/hit/edge arrays are call-local.
Source radius7.5/crown3.2/table0.5/pavilion9.5/girdle0.9 stay authored; source
seed/palette plus common rate/light/placement controls are configurable.
Four289-sample variants match2312/2312 raw color/light planes strictly;
critical turn/flare/loop transformed layers match150/150 strictly. No new
tolerance is introduced. Geometry and helper material controls have independent
upstream fixtures, including white, alpha, edges-off and gain variations.

`pickups/crystal-shard` is a 3.2-second analytic loop with22 irregular prism,
cap, point, shear and chip planes, source lean/projection, masked rising crack
pulses, split orbiting splinters and break flares/embers. Mesh planes are owned
immutable preparation data;32x52 solid and16x16 splinter scratch stay call-local.
An elliptical additive floor pool uses the source squared falloff. Only source
seed/palette plus common rate/light/placement controls are exposed; dimensions,
fracture paths and projection remain authored. Four385-sample variants match
3080/3080 raw color/light planes strictly. Pulse/half-loop transformed layers
match150/150 strictly. The pool has independent transformed/cutoff source fixtures.

`pickups/xp-orb` is a 1.8-second analytic figure-eight with source tapered
ribbon/core, shaded round head and seeded drifting tail flecks. Four217-sample
variants match1736/1736 color/light planes strictly. `pickups/loot-beam` is a
2.4-second analytic rarity column with bands/threads, two emitted floor rings,
source stepped sword/steel/gold/leather shading, sliding edge glint and motes.
Four289-sample variants match2312/2312 planes strictly. Sword scratch is call-local;
source seed affects motes while unseeded column flicker retains source hash tags.
Each effect's turn/emission/glint/loop transformed layers match150/150 strictly.
Source seed/palette and common rate/light/placement are configurable; trajectories,
column dimensions and fixed sword materials stay authored. No new tolerance.

`pickups/power-star` is a 1.6-second analytic bevelled star with five arms,
ridges, beat swelling, rotating light rays and emitted point sparks. Four193-
sample variants match1544/1544 raw planes strictly. Source fractional beat hash
tags use the same integer conversion as JS; seed controls the authored idle
specks/twinkles. `pickups/key-glint` is a two-second analytic ring/collar/shaft/
notched-bit key with rotated metal shading, slanted traveling glint and
floor-clipped falling sparks. Four241-sample variants match1928/1928 strictly.
Both sprite scratch buffers are call-local, with source seed/palette and common
rate/light/placement controls; shape/timing constants stay authored. Each
beat/glint/spark/loop transformed layer check matches150/150 strictly. No new
tolerance. The batch combines3472 raw and300 critical-layer planes.

`pickups/heart-pickup` is a 1.2-second analytic sextic heart with source curved
surface normals, glossy shading, double-beat squash/stretch, light ring and
constant-life floating mini-hearts. Four145-sample variants match1160/1160 raw
planes strictly. `pickups/potion` is a 2.4-second analytic rocking glass flask
with cork/lip/neck/bulb shading, lagged brew, rising/popping bubbles and wisps.
Four289-sample variants match2312/2312 strictly. Source seed/palette and common
rate/light/placement controls are configurable; shape/timing stay authored.
Scratch and callback borrows are call-local; numeric lifetime cycles preserve
source RNG consumption. Each critical transformed-layer check matches150/150
strictly. Pillow clipping, light direction and distance samples have independent
upstream fixtures. Combined3472 raw and300 critical planes; no new tolerance.

`pickups/collect-burst` is a 1.35-second fixed-tick one-shot: initial white
flash/cross, two expanding rings, fourteen curving/braking stars with short
trails and seven upward motes. Source seed/palette and common playback/light
controls are configurable. Four163-sample variants match1304/1304 raw source
planes strictly; critical offset/flip/rotation back/front/both phases match
150/150 planes strictly. Simulation state owns its particles and consumes the
source RNG order; absolute seek replays the same injected120-Hz timeline.

`pickups/chest-open` owns a4.6-second fixed-tick timeline: anticipation rattles
and seam light, a261-sample curved hinged lid with wood/iron/hoard shading,
fan rays, bouncing/skidding coins and four independently colored gems,
collection flashes/rings and top-down pixel dissolution into gold dust.
`coins` is an integer0..64, default12; four gems remain with zero gold coins.
`wood` is an owning1..64-entry opaque ramp normalized to7 source steps.

Wood retains signed or unsigned32-bit source numeric representation when
already7 steps; resizing reproduces source resolveOptions' conversion through
hex strings, producing signed colors. This matters because source strict
equality compares unsigned sprite cells with the original wood numbers
during dissolution. Default wood therefore uses signed numbers. Parameter
metadata exposes `colorNumberRepresentation: signed-or-unsigned-32`; Value
configuration round trips preserve these numbers. Other ramps keep their
existing opaque unsigned contract. Both representations draw the same color
bits, but source dissolution can differ.

Nine553-sample default/seed/material/wood/count/sign variants match9954/9954
raw source planes strictly, including zero and64 coins; key transformed
back/front/both phases match150/150 strictly. Source coin/gemlet helper
fixtures exercise12 orientations, edge-on/flat coin geometry, gem facets,
white collection flash, alpha and transformed target planes. State and
materials are owned; painter and callbacks are synchronous borrows, with
injected120-Hz time and source-sequenced RNG.

Critter preparation is in progress. The internal owning Grid now implements
source color/Float32-alpha/tag storage, cell-center ellipse/shade/line/polygon,
immutable-neighbor outward outline, clear/erase semantics and transformed
opaque-dither/translucent-blend stamping. This foundation itself adds no catalogue IDs; the butterfly/bird entries
below are separate native algorithms with their own gallery integration.

`critters/butterfly` and `critters/bird` now have native analytic algorithms.
The butterfly preserves burst/glide flight, folded polygon wing patterns,
veins/spots, body/antennae and seeded dust. Its source `spots` ramp is4 steps.
The bird interpolates five wing key poses, derives body lift from downstroke
history, shades body/tail/breast/beak/flight feathers, stamps the sourceFloat32
wing echo with translucent blending and releases a drifting feather. Its
source `breast` ramp is4 steps. Both use owning materials, call-local grids and
injected analytic time with the source seed phase shift.

Four385-sample butterfly variants match3080/3080 raw source planes strictly;
four289-sample bird variants match2312/2312 strictly. Critical transformed
back/front/both phase checks and native host/gallery validation are recorded
in the migration plan. No new RGB/alpha/coverage/light tolerance is introduced.

`critters/bat` preserves the source four discrete wing frames, membrane and
finger-bone shading, figure-eight flight, eye-light trails/glints, seeded
moon dust andFloat32 translucent downbeat echo. Its `eyes` ramp has4 steps.
`critters/eyeball` preserves gaze overshoot, iris perspective, fixed light
glints, wet sclera, blink lids, optic-nerve sway, shadow and occluded orbit
streaks. `flesh` and `iris` are5-step ramps, `hover` is finite0..256 pixels.
Seeded35-point vein geometry is prepared once per configuration and owned by
the immutable analytic kernel; drawing borrows settings/painter synchronously.

Four289-sample bat variants match2312/2312 raw source planes strictly;
four385-sample eyeball variants match3080/3080 strictly. Critical transformed
back/front/both phase layers match150/150 per effect,300 total strictly.
No new raster/alpha/light tolerance is introduced. Rejection, owning materials,
seed replay, phase seams, large-time sampling and stop fade share the native
configuration/playback contract; hover boundary rejection is atomic.

`critters/fish-school` is a native4.8-second analytic loop: seven authored fish
in back/front depth rows, source body/tail waves, translucent dorsal/pectoral/
forked fins, dart-and-return motion, seeded mouth bubbles, caustics and shafts
of light. Source `stripe` is a3-step opaque ramp, independent of the7-step
palette. Seed/common playback/light controls use the existing validated API.

Four577-sample variants match4616/4616 raw source planes strictly; critical
dart/return/loop transformed phase layers match150/150 strictly. Independent
critter-kit fixtures also cover17 flame/bubble looks (34 target planes) and
nine ballistic floor-hop samples; these helpers prepare the flying-skull port
but do not add a skull catalogue ID. All scratch and material ownership is
explicit, with injected time and source RNG sequencing.

`critters/flying-skull` is a native2.4-second analytic loop. It preserves
cranium/face/temple/socket/teeth/jaw shading, cracks and bone outlines,
chattering/gaping jaw, front/back crown tongues, burning eyes, mouth puffs,
smoke, licks, embers and optional bouncing floor sparks/light. `bone` is a
7-step ramp, `hover` is finite0..256, and `floor` is a typed boolean.

The18 crown tongues are immutable prepared geometry with stable ordering for
equal absolute x; fractional and large seed-derived tags stay double until
the hash boundary. Draw scratch, particle samples and heat ramp are call-local;
settings/painter borrows are synchronous. Six289-sample default/material/
seed/floor combinations match3468/3468 source planes strictly. Additional
transformed phase layers150/150 and hover/floor boundary planes56/56 are strict.
No new alpha, coverage, light or RGB tolerance is introduced.

`critters/frog` preserves crouching, takeoff, tongue capture, landing, throat and blink phases, with a three-step `belly` ramp. `critters/ghost` preserves the lagged sheet, scalloped hem, arms, gaze and Float32 transparency; `hover` accepts 0 through 256 (default 14). Both are native analytic loops with owning configuration snapshots and explicit simulation time. Raw source comparisons passed 2504 frog and 2312 ghost color/light planes exactly.

`critters/wisp` is a native3.2-second figure-eight loop with three layered flame tongues, cooling path puffs, blinking face and analytic embers. Its `hover` range is0..256 (default36). Particle drawing receives the sampled source time for ember flicker. Four full-cycle seed/material variants match3080color/light planes exactly, transformed phase layers150exact, hover endpoints28exact.

`critters/snake` is a native2.4-second analytic loop. Body humps move backward along70local pixels; the head bobs, a forked tongue flicks twice per cycle, and contact points emit sampled dust. The `belly` material owns a three-step ramp. Rendering owns a temporary88x26grid and particle samples; configuration and source RNG ordering follow the shared contracts.

Snake parity covers2312raw planes (four289-sample variants):2310strict, two
single-pixel RGB1 differences in source/CRT floating-point shadow composition.
Seed123 at85/120s: V8 sin(0.94911004723442)=0.8128975122645528 versus
CRT0.81289751226455265; the resulting height is5.86986976048397 versus
5.8698697604839687. Shadow alpha0.23382400637909287 versus
0.23382400637909295 crosses a color-channel integer truncation boundary.
SeedMAX at197/120s likewise differs in one shadow channel. Coverage, alpha
and light are exact. No position, opacity or source formula is biased to hide
these numeric boundaries. Default/zero seeds and150transformed phase planes
match strictly. The local diagnostic records each shadow blend before/after.

`critters/jellyfish` is a native3.2-second analytic loop with a depth-shaded
Float32 translucent bell, radial canals, internal organs, nine tentacles,
four frilled oral arms, rim lamps, pulse rings, plankton and rising bubbles.
The owning `glow` ramp normalizes to four opaque steps. Thirteen seed-derived
strand specifications are prepared immutably; frame paths use fixed-capacity
storage. Grid depths are owning four-neighbor snapshots, saturated at the
requested maximum, retaining source lifetime semantics through cell edits.
Sixteen independent depth fixtures and3080raw plus150transformed phase planes
match upstream exactly; palette/material/seed and configuration lifetime
contracts are verified in STATIC and DLL tests.

`critters/slime` is a native1.8-second analytic hop loop with source squash,
body gloss, Float32 translucent jelly, facial poses, internal bubble/pop,
twelve seeded ballistic droplets returning to the body, wet puddle bubbles,
spores, drips, snapped goo strand and floor/internal light. Droplet launch
parameters are prepared immutably; all drawing samples injected time.
`hop` defaults to24 and accepts0.01..256logical pixels. Zero is rejected
atomically because the source floor-light normalization divides by hop.
Four seed/material variants1736planes, transformed phase layers168planes
and hop endpoints28planes match upstream strictly. Independent piecewise
curve samples and configuration/seek/stop lifetime contracts are tested.
All twelve critter effect algorithms are present; the overall244-effect
and Full Act/composition goals remain open.

`holy/blessing` is a native3-second loop with a shimmering four-ray cone,
head/shoulder highlights, floor pool, twenty descending sparkles that expire
at actor/floor contacts and sixteen fine motes. Its owning `actor` input accepts
none, hero, dummy(default), or custom snapshots. Landing trajectories are
prepared per program from seed and mask; same-position custom masks do not
share the upstream global cache's stale entries. Fractional origins retain
the source typed-array indexing semantics. Full Act pose animation/composition
remains an explicit overall-goal requirement beyond these static actor inputs.
Four seed/palette variants2888planes, transformed phases150planes and eight
actor-input variants976planes match upstream strictly. STATIC/DLL tests cover
source phase checksums, configuration ownership, stale seek rejection, round
trips, stop fade and same-position different-mask landing independence.

`holy/feather-burst` is a native fixed120Hz simulation with thirty loose
feathers, eleven expanding puff lobes, down, glints, pop/ring and warmth.
It preserves random-default consumption before explicit launch overrides,
delayed births, reverse removal order, drag, pendulum flutter, floor settling
and per-layer feather shading. The owning Holy `glow` color is opaque packed
RGBA (default0xffc0f0ff). Simulation inputs are dt and a per-instance RNG.
Independent feather raster checksums and eleven dynamics snapshots cover
birth delay, floor contact, landing alignment, lifetime removal and RNG state.
Four full-cycle variants accept2888planes,2887strict;162transformed phase
planes accept with161strict. The two measured RGB1boundaries retain exact
coverage/alpha/light: V8 feather-tip projection u0.7770658672093027 versus
CRT0.77706586720930293 yields alpha0.2524151701291988 versus
0.25241517012919873. Blending the same color then truncates one channel:
243 versus242.99999999999997. Formulas remain unchanged, without pixel bias.

`holy/divine-shield` is a native2.4-second analytic loop: deduplicated sphere
meridians/equator, gold film, sweeping sheen, rim/crossing/pole glints, floor
seal and actor outside-ring light. `radius` accepts1..256 (default29). Below22
there is no floor intersection, so source NaN-bounded seal/pool operations emit
no cells; the native guards preserve that result without invalid pixel indexing.
Latitude/equator sample sequences and owning actor outline are prepared once.
Lattice replacements preserve source insertion order and minimum-depth rules.
Source two-argument Math.hypot uses max normalization before square root/rescale,
verified against pinned Node26.4.0 deps/v8/src/builtins/math.tq lines387-402:
https://raw.githubusercontent.com/nodejs/node/v26.4.0/deps/v8/src/builtins/math.tq
The independently expressed jsHypot2 helper is used only by this new kernel.
It removes the initial systematic film rounding mismatch: source normalized
radius0.8835260672633765 versus CRT hypot0.88352606726337635 at(-7.5,-24.5).
No tolerance or pixel bias was added. Four cycles2312planes, transformed phases
150planes, radius1/21/22/25656planes and eight actors976planes match strictly.
Independent179norm bit patterns and seven source outline/query fingerprints
verify the shared arithmetic/actor helpers; Blessing's976actor planes regress
strictly after sharing the bounded index query. Full Act remains in progress.

`holy/heal` is a native fixed120Hz one-shot simulation: eight blooming lotus
petals, rising hoop, drained/restored actor shading, row-edge sparkle emission,
sixteen spiral riders popping into hearts/stars, pool and burst lights.
It owns actor/palette data and derived edge rows; RNG, time and mutable particle
state belong to the instance. Recolor traversal follows nonzero actor pixels,
independently of mask occupancy, with synchronous color/alpha callbacks.
The source unconditionally requires an actor silhouette. A source-owned
`actorRequired` descriptor flag excludes none from this effect's constraints
and returns InvalidArgument at /options/actor/source before preparation can
publish missing input. Existing optional-actor effects still advertise none.
Stock hero/dummy and custom snapshots, including empty masks and fractional
origins, are supported. Full Act pose animation/composition remains open.
Four full-cycle variants1832planes, transformed phase/tail174planes and seven
actor variants854planes match upstream strictly. Independent recolor fixtures
and required/optional actor metadata, atomic rejection, palette ownership,
roundtrip, stale seek, replay, far-seek rejection and stop fade are tested.

`holy/heal-aoe` adds an owning fixed120Hz Healing ring simulation,256x136 with
origin128/104 and a2.9-second one-shot duration. Its source defaults are seed6,
the eight-step healing green ramp and reach104. `reach` is a finite positive
floor radius bounded to1..256 through the shared configuration validator.
Twenty-six prepared sprout sites and fourteen orbiting lights retain source
RNG ordering. Rendering splits the floor wash, back/front rolling ring and
beveled crosses around the seed flare and release jet; particles and transient
lights belong to the instance. Configuration retains no caller storage.
Four full-cycle variants2792planes, transformed phase/tail222planes and four
reach boundary variants256planes (stock and expanded canvas) match upstream strictly. STATIC/DLL each
pass16holy cases/311458assertions;166focused host cases pass. Full engine
build passes. Full Act/composition and remaining final integration gates stay open.

`holy/holy-pillar` is a native fixed120Hz simulation with a192x232 canvas,
origin96/204 and a3.5-second one-shot duration. It owns shaft recipes,72dust
motes, delayed drifting feathers, source RNG and particle/light state. The
opening fan pours down from the iris sun, pulses, folds on the floor sigil and
lifts away. Shaft contributions are rounded toFloat32 individually; core
brightness retains the source Uint8 truncation before compositing. `shafts`
is a checked integer3..64, default7; seed3 and GOLD8 are source defaults.
Configuration uses the existing owning snapshots and atomic prepare/commit
route. Frame drawing retains no callbacks or caller pointers. Full Act and
remaining final integration gates are still open.

Four full-cycle variants3368planes, transformed phase/layer204planes and
expanded-canvas shaft3/8/64variants96planes match upstream strictly. STATIC/DLL
each pass18holy cases/489736assertions. Source-quality gate and full Windows
build and168focused host cases pass. The8holy-effect family regression432planes
remains strict. Native114-ID catalogue/playback checks and standard six-second
smoke pass; engine screenshots verify default and eight-shaft variants.

`holy/smite` is an owning fixed120Hz simulation,224x176 with origin112/148,
2.8-second one-shot duration and seed9. The closing ground mark precedes a
falling hammer of light, shock ring/dome, stone chips and cooling cracks.
Seven crack paths and their forks are prepared once from source RNG order.
`stone` is an opaque packed RGBA material, default0xff66505a; derived chip
shades and dust retain an owning snapshot. Particle/chip/light updates preserve
source order, including chip bounces and reverse removal. All drawing borrows
are synchronous; no external actor/entity/image storage is retained.
Four full-cycle variants2696planes, transformed phase/layer216planes and three
expanded-canvas stone variants96planes match upstream strictly. STATIC/DLL
each pass20holy cases/647509assertions. Full Windows build, source-quality gate
and170focused host cases pass. Same-family9-effect transformed regression486planes
remains strict. Native115-ID catalogue/playback checks and six-second smoke pass;
engine screenshots verify default/material variants. Full Act/composition and
final platform/performance gates remain open.

`holy/resurrection` is an owning fixed120Hz simulation,192x160 with origin96/140,
4.8-second one-shot duration and seed5. It draws the pouring shaft, floor pool,
climbing body recolor, unfolding eighteen-quill wings, halo, wingbeat gust and
feather scatter. `actor` requires a stock/custom snapshot; none rejects before
publication. `ash` accepts1..64 opaque packed colors and normalizes to six steps.
Actor, ash, palette, outline rows, ray/mote/quill recipes and mutable simulation
state belong to the instance. Wing queries return owning optional samples;
frame drawing retains no pose pointers or caller storage. Explicit feather
side overrides still consume all seven source default draws.
Independent wing400planes, actor matrix938planes and ash variants96planes are
strict. STATIC/DLL each24holy cases/770897assertions pass. Dense comparison is
4612/4616strict, with four single-pixel RGB-only LSB boundaries; transformed
phase/layer comparison is298/300strict, with the same tick138boundary in back
and both. Every coverage/alpha/light channel is exact. Production/source numeric
probes locate feather blend alpha last-bit differences before channel
truncation; the explicit RGB1 measurement retains spatial reports. No pixel bias,
epsilon or default comparer relaxation is used. Windows build,174focused host
cases and final source-quality gate pass. All10holy effects regress540transformed
planes strictly. Native116-ID catalogue/playback checks and six-second smoke pass;
engine screenshots cover body recolor, wing opening/beat/scatter and a custom
actor/two-stop ash variant. Full Act pose/composition remains in progress.

`impacts/blunt-hit` is a native fixed120Hz simulation,128x88 with origin64/72,
0.9-second one-shot duration and seed9. It owns nine inked wedges,35soft dust
puffs, five faceted tumbling clods, collision dust kicks and warm lights. The
lumpy hot core flashes white then cools to cream before it dissolves. `warm`,
`dust` and `clod` are opaque packed colors; `height` is finite0..256 logical
pixels above the floor, default22. The seven-step palette is independently
owned and normalized at preparation. Impact stone/rock shading follows its
own faceted source recipe rather than the explosion rock renderer.
Defaults218dense planes, transformed phase/layer222planes and expanded-canvas
height0/32.5/256variants96planes match strictly. STATIC/DLL each pass two
impact cases/45141assertions. Full dense measurement872planes is804strict;
all872remain within the explicit RGB1bound with coverage/alpha/light exact.
The294different RGB pixels are located by spatial reports. Source/production
numeric probes locate last-bit trajectory differences in overlapping dust
blends; no bias, epsilon or default comparer relaxation is applied. Three
material variants96planes are95strict, with three white-material pixels at
one time differing by RGB1only. Full Windows build,176focused host cases and
final source-quality gate pass.117-ID native catalogue/playback and six-second
smoke pass. Engine screenshots verify core, tick8wedges, dust and clods.
Comma/period inspect previous/next120Hz frames while paused via checked seeks;
12native workflow checks verify simulated and analytic stepping, exact frame
restoration and zero clamp. Full Act/composition and final gates remain open.

`impacts/bullet-impact` is a native fixed120Hz wall-hit simulation,152x100 with
origin116/84 and a1.4-second one-shot duration. It owns directional spikes,
ragged pock rim/cracks,18rebounding sparks,8wall chips,9dust particles and a
smoke wisp. The hot half-star and half-ring open away from the wall; the mark
cools through the palette to soot. `stone` is an opaque packed wall material,
default0xff79505b. `height` accepts0..256, default24. Seed6 and seven-step hot
palette are source defaults. Source RNG consumption and draw layering are
preserved; no caller options or painter pointers are retained.
Four full cycles1352planes, transformed phase/layer222planes, expanded-height
variants96planes and stone variants96planes all match upstream strictly.
STATIC/DLL each4impact cases/106005assertions pass. Windows build,178focused
host cases and final source-quality gate pass; three impact effects regress
162transformed planes strictly.118-ID native catalogue/playback checks, exact
tick0/2/5engine screenshots and standard smoke pass. Full Act/composition and
final cross-platform/performance gates remain open.

`impacts/crit-hit` is a native fixed120Hz critical-hit simulation with four
ranked star keyframes, seven breaking ring arcs, four-ray flare,36sparks,
10tumbling shards,12motes,5twinkles and independently decaying lights.
The216x136 canvas has origin84/106 and a.9-second one-shot duration.
`aim` accepts[-2pi,2pi], default.12; `height` accepts0..256, default26.
Seed3 and eight palette stops are source defaults. Recipes and simulation
state are owned; time/RNG are injected, painter borrows are synchronous.
Four dense cycles872planes, transformed phase/layer222planes and four
expanded direction/height variants128planes match pinned upstream strictly.
STATIC/DLL each6impact cases/223594assertions pass. Complete Windows build,
180focused host cases and source-quality gate pass. All119catalogue IDs,
pause/stop/restart and exact page/cycle restoration pass through normal input.
Engine screenshots at ticks0/4/7/12verify all four star beats and the direction/
height variant; standard six-second smoke passes. Verified algorithms119/244.
Full Act/composition and final OBJECT/platform/performance gates remain open.

`impacts/hit-spark` owns an asymmetric five-ray cross, diamond core, nine
bouncing sparks, six hot chips and three decaying lights. It uses a136x80
canvas, origin44/64,.5-second duration, seed4 and seven palette stops.
`aim` accepts[-2pi,2pi], default.1; `height` accepts0..256, default20.
State and randomized recipes are owned, update time and RNG explicit;
configuration publication rejects invalid candidates without observable mutation.
Four dense cycles488planes, transformed phase/layer222planes and expanded
aim/height128planes match strictly; five-impact family270planes strict.
STATIC/DLL each8impact cases/267193assertions, full Windows build,182focused
host cases and source gate pass.120-ID native catalogue, pause/stop/restart
and exact page/cycle restoration pass. The short-lived effect's stop baseline
uses a paused tick0frame, since the later animation sample is already empty.
Engine screenshots verify ticks0/3/7and the direction/height variant; standard
six-second smoke passes. Verified algorithms120/244; Full Act/composition and
final OBJECT/platform/performance gates remain open.

`impacts/hit-flash` owns a required actor snapshot, luminance-derived red/tint
ramps, randomized comic wedges, hot directional burst and decaying lights.
It fills actor mask cells independently of pixel content; white beats fill
zero-valued source pixels, while later red and cooling beats preserve the
source's distinct treatment. Default actor is Dummy; Hero and custom masks,
including fractional coordinates and mismatched mask/pixel occupancy, work.
The80x64 canvas has origin40/54,.5-second duration, seed17 and seven palette
stops. `lines` accepts integer0..128(default7), and `side` accepts numeric
-8..8(default1), multiplying a12logical-pixel offset from the chest anchor.
Configuration preparation copies owned data; checked publication and stale
seek rejection preserve atomicity. All time and randomness are explicit.
Four dense cycles488planes, transformed phase/layer222planes, seven actor
variants854planes and line/side variants224planes match strictly. STATIC/DLL
each10impact cases/287751assertions pass. Complete Windows build(retry after
index file-access failure),184focused host cases,121-ID native catalogue and
pause/stop/restart/page restoration pass. Engine screenshots at ticks0/3/7/
16/19/30verify five beat states over static stock/custom actors. Standard6s
smoke passes. Final source-quality gate is recorded in
`build/pixelfx-validation/hit-flash-source-check-final.log`. Full Act/onBody
pose composition and final OBJECT/platform/performance gates remain open.

`impacts/parry` owns eight randomized star spikes, six broken-ring segments,
two crossed blade edges,24rebounding sparks,8motes,3delayed stars and lights.
The star changes through three source keyframes, the ring breaks into cooling
arcs, and the alternating spark fan rains down. Source RNG order and fixed
120Hz integration are preserved. Canvas200x112, origin100/96,duration1s;
seed11,palette7,height30 are defaults. `height` accepts0..256. Configuration
and seeks use checked preparation/publication; state is owned and painter
borrows remain synchronous. No actor or caller reference is retained.
Four dense cycles968planes, transformed phase/layer222planes and expanded
height variants96planes strictly match upstream. STATIC/DLL each12impact
cases/377427assertions pass. Complete Windows build,186focused host tests,
122-ID native catalogue and animation/pause/stop/restart/page restoration pass.
Engine screenshots at ticks0/3/7/18/48verify three star beats, broken ring and
late sparks, with height/seed/palette/speed/light variant. Standard6s smoke
passes. Final source-quality gate is recorded in
`build/pixelfx-validation/parry-source-check-final.log`. Full Act/composition
and final OBJECT/platform/performance scope remain open.

`impacts/ricochet` owns incoming/outgoing tracers, a delayed impact trigger,
a glancing star, low rebounding sparks, grit/dust, cooling skid and half ring.
Canvas232x84, origin64/60,duration1.1s,seed8,palette7; `stone` is opaque packed
RGBA(default0xff79505b). `impacts/shield-block` owns front/back dome lattice,
two surface ripples, deterministic cell blink clocks, glancing sparks and
RNG-driven lifting stars. Canvas148x104,origin40/88,duration1.1s,seed12,
palette8. Its independently configurable `sparks` ramp accepts1..64opaque
stops normalized to4. Actor defaults Dummy and can be Hero/custom/None;
effect planes are actor-independent and body drawing remains separate.
Both kernels match1064dense planes and222transformed phase/layer planes
each. Stone and spark-ramp variants each match96expanded-canvas planes.
STATIC/DLL17impact cases/626246assertions pass including owned-layer drawing.

C++ `drawLayer(PixelFxLayer)` replaces owned planes at the current clock with
Back, Front or Both and returns checked status. Invalid layer leaves planes
unchanged. It shares the existing canvas renderer, retains no target/callback
and does not advance/seek. Raster/scratch costs follow `draw()`; output storage
is reused. Script `drawLayerTo(color, light, layer)` accepts integer0(back),
1(front),2(both), validates distinct matching RGBA8images, and returns status.
The gallery's actor previews now alpha-compose back color, body, front color,
then add the combined light plane. Images/textures are allocated once; explicit
back/front and combined-light raster passes preserve source light saturation.
Complete Windows layer-adapter build and191focused host cases pass.124-ID
normal-input catalogue and both effects' animation/pause/stop/restart checks
pass. Native startup checks reject invalid integer/float/string layers and
aliased images for both shield variants. Engine screenshots verify incoming/
outgoing ricochet, skid, dome/ripple/blink/motes and custom material/actor/ramp.
At tick72, all9source-derived opaque-body occlusion pixels exactly match the
native screenshot, including combined light; source back lattice is hidden
correctly. Standard6s smoke passes. Final source-quality gate is recorded in
`build/pixelfx-validation/block-ricochet-source-check-final.log`. Full Act/
brace/pose/hitstop/shake/dust and final platform/performance scope remain open.

`impacts/heavy-landing` owns floor dent/cracks, split back/front dust,
two lumpy rings,12faceted pebbles,24grit bits and collision dust kicks.
Canvas176x80,origin88/60,duration1.6s,seed14,palette7,default Dummy actor;
optional actor affects body composition independently of effect state.
`impacts/ground-slam` owns hot cracks/embers, seven-spike burst, paired
crescent fronts, source emission accumulators,20rocks,110grit particles,
rolling dust, a tall cloud, settling flakes and lights. Canvas288x208,
origin144/164,duration3.2s,seed5,palette7. Both own opaque `stone` material,
default0xff693f4a. Ground `reach` accepts0..512logical pixels(default104).
Shared ground helpers prepare owning jagged walks/forks once and rasterize
through synchronous compiled callbacks. Time/RNG are injected; configuration
and seek publication remain checked and revision-safe.
Heavy four dense cycles1544planes and354transformed phase/layer planes,
ground four dense cycles3080planes and402transformed phase/layer planes,
three materials96planes per effect and expanded reaches0/72.5/51254planes
all match strictly. STATIC/DLL20impact cases/922329assertions pass. All11
upstream impact algorithms are present;594family transformed planes strict.
Complete Windows build and194focused host tests pass. Native126-ID catalogue and both effects' animation/pause/stop/restart pass.
Engine screenshots verify heavy ticks0/6/30/60/120/180and ground0/7/30/60/
120/300, including stone/reach/custom-actor variants. Standard6s smoke passes.
Final source-quality gate is recorded in
`build/pixelfx-validation/ground-impacts-source-check-final.log`.
Full Act/FALL/landing poses, ghost/hitstop/shake and final platform/performance
scope remain open. Gallery PgUp/PgDn steps back/forward30ticks(.25s), while
comma/period keeps one-tick stepping. Simulation and analytic roundtrip,
zero clamp and frame1restoration have native input/screenshot proof.


Search `scifi/jetpack`, `scifi/teleporter-pad` and `scifi/hologram` for native
periodic exhaust/fire-to-smoke, lifting teleport rings and a projected rotating
crystal with scanline tears. Right previews change the opaque metal ramp;
jetpack length is40.5 and hologram split is0xff40e0ff. Typed configuration
validates metal1..64opaque colors (normalized to6), length0..128 and opaque
split. Ghost emitters reconstruct source1/240-second motion and periodic
carry-over from owned recipes; direct seek and drawing do not mutate recipes.
Verification of this batch is in progress; the complete244-effect goal and
full Act/12moves, workflow and platform/performance acceptance remain open.


Scifi batch verification:8088dense planes across four seed/palette variants,
162transformed back/front/both planes and624parameter/seam planes all match
strictly. STATIC and DLL each pass5cases/374assertions. Complete Windows
build (including5303-case test index),199focused host tests and make check
pass. Native129-ID search/no-match/wrap plus all three effects' animation,
pause, stop and restart checks pass. No source bias or pixel tolerance added.
Ghost1/240 integration fixture includes112independent states and6carry-over
frames. Host test-classification cache was regenerated after new phase cases;
final build log:build/pixelfx-validation/scifi-host-build-final.log.
Final screenshots and standard smoke remain pending at this checkpoint.
The244goal remains active; full Act/12moves/workflow/platform/performance
scope remains open. No architecture exception.

- Native screenshots now verified:jetpack ticks0/30/120/239, teleporter
  0/30/120/287 and hologram0/111/306/410/479. Capture checks both previews'
  emitted ticks before every frame. Retained under build/pixelfx-validation.
  Standard6s smoke passes1/1; own preview stopped; exact smoke script bytes
  restored. Final source gate running in scifi-source-check-final.log.
 129implemented; verified126until final gate0. Remaining115algorithms plus
  full Act/12moves/workflow/OBJECT/platformCI/performance remain active.

- Final make check terminal0(base origin/dev); verified129/244algorithms.
  Scifi batch ownership, injected time/seed, checked publication and source
  parity verified; no architecture exception. Remaining115algorithms and full
  Act/12moves/parameter-background workflow/OBJECT/platformCI/performance
  acceptance remain active. This is a batch checkpoint, not full completion.


Search `scifi/laser-grid` for sliding and scissoring security beams, lit steel
posts, beam-distance haze/dust, intersection flares and floor scorch/sparks.
The right preview changes seed, palette and metal ramp through checked
configuration; the metal ramp accepts1..64opaque stops, normalized to6.
Independent phase fingerprints and configuration/stale-seek tests cover the
native analytic kernel. Final batch verification is in progress.


Laser-grid batch complete:3086/3086strict parity planes; STATIC/DLL each
6scifi cases/483assertions; complete Windows build and200focused host tests
pass. make check terminal0(base origin/dev). Native130-ID search/no-match/
wrap and laser-grid animation/pause/stop/restart all pass. Engine-owned
screenshots at ticks0/30/90/180/270/359verify sweep, intersections, scorch,
haze and loop seam with both parameter variants. Standard6s smoke passes1/1;
own preview stopped and exact script bytes restored. Evidence is local under
build/pixelfx-validation/laser-grid-*. Public ownership, injected time/RNG,
checked publication and no upward module calls verified; no architecture
exception. Verified130/244; remaining114algorithms, full Act/12moves,
parameter/background workflow, OBJECT/platformCI and performance/long-session
acceptance remain active. This checkpoint does not complete the244goal.


Search `scifi/energy-core` for beating plasma inside a front/back steel cage,
two tilted orbit rings, prepared cage arcs, sparks/motes and shock rings.
The right preview changes seed, palette and opaque metal ramp through checked
configuration. The ramp accepts1..64stops and normalizes to6. Instances own
their emission recipes and arc paths; drawing and seeking use supplied time.
Independent source-frame fingerprints cover heartbeat and arc-slot edges.
Final batch verification is in progress; the244goal remains open.


Energy-core batch complete:2312dense planes (four full seed/palette cycles),
54transformed separate-layer planes and144metal/seam planes all match
strictly:2510/2510. STATIC/DLL each7scifi cases/641assertions pass, including
source heartbeat/arc-slot fingerprints, owning material publication, roundtrip
and stale-seek rejection. Complete Windows build (5305-case index),201focused
host tests and make check terminal0(base origin/dev) pass. Native131-ID
search/no-match/wrap plus energy-core animation/pause/stop/restart pass.
Engine-owned screenshots at ticks0/6/10/30/60/154/287verify heartbeat onset,
peak, echo, cooling, second beat and seam with both parameter variants.
Standard6s smoke passes1/1; own preview stopped and exact script bytes restored.
Evidence remains local under build/pixelfx-validation/energy-core-*.
Ownership, injected time/RNG and synchronous borrowed arc traversal follow
existing checked configuration/lifecycle contracts; no module/schema boundary
change or architecture exception. Verified131/244. Remaining113algorithms,
full Act/12moves, parameter/background workflow, OBJECT/platformCI and
performance/long-session acceptance remain active; the244goal is incomplete.


Search `scifi/scanner` for locking brackets, a body scan slice, cooling
outline, periodic data bits and a percentage/OK mini-font readout. The right
preview uses a custom actor, seed and palette; both use the existing layered
body/effect compositor. Actor snapshot preparation owns mask/pixel/anchor
inputs and rejects absent or malformed inputs. Full onBody Act pose remapping
remains pending in the complete244-effect scope. Batch validation is ongoing.


Scanner algorithm batch complete:3080dense seed/palette planes,3850stock/
custom/fractional/empty/low-position actor planes and54transformed separate-
layer planes strictly match:6984/6984. STATIC/DLL each9scifi cases/834assertions
pass, including source scan/readout/exit fingerprints, required actor and
malformed input rejection, owning snapshot publication, roundtrip and seek
restoration. Complete Windows build (5307-case index),203focused host tests
and make check terminal0(base origin/dev) pass. Native132-ID search/no-match/
wrap and scanner animation/pause/stop/restart all pass. Engine-owned screenshots
at ticks0/36/96/193/211/315/383verify lock, slice, scan-end, OK, exit deletion
and seam with default/custom actors. Standard6s smoke passes1/1; own preview
stopped; exact script bytes restored. Evidence stays local under
build/pixelfx-validation/scanner-*. Ownership, injected time/seed, checked
publication and synchronous draw contracts verified; no architecture exception.
Verified native algorithm inventory132/244;112algorithms remain. Force-field
is source-audited but unimplemented. Full scanner onBody Act pose remapping,
all12moves, complete parameter/background workflow, OBJECT/platformCI and
performance/long sessions remain required; the full244goal remains active.


Search `scifi/force-field` for a translucent front/back hex dome, two incoming
shots, sphere ripples, projected cracks, a climbing scan and glancing debris.
The right preview uses custom actor, radius72.5and a custom spark ramp. Radius
is0.125..256logical pixels; sparks accepts1..64opaque stops normalized to4.
Full brace-pose/hitstop/shake Act composition remains pending. Force-field
numeric comparison permits explicitly measured RGB1quantization differences
from reference inverse trig; alpha, coverage and light must remain exact.
The default comparison tool remains strict. Final batch gates are in progress.


Force-field algorithm batch complete under the approved narrow numeric
contract:2654planes accepted,1904byte-exact,1593differing RGB pixel observations
with maximum error1; alpha/coverage/light all exact. No pixel correction or
rounding bias; longitude uses reference ratio/quadrant reduction, with measured
remaining inverse-trig ULP differences. Default comparer stays strict.
STATIC/DLL each11scifi cases/2427assertions pass. Complete Windows build
(5309-case index),205focused host tests and make check terminal0(base origin/dev)
pass. Native133-ID search/no-match/wrap and force-field animation/pause/stop/
restart all pass. Engine-owned screenshots at ticks0/39/51/60/120/180/240/287
verify tracer, first hit, cracks, cooling, second hit, scan and seam, with
radius44/72.5and default/custom actor layers. Standard6s smoke passes1/1; own
preview stopped, exact smoke script bytes restored. Evidence stays local under
build/pixelfx-validation/force-field-*. Ownership, injected time/seed, atomic
configuration/seek and synchronous draw contracts verified. No architecture
exception; the numeric allowance is the plan's documented raster tolerance.
Verified native algorithm inventory133/244;111algorithms remain. Full brace
poses/hitstop/shake and all Act/12moves, complete parameter/background workflow,
OBJECT/platformCI and performance/long sessions remain required. Full244goal
remains active and incomplete.


Search `scifi/tractor-beam` for the hovering pod and cone, climbing rings,
rocks popping/hovering/spiraling into the lens, dust, grit and light streaks.
The right preview changes seed/palette and the opaque hull/stone ramps. Both
ramps accept1..64stops, normalized to6hull and5stone entries. Prepared rock/
emission data is owned by the instance; near/far layers share injected time.
This source effect has no actor input. Final batch verification is in progress.


Tractor-beam algorithm batch complete:2888dense seed/palette planes,288hull/
stone material/seam planes and54transformed separate-layer planes match
strictly:3230/3230. No new tolerance or numeric bias. STATIC/DLL each13scifi
cases/2616assertions pass, including source lift/rock/swallow fingerprints,
ramp bounds, owning publication, roundtrip and seek restoration. Complete
Windows build (5311-case index),207focused host tests and make check terminal0
(base origin/dev) pass. Native134-ID search/no-match/wrap plus tractor-beam
animation/pause/stop/restart all pass. Engine-owned screenshots at ticks0/12/
50/90/180/218/300/359cover the loop with default/material variants. Independent
source lifecycle snapshots confirm this frame set contains pop, hover, rise
and swallow states; filenames are capture tags rather than per-rock timing.
Native input/startup logs retained as tractor-beam-runtime.log and -err.log;
source lifecycle JSON and all probes/frames remain local under build/.
Standard6s smoke passes1/1; own preview stopped; exact script bytes restored.
Ownership, injected time/seed, checked publication and synchronous raster
callbacks verified; no architecture exception. Verified native algorithms
134/244;110remain. All Act/12moves, complete parameter/background workflow,
OBJECT/platformCI and performance/long sessions remain required. The full244
objective remains active and incomplete.


Search `scifi/warp-out` for the charge, white body flash, head-first breakup,
upward stream and beam lift-away. Right uses a custom actor, seed and palette.
`PixelFx::actorVisibility()` / EveScript `isActorVisible()` queries source
external-body composition policy using current bounded/wrapped playback time.
The gallery applies it to the body sprite after advance/seek, including initial
render and restart. Manual drawActor/drawActorTo still draws the original
snapshot independently. Existing effects default Visible; query allocates or
mutates nothing. Full Act pose/hitstop/shake composition remains pending.
Warp-in is source-audited but not yet implemented. Final batch gates ongoing.


Warp-out/body-visibility batch complete:2504dense seed/palette planes,
380custom/fractional/empty/lower/Hero/tail planes and54transformed separate-
layer planes match strictly:2938/2938. All1469source actor-visibility samples
match independently. Native probe exports versioned body-policy evidence;
frame comparer requires it for new reference fields and fails visibility
mismatches independently of RGB tolerance.19script tests pass; CLI negative
control confirms identical/accepted pixel planes plus wrong visibility exits1,
then matching visibility exits0 (local warp-body-negative-control/summary.json).
STATIC/DLL each17scifi cases/5893assertions pass. New public strong-enum query
uses bounded/wrapped playback time (including fractional seeks), retains no
references, changes no state/RNG and leaves manual body drawing unchanged.
All135current defaults remain Visible at time0; simulation/analytic/direct
backends share the contract. Frame-partition30/60/144Hz/.007s replay and queries
match seek snapshots. No new module/ECS/Link/schema or upward includes; no
architecture exception. Script predicate is a cheap query; gallery applies it
initially and every frame after advance/seek, without per-frame ImageData allocation.
Complete final Windows build (5315-case index),211focused host tests and
make check terminal0(base origin/dev) pass. First210-case host run also passed
before adding the replay case; final logs are warp-out-host-build-final.log,
warp-out-host-tests-final.log and warp-out-source-check-final.log.
Native135-ID search/no-match/wrap and warp-out animation/pause/stop/restart
pass. Engine-owned screenshots at ticks0/50/51/84/120/156/194/312/372verify
original body, flash, breakup, stream, lift and tail with default/custom actors.
End and tail ROIs are uniformly empty (no residual external body).50/51tick
forward/backward snapshots and paused restart restore exact rendered frames;
warp-out-body-timeline.json records all five checks true. Standard6s smoke
passes1/1; own preview stopped and exact script bytes restored. Raw input/
startup logs retained as warp-out-runtime.log/-err.log; all evidence stays
local under build/pixelfx-validation.
Verified native algorithms135/244;109remain. Warp-in is source-audited but
unimplemented; full onBody Act poses/hitstop/shake, all12moves, complete
parameter/background workflow, OBJECT/platformCI and performance/long-session
acceptance remain required. Full244goal remains active and incomplete.


### Warp-in native kernel and nonempty silhouette contract (2026-11-04)

`scifi/warp-in` is a native fixed-120-Hz kernel with injected seed/time: guide
thread, beam slam, hollow column/strands/pulses, descending rings, scanline,
row-wise body materialization and color cooling, closing thread, snap particles
and delayed motes. Width is configurable in [0,128], including 0 and 1.5.
Stock hero/dummy and owning custom snapshots share the existing actor contract.
The source samples a random occupied body cell at DONE; empty custom masks are
therefore rejected during cold preparation. A source-owned minimumActorCells
field in Descriptor drives both validation and parameter introspection
minimumFilledMaskCells, without effect-ID dispatch or changing other kernels'
empty-mask behavior. Configuration schema remains version1; the metadata adds
an ignorable constraint key. Invalid configurations cannot mutate active state.

The existing actor visibility interface uses external playback time: Hidden
before SCAN1+COOL, Visible thereafter. Preserve the source expression 1.4+.42;
it is independent of the rounded internal simulation tick. The gallery applies
this generic policy to both default and custom actor layers. Manual actor
snapshot drawing remains independent; full onBody Act pose remapping is pending.

Local reference evidence: warp-in-parity-summary.json covers3438planes across
four dense361-frame seeds/ramps, four38-time actor fixtures, four24-time width
variants and54transformed back/front/both planes.3432planes are byte-exact.
The six remaining color planes contain249differing pixel observations, maximum
RGB channel error1; all alpha, coverage and light are exact.1719external body
visibility samples are exact. Strict reports are preserved beside explicit
comparison-rgb1.json reports; the default comparator is unchanged. The standalone
MSVC/V8 numeric probes at tick95 show sin(t*31) differs by1ULP, propagating into
width and translucent color truncation; no output correction or global numeric
approximation was introduced. This uses the plan's bounded RGB1 acceptance.

Static/DLL scifi contracts each pass18cases/6020assertions. Complete Windows
build (4427-file/5316-test index),212focused host tests and make check(base
origin/dev) pass, logs warp-in-host-build.log, warp-in-host-tests.log and
warp-in-source-check.log. Native136-ID search/no-match/wrap plus WarpIn
animation/pause/stop/restart pass. Engine-owned screenshots at0/41/96/168/218/
219/260/360/432ticks cover guide/slam/scan/cooling/set/snap/end/tail. Tail keeps
the external body;218/219forward/backward and paused restart restore exact
frames. warp-in-body-timeline.json records all five checks true. Evidence stays
local under build/pixelfx-validation. Runtime captures show default and custom
actors, with width27.5in the custom variant.

Applicable architecture rules: owning immutable configuration snapshots,
structured checked Results and atomic preparation, injected simulation time/RNG,
owner-thread queries and no per-frame string dispatch, source-owned constraints,
no upward includes/new module/ECS/Link/persistent schema. No architecture exception.
Verified native algorithms136/244;108remain. Sci-fi still needs EMP blast and
Glitch. Full onBody poses/hitstop/shake/ghost/target, all12moves, complete
parameter/background UI, OBJECT/platformCI and performance/long-session gates
remain required; the complete244goal remains active and incomplete.

Warp-in final smoke: standard6s example smoke passes1/1(terminal0); own preview
was stopped after exact executable-path verification and smoke script bytes
restored. Input/startup logs retained as warp-in-runtime.log/-err.log.


### EMP blast native kernel and strict parity (2026-11-04)

`scifi/emp-blast` is a native procedural fixed-120-Hz simulation: charging puck
and frame-local arcs, white discharge/star paths, expanding scanline dome with
last-write-wins row tears and colored fringe, deterministic strobe cutouts,
crawling skin arcs, a short-out into falling/blinking dead pixels, delayed smoke
and residual sparks. It owns particle/RNG/light/arc/dead state; no JS runtime,
baked images, external entity or rendering pointers. Both radius[0,256] and
opaque fringe color are configurable through the existing checked prepared
configuration/Value path. Defaults are radius56,fringe#ff5ad8,seed33. The gallery
right preview uses radius42.5and fringe0xff70ffd0plus seed/palette/light changes.
This source effect has no actor input or Act poses; it does not claim body mapping.

Strict final evidence in emp-blast-parity-summary.json:3710/3710raw planes,
zero RGB/alpha/coverage/light differences and1855matching external actor-policy
samples. Four385-frame dense seed/ramp variants contribute3080planes; five
radius cases(0/.125/12.5/97/256)and three fringe cases contribute576planes at
36times, including pop/strobe/break/duration/tail boundaries. Transformed
back/front/both expanded-canvas checks contribute54planes. Reference revision
was freshly verified as a84159688af3ca37a4be05a0d422b94451b2e24e; no new tolerance
or algorithm approximation. Source phase fingerprints in the module test are
computed from independent pinned reference bytes, not native snapshots.

Final static and DLL scifi contracts each pass21cases/9173assertions, covering
source fingerprints, metadata/bounds, prepared-config ownership/atomic failure,
configuration roundtrip and partitioned30/60/144Hz/.007s advance/draw versus seek.
The private frame marker remains a floating-point floor value, matching source
and avoiding narrowing after long accumulated playback. Final full Windows build
(4428-file/5319-test index),215focused host cases and make check(base origin/dev)
pass: emp-blast-host-build-final.log, emp-blast-host-tests-final.log and
emp-blast-source-check-final.log. All3710planes were regenerated/rechecked after
the frame-marker cleanup (emp-blast-*-final.log).

Native137-ID search/no-match/wrap and EMP animation/pause/stop/restart pass.
Engine-owned screenshots at0/24/50/51/75/149/151/189/190/240/288/384/432ticks
show charge/pop/bubble/strobe/short-out/falling pixels/smoke/end/tail. End and tail
ROIs are empty.189/190forward/backward snapshots and paused restart restore
exact frames; emp-blast-timeline.json records five checks true. Final binary
screenshots at0/75/190/384are pixel-identical to those validated captures,
recorded by emp-blast-final-render-equivalence.json. Local images and initial/
final raw MCP/startup logs remain under build/pixelfx-validation; no evidence
artifacts are committed.

Applicable rules: source-owned declarative options, owning caller-thread state,
checked structured Results and atomic configuration, injected seed/time,
Doxygen parameter ownership, typed hot-path state without string lookup, and
existing module/layer/export ownership. No module/ECS/Link/provider/persistent
format change or architecture exception. The authoritative inventory reports
137/244, complete=false, no duplicates/extras/metadata mismatches
(emp-blast-coverage.json); its incomplete exit is intentional and not waived.
107algorithms remain, including sci-fi Glitch, plus full onBody Act mapping,
hitstop/shake/ghost/target, all12moves, full parameter/background UI,
OBJECT/platformCI and performance/long-session acceptance. Goal remains active.

Final EMP standard6s smoke passes1/1(terminal0); the task preview is stopped,
its executable path was verified before stopping, and smoke script original
bytes restored. No live task-owned preview remains.


### Glitch analytic kernel and completed sci-fi algorithm inventory (2026-11-04)

`scifi/glitch` is a native analytic effect with source-owned seven-stop palette,
seed and owning actor snapshot configuration. It reproduces the three30-Hz
corruption bursts: red/cyan ghosts, first-matching horizontal tears, interlace,
posterized/tinted body rows, edge smears, rolled/repeated slices, junk blocks,
closed-form drifting dead pixels and burst/bar light. It draws the body itself,
so the existing generic external-body policy is Hidden at every time. Gallery
initialization and frame updates apply that policy to stock/custom body layers;
manual drawActor remains an independent snapshot operation. Empty custom masks
are valid, and pixel/mask arrays remain independent as in source. One-row roll
reads outside the source pixel array are falsy; native code bounds those reads.
No JS runtime, baked images, mutable analytic state or retained external pointers.

Final strict parity:12748/12748planes, zero color/alpha/coverage/light differences,
6374matching external body-policy samples (glitch-parity-summary.json). Four
seed/ramp and seven actor variants each use577distinct120-Hz samples from0to
4.8seconds, covering two full2.4-second cycles. Actors include stock hero,
custom/fractional/lower/empty, a1x1snapshot and independent mask/pixel arrays.
Eleven actual reference time grids are asserted by glitch-time-grid.json;
three transformed front/back/both checks add54planes. No tolerance added.

The first source fingerprint run caught a verification-fixture indexing error:
--frames577oversamples one declared duration, not two periods. It repeated
rounded ticks, and the unit fixture generator incorrectly treated sample index
as tick. Verification was corrected to explicit --times and actual frames.json
time/filename lookup; all comparisons were regenerated. The corrected32phase
fixtures cover burst edges, residual pixels and both loop seams. Tool README
now documents frames-versus-times and distinct-time checks; no gate was loosened.

Static and DLL scifi contracts each pass24cases/13678assertions: independent
source fingerprints, actor ownership/empty/tiny/malformed inputs, atomic
configuration failure and roundtrip, partitioned30/60/144Hz/.007s analytic
advance/draw versus seek, and pure Hidden policy. Complete Windows build
(4429-file/5322-test index),218focused host cases and make check(base origin/dev)
pass: glitch-host-build.log, glitch-host-tests.log, glitch-source-check.log.
Native138-ID search/no-match/wrap and Glitch animation/pause/stop/restart pass.
Engine screenshots at0/35/36/64/68/132/152/204/248/252/288/289/576ticks show
calm/bursts/residuals/loop seams for default and custom actors.288and576ticks
restore initial pixels exactly;35/36forward/backward and paused restart restore
exact frames. glitch-timeline.json records seven checks true. Raw input/startup
logs and engine-owned images remain local under build/pixelfx-validation.

Applicable architecture rules: immutable configuration owner, synchronous
borrow-only painter/settings/tear access, injected absolute time and named seed,
checked structured Results/atomic publication through the existing codec,
source-owned options and typed hot-path state, documented API ownership and
existing module/export boundary. No new ECS/Link/module/provider/persistent
format, no architecture exception. Sci-fi's12native algorithms are now present
and individually verified under the plan's explicit numeric contracts. Full
onBody Act poses (including Glitch's snap poses) remain pending, not implied by
static-actor algorithm parity. Authoritative catalogue coverage is138/244,
complete=false, with no duplicate/extra/metadata mismatch;106algorithms remain.
Full Act/hitstop/shake/ghost/target, all12moves, parameter/background UI,
OBJECT/platformCI and performance/long-session acceptance remain open.
Next source audit: arcane/_lib.js and rune-circle.js read; no arcane effect is
implemented or counted yet. Full244goal remains active and incomplete.

Glitch final standard6s smoke passes1/1(terminal0); task preview was stopped
after executable-path verification, and original smoke script bytes restored.
No live task preview remains.


### Rune circle and source-owned arcane drawing helpers (2026-11-04)

`arcane/rune-circle` is a native analytic effect: perspective floor rings,
chasing lights, moving studs, a clipped counter-rotating square, stable-depth
sorted/bobbing rune glyphs, closed-form rising motes/twinkles and soft ground/
column light. Native Arcane.h ports only the currently required floorRing,
eight original3x5glyphs and mote helpers; remaining arcane helpers/effects are
not claimed implemented. Rune Circle exposes its actual source palette/seed
through the common configuration/transform/playback/light API. No new invented
radius option; source geometry remains unchanged. Gallery right preview changes
seed/palette/playback/light. No actor/Act input is required by this effect.

Final strict parity in rune-circle-parity-summary.json:7762/7762raw color/light
planes, no RGB/alpha/coverage/light differences,3881matching external policy
samples. Four seed/ramp variants each sample961distinct120-Hz times from0to8s
(two4-second cycles), contributing7688planes; transformed back/front/both
checks add54planes. Ten near-seam actual/ideal clock times add20planes. Source
revision remains a84159688af3ca37a4be05a0d422b94451b2e24e. Independent Node helper
fixtures cover all glyphs/negative indices/under-shadow/alpha/placement,
floorRing callback colors/alpha/keep and degenerate radii/flattening, and mote
zero/large velocities and intensity. C++ static/DLL helpers match their full
raw-plane fingerprints, not native-derived baselines.

Analytic clock contract was made explicit after the first partition test:
naive test-side dt accumulation and the engine's compensated actual clock can
differ around exact4/8s seams. Eight observed pairs have errors up to about
1.67e-13s and different source colors: the upstream itself changes pixels at
these near-seam times. Native matches source exactly for every identical time
(rune-circle-clock.log and build/pixelfx-rune-circle-clock/source-clock-differences.json).
Production time/algorithm were not snapped, quantized or patched. The test checks
clock error<1e-12s for its tested8.123s partitions, then compares pure analytic
pixels at the same injected actual clock. This is not a claim of identical
pixels for two distinct floating times or a global long-session error bound.
Absolute source-time samples and loop endpoints remain strict. Static/DLL
arcane contracts each pass3cases/3419assertions, including source phase
fingerprints, configuration ownership/roundtrip/atomic failure and partitioned
30/60/144Hz/.007s playback at identical actual times. No image tolerance added.

Complete Windows build (4432-file/5325-test index),221focused host cases and
make check(base origin/dev) pass: rune-circle-host-build.log,
rune-circle-host-tests.log and rune-circle-source-check.log. Native139-ID
search/no-match/wrap and Rune Circle animation/pause/stop/restart pass.
Engine-owned screenshots at0/60/120/240/360/479/480/481/960ticks cover circle
phases and both loop endpoints.480/960restore initial pixels exactly;
479/480forward/backward and paused restart restore exact frames.
rune-circle-timeline.json records six checks true. Raw input/startup logs and
screenshots remain local under build/pixelfx-validation.

Applicable rules: source-owned options and immutable program settings,
synchronous borrow-only painter/callback inputs, checked structured Results/
atomic publication, injected absolute time and separate source seed streams
(seed*5+1for motes,seed*5+2for twinkles), Doxygen cost/lifetime contracts,
existing module/exports and typed hot-path state without string lookup. No
new module/ECS/Link/provider/persistent format, no architecture exception.
Authoritative coverage reports139/244,complete=false, no duplicate/extra/
metadata mismatch.105algorithms remain, including11arcane effects. Full
Act/onBody/hitstop/shake/ghost/target, all12moves, full parameter/background UI,
OBJECT/platformCI and performance/long-session acceptance remain required.
Star-fall source header/first phase audited next; it is not implemented or
counted. Full244goal remains active and incomplete.

Rune Circle final standard6s smoke passes1/1(terminal0); task preview was
stopped after exact executable-path verification, and original smoke script
bytes restored. No live task preview remains.


### Star fall native simulation and source landing order (2026-11-05)

`arcane/star-fall` is a native fixed-120-Hz procedural simulation. It retains
the source's11stratified/shuffled landing positions, per-star launch/flight/
size/phase,20-segment tapered diagonal trails, twinkling heads, independent
landing flashes/flares, split front/back impact rings, cooling ground marks,
sparks/stars and transient lights. A single owned RNG preserves constructor,
shuffle, emitter-argument and particle-update consumption order. Fixed arrays
own the11stars and bounded landing records; an internal assertion guards the
once-only landing append. No global clock/RNG, retained external pointer, JS
runtime, baked frames or gameplay authority. Actual source palette/seed options
and common transform/playback/light configuration remain supported; no invented
star-count/shape options. This source effect has no actor/Act input.

Final strict parity in star-fall-parity-summary.json:3038/3038raw planes, no
RGB/alpha/coverage/light differences,1519matching external-policy samples.
Four seed/ramp variants each use361distinct120-Hz times from0to3s, beyond the
declared2.5s duration, contributing2888planes. Single-color and64-stop palette
cases add96planes; transformed front/back/both checks add54planes. Pinned
reference remains a84159688af3ca37a4be05a0d422b94451b2e24e; no new tolerance.
Independent source constructor fixtures give default seed13landing ticks
51/58/72/85/94/122/123/140/155/163/171and seed123ticks
48/70/76/85/95/117/126/144/150/167/182, retained locally in
star-fall-source-landings.json. Source phase fingerprints use actual reference
time-to-file mapping, not native-generated baselines or assumed sample indices.

Static/DLL arcane contracts each pass6cases/4928assertions. New cases cover
pinned trails/landing/flash/ring/end frames, partitioned30/60/144Hz/.007s replay
with intervening draws (no RNG consumption), zero tail planes, configuration
ownership/roundtrip/atomic failure and stale seek invalidation. Existing rune
helpers and analytic-clock contracts remain passing. Initial full host/source
checks were interrupted: several unrelated compiler and test subprocesses
reported1073807364without compiler diagnostics, and their process handles were
missing. Logs are preserved as star-fall-host-build.log and
star-fall-source-check.log. Both checks were rerun unmodified, without skips.
Final complete Windows build (4433-file/5328-test index),224focused host cases
and make check(base origin/dev) pass: star-fall-host-build-final.log,
star-fall-host-tests.log and star-fall-source-check-final.log.

Native140-ID search/no-match/wrap and Star Fall animation/pause/stop/restart
pass. Engine-owned screenshots at0/12/36/47/48/50/51/72/123/171/182/216/300/360
cover launch/trails/first variant/default impacts/overlapping hits/last hits/
cooling/end/tail.300and360tick ROIs are empty.50/51forward/backward snapshots
and paused restart restore exact rendered pixels; star-fall-timeline.json
records five checks true. Local raw input/startup logs and screenshots remain
under build/pixelfx-validation; no evidence artifacts are committed.

Applicable rules: source-owned declarations, immutable owning configuration,
checked structured Results/atomic publication, caller-thread state, injected
time/seed and fixed-step replay, no retained callback/painter inputs, typed
hot-path data without string lookup, existing module/export boundary. No new
module/ECS/Link/provider/persistent format or architecture exception. Coverage
reports140/244,complete=false, no duplicate/extra/metadata mismatch;104algorithms
remain, including10arcane effects. Full Act/onBody/hitstop/shake/ghost/target,
all12moves, full parameter/background UI, OBJECT/platformCI and performance/
long-session acceptance remain required. Mana-burst header/initial phases
have been read next; no mana-burst implementation is counted. Full244goal
remains active and incomplete.

Star Fall final standard6s smoke passes1/1(terminal0); task preview stopped
after executable-path verification, and original smoke script bytes restored.


### Mana burst and native source sigils (2026-11-05)

`arcane/mana-burst` is a native fixed-120-Hz simulation: inward energy streaks,
growing charge, white release/cross, floor-clipped expanding rings,96motes/stars
and20sparks, nine damped/bobbing sigils that flare and pop independently,
secondary sparks/motes/stars, pop rings and transient light. Rune motion keeps
the source's exact vy subtraction (drag plus8*dt upward bias). One owning source
RNG retains constructor/emitter/update consumption order; fixed arrays own all
nine runes and bounded pop records, guarded by an internal once-only assertion.
Actual source seed/palette and common transform/playback/light options remain
configurable; no invented count/shape options. No actor/Act input, JS runtime,
baked frames, global RNG/time or retained external pointers.

Arcane.h adds the eight original5x7SIGILS and source sigil() renderer, including
negative-index wrapping, JS rounding and four cardinal edge stamps before the
center glyph. Independent Node fixtures cover all sigils, negative indices,
edge/alpha and flipped/rotated fractional placement. Their full-plane C++
fingerprints match under static and DLL consumption; prior glyph/ring/mote
contracts remain passing. Consumers of Arcane.h were touched for deterministic
recompilation under the repository's unscanned dependency caveat.

Final strict parity in mana-burst-parity-summary.json:3038/3038raw color/light
planes, no RGB/alpha/coverage/light difference,1519matching external-policy
samples. Four seed/ramp variants use361distinct120-Hz samples from0to3s,
beyond the declared1.8s duration (2888planes). Single-color and64-stop palette
cases add96planes; transformed back/front/both checks add54planes. Reference
revision remains a84159688af3ca37a4be05a0d422b94451b2e24e. No tolerance added.
Source constructor fixtures record seed9pop ticks78/87/97/102/112/122/124/135/
141and seed123ticks80/90/97/104/110/117/126/138/145in
mana-burst-source-pops.json. Four reference seed/ramp cases are empty at216ticks
and the checked later tail times; this is measured, not inferred from duration.

Static/DLL arcane contracts each pass10cases/6504assertions. New cases cover
sigil fixtures, independent source gather/release/rune/pop/tail fingerprints,
partitioned30/60/144Hz/.007s fixed-step replay with intervening draws,
configuration ownership/roundtrip/atomic failure and stale seek invalidation.
Complete Windows build (4434-file/5332-test index),228focused host cases and
make check(base origin/dev) pass: mana-burst-host-build.log,
mana-burst-host-tests.log and mana-burst-source-check.log.
Native141-ID search/no-match/wrap and Mana Burst animation/pause/stop/restart
pass. Engine-owned screenshots at0/12/26/27/36/60/77/78/80/120/141/145/180/216/
288ticks cover gather/release/flying/settling runes/pop boundaries/afterglow/
end/tail.216and288tick ROIs are empty.77/78forward/backward snapshots and
paused restart restore exact rendered pixels; mana-burst-timeline.json records
five checks true. Raw input/startup logs and screenshots remain local under
build/pixelfx-validation, not in commits.

Applicable rules: source-owned declarations and immutable owning configuration,
checked structured Results/atomic publication, caller-thread state, injected
seed/time and fixed-step replay, synchronous painter/callback borrows,
Doxygen lifetime/cost contracts, typed hot paths and existing module/export
ownership. No new module/ECS/Link/provider/persistent format or architecture
exception. Coverage reports141/244,complete=false, no duplicate/extra/metadata
mismatch;103algorithms remain, including9arcane effects. Full Act/onBody/
hitstop/shake/ghost/target, all12moves, full parameter/background UI,
OBJECT/platformCI and performance/long-session acceptance remain required.
Spell-charge header/orb phase audited next; it is not implemented or counted.
Full244goal remains active and incomplete.

Mana Burst final standard6s smoke passes1/1(terminal0); task preview stopped
after executable-path verification, and original smoke script bytes restored.


### Spell charge and native arcane path/streak helpers (2026-11-05)

`arcane/spell-charge` is a native analytic effect:110spiralling energy streaks,
56slower motes, four independently tilted collapsing rings, a swelling/shivering
layered orb, rays/filaments, seeded forked crackles, glints, wide caster light
and floor light. It samples injected absolute time without persistent mutable
analytic/RNG state. Source streams remain distinct:seed*3+1streaks,seed*3+2motes,
seed*3+4glints,hash(floor(t*15),77)filaments andhash(seed,floor(f/2),3)crackles.
Actual palette/seed/common playback/light/placement options remain configurable.
The external actor snapshot defaults to Dummy and accepts custom/Hero/empty/
None inputs; the effect itself uses the source's fixed hand-space coordinates
and does not dereference actor pixels. Actor None therefore remains valid and
manual drawActor clears its target. Full HOLD/READY Act poses remain pending.

Arcane.h adds native streak(), owning arc() main/fork construction and three-pass
drawArc(). Source midpoint/fork RNG order, zero-length depth handling,
main/fork widths, halo/body/core threshold comparisons and sparse light stamps
are preserved. Independent Node fixtures cover zero/large streak velocities,
alpha/flip/rotation, zero-length and forked paths, all ink thresholds, branch
sizes and post-construction RNG output. Static/DLL tests match full raw-plane
fingerprints and exact RNG/shape metadata. Path/RNG/painter inputs are synchronous
borrows; arc results own their points and are reused across three ink passes.
Relevant Arcane.h consumers were touched before compilation for unscanned deps.

Final strict parity in spell-charge-parity-summary.json:7754/7754raw planes,
zero RGB/alpha/coverage/light differences,3877matching external-policy samples.
Four seed/ramp variants and six actor variants each use385distinct120-Hz times
from0to3.2s(two1.6s cycles); three transformed back/front/both checks add54planes.
Actor cases cover custom/fractional/empty/lower/Hero/None snapshots. No new
tolerance or time quantization. Pinned revision remains
a84159688af3ca37a4be05a0d422b94451b2e24e. Static/DLL arcane contracts each pass
14cases/8214assertions: source helper/phase fingerprints, same-actual-clock
partitioned30/60/144Hz/.007s playback/draw purity, actor/configuration ownership,
roundtrip/atomic failure and stale seek invalidation. For the tested3.333s
partitions, clock error versus ideal targets stays<1e-12s; exact pixels are
compared at identical injected times, preserving the earlier analytic contract.

Complete Windows build (4435-file/5336-test index),232focused host cases and
make check(base origin/dev) pass: spell-charge-host-build.log,
spell-charge-host-tests.log and spell-charge-source-check.log.
Native142-ID search/no-match/wrap and Spell Charge animation/pause/stop/restart
pass. Engine-owned screenshots at0/11/12/36/59/60/84/108/156/191/192/193/384ticks
cover ring landings, swirl/crackles, default/custom body composition and loop
endpoints.192and384restore initial rendered pixels exactly;59/60forward/backward
snapshots and paused restart restore exact frames. spell-charge-timeline.json
records six checks true. Raw input/startup logs and screenshots remain local
under build/pixelfx-validation, not in commits.

Applicable rules: source-owned declarations, immutable owning configuration,
checked structured Results/atomic publication, caller-thread state, injected
time/named source RNG streams, synchronous callback/painter/path borrows,
Doxygen ownership/cost contracts, typed hot paths and existing module/exports.
No module/ECS/Link/provider/persistent-format change or architecture exception.
Coverage is142/244,complete=false, with no duplicate/extra/metadata mismatch;
102algorithms remain, including8arcane effects. Full Act/onBody/hitstop/shake/
ghost/target, all12moves, full parameter/background UI, OBJECT/platformCI and
performance/long-session acceptance remain required. Arcane-orb source header/
ring phase read next; it is not implemented or counted. Full244goal stays active.

Spell Charge final standard6s smoke passes1/1(terminal0); task preview stopped
after executable-path verification, and original smoke script bytes restored.


2026-11-05 Arcane Orb native algorithm validation
------------------------------------------------
ArcaneOrb now owns the pinned arcane/arcane-orb.js analytic kernel: translucent
glass, four spiral arms, tilted rear/front rings, beads and orbiting/drifting
motes. Source palette/seed plus common placement, playback and light settings
remain procedural; no baked animation or source-specific numeric correction.
The 2.4-second loop is sampled from external absolute time.

Strict parity passes 4766/4766 color/light planes across nine cases, including
four seed/ramp variants at 577 distinct ticks over two loops, single/64-stop
ramps and transformed Back/Front/Both canvases. Actor policy is exact for all
2383 samples. Partial alpha and ordered Back+Front composition have independent
contract tests. Static and DLL arcane contracts pass 17 cases/10451 assertions;
235 host PixelFX tests pass. The full Windows Debug build and make check pass;
the dependency index records 4436 files/5339 tests. Initial restricted Ninja
index access was denied; the authorized standard build rerun passes unchanged.
Native gallery visits all 143 IDs and passes search, wrap, animation, pause,
stop and restart checks. Evidence remains ignored under build/pixelfx-validation.

Applicable rules: source-owned declarations, owning immutable configuration,
checked structured Results/atomic publication, caller-thread state, injected
time/RNG, synchronous painter borrows, typed hot paths and existing exports.
No module/ECS/Link/provider/persistent-format change or architecture exception.
Coverage is143/244,complete=false, with no duplicate/extra/metadata mismatch.
101algorithms remain, including7arcane effects. Full Act/onBody/hitstop/shake/
ghost/target, all12moves, full parameter/background UI, OBJECT/platformCI and
performance/long-session acceptance remain required. Arcane Beam source read
next; it is not implemented or counted. Full244goal stays active.

Arcane Orb native timeline passes all six exact loop/reverse/restart checks.
The standard6s example smoke passes1/1(terminal0); the task preview is stopped
after executable-path verification and original smoke script bytes restored.


2026-11-05 Arcane Beam native algorithm validation
-------------------------------------------------
ArcaneBeam now owns the pinned arcane/arcane-beam.js analytic kernel: banded
beam/dithered edge, three twisting strands, source/end flares, expanding shock
rings, falling sparks, vapour and motes. Source palette/seed plus common
placement/playback/light settings remain procedural. Its loop is1.2seconds.
The gallery supplies default Dummy and custom actor examples; the source AIM/
PULL Act poses, target/hits/dust remain part of the pending full Act workflow.

Nine parity cases cover2462color/light planes: four seed/ramp variants at289
distinct ticks over two loops, single/64-stop ramps and transformed Back/Front/
Both canvases.2366planes are exact; all2462are accepted at explicit RGB<=1.
Alpha, coverage, light and all1231actor-policy samples remain exact. Parameters
and canvas cases are fully exact. The scoped tolerance follows measured native/
V8 libm evidence, not a production correction: at tick11, vapour sample3 has
sin(q*pi)=0.67378331405140834(native) versus0.67378331405140846(JS). Its blue
blend becomes236versus235.99999999999997before source integer truncation.
The original strict comparison and the independently built numeric probe are
retained under ignored build/pixelfx-validation/arcane-beam-numeric. No global
tolerance, quantization or source-specific numeric adjustment was introduced.

Static and DLL arcane contracts each pass20cases/14943assertions, including
source alpha/light fingerprints, sampled RGB, dither cells, empty rear layer,
ordered composition, replay/clock partitioning and owning atomic configuration.
The standard full Windows Debug build and make check pass; source dependency
index records4437files/5342tests. Source ownership/license notices are updated.

Applicable rules: source-owned declarations, owning immutable configuration,
checked structured Results/atomic publication, caller-thread state, injected
time/named RNG streams, synchronous painter borrows, Doxygen contracts, typed
hot paths and existing module/exports. No module/ECS/Link/provider/persistent-
format change or architecture exception. Coverage is144/244,complete=false,
with no duplicate/extra/metadata mismatch.100algorithms remain, including6
arcane effects. Full Act/onBody/hitstop/shake/ghost/target, all12moves, full
parameter/background UI, OBJECT/platformCI and performance/long-session
acceptance remain required. Arcane Missiles source audited next (five owning
bolts/trails, shared ordered RNG/particles/lights); it is not implemented or
counted. Full244goal stays active. Evidence stays local under build/.

Native integration caught and corrected a missing descriptor actor option;
the gallery custom-actor publication now follows the declared parameter schema.
A regression asserts actor presence and configuration round-trip. Final source
gate and complete Windows build are rerun after the fix and pass.

Final host PixelFX suite passes238/238tests. Native gallery visits all144IDs
and passes search/no-match/wrap plus animation/pause/stop/restart with both
stock and custom actor previews; source AIM/PULL poses remain pending.

Arcane Beam native timeline passes all six exact loop/reverse/restart checks.
Engine-owned quarter-cycle image confirms the visible default/custom previews.

Arcane Beam final standard6s smoke passes1/1(terminal0); task preview stopped
after executable-path verification and original smoke script bytes restored.


2026-11-05 Arcane Missiles native algorithm validation
-----------------------------------------------------
ArcaneMissiles owns the pinned arcane/arcane-missiles.js fixed120Hz simulation:
five staggered bolts,18-point newest-first ribbons, homing steering, lock-on
brackets, ordered pops/sparks/stars, shared source RNG/particles/lights and
rear floor light. Source palette/seed and common placement/playback/light/actor
configuration are procedural. Default Dummy and custom actor gallery entries
use the declared actor option. Full source OPEN/KICK/GATHER Act and target/hit
responses remain pending in the full Act workflow.

Nine comparison cases pass2078/2078strict color/light planes and1039exact
actor-policy samples. Four seed/ramp cases cover241distinct ticks0..2seconds;
single/64-stop ramps and transformed Back/Front/Both canvases also pass.
All sampled effect planes at/after1.75seconds are empty in all four dense
cases. No tolerance or numeric correction was added. The first comparison
caught C++ operand evaluation reordering launch spark RNG draws; explicit
angle-then-speed scalar statements now preserve JS order. Static and DLL
arcane contracts each pass24cases/16735assertions: dual-seed upstream FNV
fixtures around launches/hits, clock partitions, replay/draw purity, atomic
owning configuration, actor option presence and ordered layers/reverse seek.
Default source hit ticks94/103/115/132/146and variant94/104/111/131/145are
independently extracted from the read-only pinned source for native inspection.

Applicable rules: source-owned descriptors, owning bounded bolt/trail/pop state,
checked structured Results/atomic publication, caller-thread simulation, injected
time and source RNG ordering, synchronous painter borrows, existing module and
exports. No module/ECS/Link/provider/persistent-format change or architecture
exception. Coverage is145/244,complete=false, with no duplicate/extra/metadata
mismatch.99algorithms remain, including5arcane effects. Full Act/onBody/
hitstop/shake/ghost/target, all12moves, full parameter/background UI, OBJECT/
platformCI and performance/long-session acceptance remain required. Full244
goal stays active; generated probes/reports/images/logs remain ignored build/.

Standard full Windows Debug build and make check pass. The source dependency
index records4438files/5346tests; focused host suite passes242/242PixelFX
cases. No full local CTest run was used.

Independent native/source event probes compare four seeds,20hit events and
100bolt snapshots: hit times/on-state/trail lengths exact, maximum position
error0. Native gallery visits all145IDs and passes search/no-match/wrap and
animation/pause/stop/restart using default/custom actor previews.

Arcane Missiles native timeline passes all five checks: terminal/tail identical,
before-hit and hit restored, reverse restores trail, restart restores initial.
Engine-owned screenshots were inspected. Blink In/Out source and shades/
squeezed actor helpers were read for the next port; neither is counted yet.

Arcane Missiles final standard6s smoke passes1/1(terminal0); task preview
stopped after executable-path verification and original smoke bytes restored.


2026-11-05 Blink In native algorithm validation
----------------------------------------------
BlinkIn owns the pinned arcane/blink-in.js fixed120Hz simulation:230spiralling
bits, gathering streaks, growing line, squeezed white silhouette, cooling body
light/outline, split floor ring and arrival stars/sparks. Actor visibility uses
external playback time: hidden before0.48seconds, visible from0.48seconds.
Source actor/palette/seed plus common placement/playback/light are configurable.
The descriptor requires an actor but permits an empty binary mask, as upstream.
Default Dummy and custom actor gallery entries use the same checked schema.
Full source onBody/LANDED poses, landing hitstop/shake/dust remain pending in
the full Act workflow; static snapshot algorithm parity does not claim these.

Sixteen cases pass4892/4892strict color/light planes and2446exact actor-policy
samples, with no RGB tolerance. Four seed/ramp variants cover241distinct ticks
0..2seconds. Seven actor cases cover201ticks each: custom, fractional placement,
empty mask, lower origin, stock Hero, tiny mask and independent mask/pixels.
Single/64-stop ramps and transformed Back/Front/Both canvases also pass.
Arcane.h owns source shades/squeezed helpers: cold owning luminance bands and
synchronous allocation-free squeeze raster. Independent JS fixtures cover
luma thresholds, absent pixels, sx<=.02, width expansion and fractional x.

Static and DLL arcane contracts each pass29cases/18703assertions, including
source dual-seed planes, fixed-step replay/draw purity, exact visibility boundary,
ordered composition/reverse seek, actor requirements, empty masks, configuration
ownership/roundtrip and failure atomicity. The full-catalogue initial visibility
contract also passes1case/519assertions. A new test initially used iterators
from two settings() copies; it was corrected to retain one owning copy and all
contracts rerun. Generated diagnostic evidence remains local under build/.

Applicable rules: source-owned descriptors, owning actor/mask/shade/bit state,
checked structured Results/atomic publication, caller-thread simulation, injected
time and explicitly ordered source RNG, synchronous painter/span borrows,
Doxygen ownership/cost contracts and existing module/exports. No module/ECS/
Link/provider/persistent-format change or architecture exception. Coverage is
146/244,complete=false, with no duplicate/extra/metadata mismatch.98algorithms
remain, including4arcane effects. Full Act/onBody/hitstop/shake/ghost/target,
all12moves, full parameter/background UI, OBJECT/platformCI and performance/
long-session acceptance remain required. Full244goal stays active.

Final standard Windows Debug build and make check pass after the test fix;
the source dependency index records4439files/5351tests.

Final host suite passes247/247PixelFX cases. All sampled terminal/tail effect
planes at/after1.4seconds are empty across dense and actor cases.

Native gallery visits all146IDs and passes search/no-match/wrap plus
animation/pause/stop/restart checks with default/custom Blink In previews.
Blink Out full source was read for the next port; it is not implemented or
counted here. It shares the validated shades/squeezed actor helpers.

Native timeline passes all five exact terminal/reverse/restart checks. Each
sampled step asserts both preview actors hidden through tick57and visible
from tick58. Engine-owned line/open and arrival images were inspected.

Blink In final standard6s smoke passes1/1(terminal0); task preview stopped
after executable-path verification and original smoke script bytes restored.


2026-11-05 Blink Out native algorithm validation
-----------------------------------------------
BlinkOut owns pinned arcane/blink-out.js fixed120Hz simulation: gathering
streaks/body light, flash, squeeze-to-line/collapse,142burst particles, split
floor ring and translucent rear afterimage. External actor visibility is
visible before0.32seconds and hidden thereafter; source pop time is0.52seconds.
Actor snapshot, palette/seed and common placement/playback/light are configurable.
Actor is required; empty masks are valid as upstream. Default Dummy/custom
actor gallery entries share checked configuration and the existing Arcane.h
shades/squeezed helpers. Source onBody/IN poses remain pending full Act work.

Sixteen cases pass4892/4892strict color/light planes and2446exact actor-policy
samples, no tolerance. Four seed/ramp cases cover241ticks0..2seconds; seven
actor cases cover201ticks each (custom/fraction/empty/lower/Hero/tiny/independent).
Single/64-stop ramps and transformed Back/Front/Both canvases also pass.
All sampled terminal/tail effect planes at/after1.6seconds are empty across
dense and actor cases. Static and DLL arcane contracts each pass33cases/
20641assertions, covering independent dual-seed source planes at flash/squeeze/
pop/ghost boundaries, replay/clock partitions, external visibility, owning
configuration/failure atomicity, empty masks, strict layer order and partial
alpha in the ghost. Full-catalogue initial policy passes1case/522assertions.

Applicable rules: source-owned descriptors, owning actor/shade state, checked
structured Results/atomic publication, caller-thread simulation, injected time/
source RNG ordering, synchronous painter/span borrows and existing module/
exports. No module/ECS/Link/provider/persistent-format change or architecture
exception. Coverage is147/244,complete=false, with no duplicate/extra/metadata
mismatch.97algorithms remain, including3arcane effects. Full Act/onBody/
hitstop/shake/ghost/target, all12moves, full parameter/background UI, OBJECT/
platformCI and performance/long-session acceptance remain required. Full244
objective stays active; generated evidence remains ignored under build/.

Standard full Windows Debug build and make check pass; the source dependency
index records4440files/5355tests.

Focused host suite passes251/251PixelFX tests. No full local CTest run was
used; the standard build and focused graphics-domain executables are verified.

Native gallery visits all147IDs and passes search/no-match/wrap, animation,
pause, stop and restart with default/custom Blink Out previews. The Magic
Circle Cast source metadata/setup were read next; it is not implemented or
counted here.

Native timeline passes all six exact checks: terminal/tail empty and identical,
pre-flash and flash restored, reverse restores body, restart restores initial.
Every sampled step checks both preview actors visible through tick38and hidden
from tick39. Engine-owned flash and ghost images were inspected.

Blink Out final standard6s smoke passes1/1(terminal0); task preview stopped
after executable-path verification and original smoke script bytes restored.


2026-11-05 Magic Circle Cast native algorithm validation
-------------------------------------------------------
MagicCircleCast owns pinned arcane/magic-circle-cast.js fixed120Hz simulation:
three pen-drawn rings, timed hexagram edges,32ticks,8runes that ignite and go
out sequentially, gathering streaks, climbing thread/column/halos, shocks,
flash and breakup glints. Source palette/seed and common placement/playback/
light remain procedural. The source has no actor/Act; this effect adds none.
Far circle/runes plus column belong to Back, near geometry/pens/flash to Front;
source particle layer and shared RNG ordering are preserved. Empty edge picks
consume one RNG value before selecting source edge0, matching JS nullish fallback.

Nine cases pass4382/4382strict color/light planes and2191exact actor-policy
samples. Four seed/ramp variants cover529distinct ticks0..4.4seconds; single/
64-stop ramps and transformed Back/Front/Both canvases also pass. All dense
sampled effect planes at/after3.9seconds are empty. No tolerance or numerical
correction was added. Independent native/source probes compare2116state samples
across4seeds: time/RNG/particle counts/light counts/stage flags exact. Flash is
at183, breakup291; rune-out ticks315/325/334/344/353/363/373/382.

Static and DLL arcane contracts each pass37cases/23570assertions, including
independent dual-seed source FNV fixtures at phase boundaries, fixed-clock
partitioning/replay/draw purity, owning configuration and failure atomicity,
ordered far/column/near composition and reverse replay. Standard full Windows
Debug build and make check pass; dependency index4441files/5359tests. Evidence
stays ignored under build/pixelfx-validation and reference/native dump folders.

Applicable rules: source-owned descriptors, owning bounded geometry/simulation,
checked structured Results/atomic publication, caller-thread state, injected
time and ordered source RNG, synchronous painter borrows, typed hot paths and
existing module/exports. No module/ECS/Link/provider/persistent-format change
or architecture exception. Coverage is148/244,complete=false, with no duplicate/
extra/metadata mismatch.96algorithms remain, including2arcane effects. Full
Act/onBody/hitstop/shake/ghost/target, all12moves, full parameter/background UI,
OBJECT/platformCI and performance/long-session acceptance remain required.
Full244objective stays active.

Focused host suite passes255/255PixelFX cases. Portal and Time Stop source
headers/setup were read for the next arcane ports; neither is counted here.

Native gallery visits all148IDs and passes search/no-match/wrap plus
animation/pause/stop/restart checks. Timeline passes six exact terminal/reverse/
restart checks; end/tail are empty. Engine-owned full-column and first-rune-out
breakup images were inspected. Source flash183and breakup291phases are shown
in the native frame-step path, with no eval-based gameplay route.

Magic Circle Cast final standard6s smoke passes1/1(terminal0); task preview
stopped after executable-path verification and original smoke bytes restored.


2026-11-05 Portal native algorithm validation
--------------------------------------------
Portal owns pinned arcane/portal.js analytic drawing: dithered fibrous/log-spiral
well, knotted arms and shrinking tunnel bands, noisy oval rim/hot spots, deep
specks, orbiting/infalling motes/streaks, floor skaters and rim/floor lighting.
New owning PixelFxArcaneSettings stores portalWidth/portalHeight. Source-owned
options width/height accept[.125,128], defaults17/29; invalid values fail existing
structured preparation atomically. The typed Option codec owns the arcane field
access; no per-frame string lookup or unrelated-family settings are used.
Arcane.h tone matches the source dither/sharpness helper. The source has no Act
or actor. Absolute external time drives the2.4second loop, without quantization.

Four seed/ramp variants at577distinct ticks0..4.8seconds, single/64-stop ramps,
transformed Back/Front/Both canvases and five geometry variants (tiny/half/wide/
tall/maximum) pass4906/4906strict color/light planes and2453exact actor-policy
samples. No tolerance/correction was added. Independent source tone fixtures
cover negative/zero/high intensity, lo0and sharpness0/.65/1. Static and DLL
arcane contracts each pass42cases/25989assertions: source FNV planes, analytic
clock partitioning/replay, ordered layers, width/height bounds including NaN/Inf,
configuration round-trip, owning candidates and failure atomicity.

The public settings value layout addition required rebuilding its consumers:
all stylize implementation/test and probe translation units were invalidated;
no settings-header includes were found outside that scope. Standard full Windows
Debug build and make check pass, index4442files/5364tests. Gallery right variant
publishes width24/height23with changed seed/palette/playback/light through one
checked prepare/commit. Existing inactive fields stay out of serialized options;
no persistent schema/provider/module/ECS/Link boundary was introduced.

Applicable rules: source-owned declarations, owning immutable configuration,
checked structured Results/atomic publication, caller-thread state, injected
time/source RNG, synchronous painter/span borrows, Doxygen ownership and typed
hot paths. No architecture exception. Coverage is149/244,complete=false, with
no duplicate/extra/metadata mismatch.95algorithms remain, including Time Stop
to finish the arcane family. Full Act/onBody/hitstop/shake/ghost/target, all12
moves, full parameter/background UI, OBJECT/platformCI and performance/long-
session acceptance remain required. Full244objective stays active; evidence
remains ignored under build/.

Focused host suite passes260/260PixelFX tests after the public settings layout
addition, covering the existing family contracts as well as Portal. Native
gallery visits all149IDs and passes search/no-match/wrap plus animation/pause/
stop/restart; the changed geometry snapshot prepares and publishes successfully.

Native Portal timeline passes six exact two-loop/reverse/restart checks.
Engine-owned quarter-cycle image was inspected: the right width24/height23
portal is visibly wider/shorter than the default17/29, with both floor anchors
aligned and procedural rim/arms/motes active.

Portal final standard6s smoke passes1/1(terminal0); task preview stopped
after executable-path verification and original smoke script bytes restored.


2026-11-05 Time Stop native algorithm validation
-----------------------------------------------
TimeStop owns pinned arcane/time-stop.js fixed120Hz simulation: springing
clock face/glass, eight decelerating hand ticks/ripples, separately integrated
mote time, drained ramp, frozen hand tremor, recursive walking cracks and
74tumbling fragments plus glints/sparks/vapour. Literal clockTime integration
and RNG draw/branch order are preserved, including recursive child-before-parent
crack storage. Source palette/seed plus common placement/playback/light remain
configurable. Source has no actor or Act; this kernel introduces none.

Nine cases pass3998/3998strict color/light planes and1999exact actor-policy
samples: four seed/ramp variants at481distinct ticks0..4seconds, single/64-stop
ramps and transformed Back/Front/Both canvases. All dense samples at/after3.6
seconds are empty. No tolerance, snapping or numeric correction was introduced.
Independent native/source state probes compare1924samples across4seeds: time,
integrated clock, RNG, tick/freeze/shatter flags and debris/particle/light counts
are all exact. Tick events44/56/69/84/103/124/149/179; first freeze211and
shatter291. The nominal1.75second threshold first crosses at211under upstream
naive fixed-step accumulation; production preserves that source behavior.

Static and DLL arcane contracts each pass46cases/28688assertions. New contracts
cover independent dual-seed source FNV planes at tick/freeze/crack/shatter/tail
boundaries, fixed-clock partitions/replay/draw purity, owning configuration and
failure atomicity, partial glass alpha, ordered layer composition and reverse
replay. Full standard Windows Debug build and make check pass; source index
4443files/5368tests. Evidence remains ignored under build/.

Applicable rules: source-owned descriptors, owning bounded simulation/geometry,
checked structured Results/atomic publication, caller-thread state, injected
time/source RNG ordering, synchronous painter borrows and existing module/
exports. No module/ECS/Link/provider/persistent-format change or architecture
exception. Coverage is150/244,complete=false; no duplicate/extra/metadata
mismatch. All12arcane algorithms are implemented.94algorithms remain across
moves/melee/spawn/shadow/status/nature/ui/transitions. Full Act/onBody/hitstop/
shake/ghost/target, all12moves, full parameter/background UI, OBJECT/platformCI
and performance/long-session acceptance remain required. Completing the arcane
algorithm inventory does not complete those workflows or the full244objective.

Focused host suite passes264/264PixelFX tests. Re-read the original delivery/
acceptance plan: formal editor/Inspector integration stays outside scope, while
real-time gallery parameters/backgrounds and full Act remain mandatory. After
this arcane milestone, continue the approved algorithm order with Shadow.

Native gallery visits all150IDs and passes search/no-match/wrap plus playback/
pause/stop/restart. Timeline passes six exact terminal/reverse/restart checks;
rewind across210/211restores the warm/frozen frames, and end/tail are empty.
Engine-owned cold-clock and tumbling-shard images were inspected.

Time Stop final standard6s smoke passes1/1(terminal0); task preview stopped
after executable-path verification and original smoke script bytes restored.

Black Hole (`shadow/black-hole`) adds a four-second procedural loop with bent
accretion bands, a photon ring, inward motes and heated rock orbits. The typed
`shadow.rock` ramp accepts1..64colors and owns four normalized stops; the right
preview changes this material as well as seed/palette/playback/light.

Verified algorithm coverage is151/244;93remain. Black Hole passes7902strict
source color/light planes,3951actor-policy samples, static and DLL Shadow
contracts(5cases/3541assertions each),269focused host tests, full Windows Debug
rebuild and make check. Full Act, gallery parameter/background controls,
OBJECT/platform CI and performance/long-session acceptance remain pending.

The native gallery has visited all151IDs; Black Hole passes six exact loop,
reverse and restart checks. Its engine-owned rendered frame was inspected.

Standard6s example smoke passes1/1; the preview is stopped and the smoke script
is restored. No architecture exception was taken.

Curse (`shadow/curse`) adds gathering wisps, a stamping/burning/cracking skull,
falling bones and violet body flood with a crawling outline. It accepts an
optional stock/custom actor and an owning `shadow.bone` material ramp of1..64
colors normalized to five source steps. The right gallery preview changes the
actor, bone material, seed/palette/playback/light. Duration3.4seconds; at120Hz
stamp66,first crack207and breakup238are useful inspection frames.

Algorithm coverage152/244;92remain. Curse passes9130strict source color/light
planes and4565actor-policy samples across17cases, including absent/custom and
empty actors, transformed layers and material extremes. Static/DLL Shadow
contracts each pass11cases/6558assertions; full Windows Debug build and
make check pass. The reference adapter now really exercises absent simulation
actors instead of letting the source wrapper substitute a default actor.
Full source Act, parameter/background UI, OBJECT/platformCI and performance/
long-session acceptance remain pending.

Native gallery navigation visits all152IDs. Curse's six exact terminal/reverse/
restart checks pass, including65/66stamp and237pre-break restoration. Its body
flood and settled-bone engine screenshots were inspected.275focused host tests
and the final source gate pass. Full source Act is still pending.

The standard6s example smoke passes1/1; preview stopped and smoke script restored.
No architecture exception was taken.

Dark explosion (`shadow/dark-explosion`) adds a gathering core, flash cut by a
black star, a rim-burning dark sphere, lit obsidian shards, ground shock rings,
collapse, a second pop and rising smoke. Its source palette/seed are configurable;
the right preview changes seed/palette/playback/light. Source duration3.8seconds.
At120Hz useful boundaries are release63,collapse195and second pop221; the native
simulation can continue through the smoke tail after the nominal duration.

Algorithm coverage153/244;91remain. Dark Explosion passes5044strict raw source
color/light planes and2522actor-policy samples across9cases. Static/DLL Shadow
contracts each pass18cases/24926assertions. Full Act, gallery parameter/background
UI, OBJECT/platformCI and performance/long-session acceptance remain pending.

Windows Debug build,282focused host tests and make check pass. Native gallery
visits all153IDs and Dark Explosion passes six exact tail/reverse/restart checks,
including release62/63restoration. Its engine-owned black-star and burning-sphere
frames were inspected. No architecture exception was taken.

Standard6s example smoke passes1/1; task preview is stopped and smoke script
restored. Eclipse is the next Shadow algorithm.

Eclipse (`shadow/eclipse`) adds twinkling sky stars,30curved streamers, a
breathing pearl corona, dark moon/earthshine and a blazing diamond bead.
`shadow.pearl` accepts1..64colors and owns four normalized stops. The right
preview changes pearl material, seed/palette/playback/light. Its4second loop
wraps at480ticks; comma/period can compare the seam at120Hz.

Algorithm coverage154/244;90remain. Eclipse passes7902strict raw source
color/light planes and3951actor-policy samples across11cases, including two
loops, transformed layers and palette/material extremes. Static/DLL Shadow
contracts each pass22cases/28461assertions. Full Act, gallery parameter/
background UI, OBJECT/platformCI and performance/long-session acceptance remain
pending.

Windows Debug build,286focused host tests and make check pass. Native gallery
visits all154IDs; Eclipse passes six exact loop/reverse/restart checks. Its
engine-owned corona/streamer/bead frames were inspected. No architecture
exception was taken.

Standard6s example smoke passes1/1; task preview stopped and smoke script
restored. Grasping Hands is the next Shadow algorithm.

Grasping hands (`shadow/grasping-hands`) adds four independent articulated shadow
arms, five-digit claws, splash drops, clench flashes and a closing rippled pool.
It has no source actor input. Palette/seed and native playback/light are
configurable; the right preview changes them. Source duration3.1seconds. With
default seed5, first emergence is tick37and first clench flash81at120Hz; all
four clenches have fired by102and last down-splash is241.

Algorithm coverage155/244;89remain. Grasping Hands passes3998strict raw source
color/light planes and1999actor-policy samples across9cases. Static/DLL Shadow
contracts each pass27cases/31331assertions, including independent ink-drop
impact/state/order fixtures. Full Windows Debug build and make check pass.
Full Act, parameter/background UI, OBJECT/platformCI and performance/long-session
acceptance remain pending.

291focused host tests pass. Native gallery visits all155IDs and Grasping Hands
passes six exact end/reverse/restart checks, including80/81clench restoration.
Its engine-owned articulated-arm/pool frames were inspected. No architecture
exception was taken.

Standard6s example smoke passes1/1; task preview stopped and smoke script
restored. Hex Sigil is the next Shadow algorithm.

Hex sigil (`shadow/hex-sigil`) adds timed burning pen strokes, a seven-pointed
star, seven seeded glyphs that pop in sequence, a white flare and tumbling
fragments. It is a one-shot effect; palette/seed and native playback/light are
configurable. The right preview changes those values. Source duration4.2seconds.
At120Hz the first glyph pops195,white flare313and breakup329; compare312/313
with comma/period. The source geometry has seven points despite the effect name.

Algorithm coverage156/244;88remain. Hex Sigil passes5092strict raw source
color/light planes and2546actor-policy samples across9cases. A separate probe
matches2404clock/cursor/RNG/flag/count snapshots and28glyph geometries exactly.
Static/DLL Shadow suites each pass31cases/34057assertions; full Windows Debug
build and make check pass. Full Act, parameter/background UI, OBJECT/platformCI
and performance/long-session acceptance remain pending.

295focused host tests pass. Native gallery visits all156IDs; Hex Sigil passes
six exact end/reverse/restart checks, including312/313flare restoration. Its
engine-owned complete-glyph and falling-fragment frames were inspected. No
architecture exception was taken.

Standard6s example smoke passes1/1; task preview stopped and smoke script
restored. Life Steal is the next Shadow algorithm.

Life steal (`shadow/life-steal`) adds seven hovering blood orbs that whip back
to the caster, tapered comet trails, impact ink and a crimson body flush. Actor
input can be Dummy/Hero/custom/absent. Palette/seed and native playback/light are
configurable. `dark` preserves the source option and roundtrips, but the pinned
source does not use it visually. Target impact is fixed at(104,-24). Source Act
target/pose/hitstop/shake presentation remains pending.

Source duration2.2seconds. Default seed11arrivals are97/103/113/122/128/136/145
at120Hz; compare96/97for the first flush and144/145for the last arrival.
Algorithm coverage157/244;87remain. Life Steal passes9156strict raw source
color/light planes across15cases, plus1924state samples with all orb/trail
scalars identical. Static/DLL Shadow suites each pass37cases/36815assertions.
Windows Debug build (including index) and make check pass. An initial index
log-access failure is retained in local evidence; isolated rebuilding completed
the same gate without a skip. Full Act, parameter/background UI, OBJECT/platformCI
and performance/long-session acceptance remain pending.

Life Steal native acceptance: 301/301 focused host tests pass. The live gallery
visited all157 native IDs; search, wrap, animation, pause, stop and restart pass.
Engine-owned screenshots were inspected for hover, homing and actor flush.
All6timeline assertions pass, including96/97and144/145arrival boundaries,
reverse replay, restart and stable end/tail. Six-second example smoke passes1/1;
the owned preview is stopped and smoke script bytes restored. Evidence remains
local under build/pixelfx-validation/life-steal-* . Next: shadow/necro-skulls.

Necro Skulls native algorithm milestone: 158/244. Ported the pinned source's
five skulls (N=5, despite its six-skull comment), stable tilted depth ordering,
12-row bone sprite, eye glows, eight-step ghost tails, cold noise flame, seeded
cyclic motes and halo. Bone is an owning configurable 1..64-color ramp fitted
to five stops, with checked preparation/publication and atomic failure. Analytic
time and named source RNG remain injected; fixed five-entry insertion sorting
retains stable source ordering without draw-time allocation. No ECS/Link/provider/
persistent-format change or module boundary change; no architecture exception.
Static/DLL Shadow suites each pass40cases/36970assertions. Standard Windows
Debug build passes including index4455files/5408tests. Coverage158/244,
complete=false;86algorithms remain, including4Shadow. Full Act/onBody/poses/
hitstop/shake/ghost/target,12moves, gallery parameter/background UI, OBJECT/
platformCI and performance/long-session acceptance remain required.

Necro Skulls pinned parity: all6116raw color/light planes are exact across10
cases: default/seed123+palette/zero/max seeds at every tick over two loops,
transformed Back/Front/Both, single palette+bone,64-stop palette and64-stop bone.
All304focused host PixelFX tests pass. Evidence: ignored build/pixelfx-validation/
necro-skulls-parity-summary.json and necro-skulls-host-tests.log.

Necro Skulls native acceptance: make check passes against origin/dev. The live
gallery visited all158native IDs, with search/no-match/layout restoration and
wrap exact; animation/pause/stop/restart pass. Engine-owned screenshots were
inspected at orbit-quarter and live playback, showing front/rear flame occlusion
and independent right-side bone/flame material variation. All6timeline checks
pass: first/second loop restore initial, first/second next-frame equality,
reverse restores359, restart restores initial. Evidence stays local build/.
Next approved source algorithm: shadow/shadow-flames.
Six-second native example smoke passes1/1; owned preview stopped, smoke script
restored and transient snapshot removed. No architecture exception.

Shadow Flames native algorithm milestone: 159/244. Ported the pinned inverted
five-tongue raster, stable outer-to-inner layers, looped noise, smoky back plane,
pale detached licks, flickering embers and forward-mapped black-heart light
removal after final glows. Local fixed 44x60 stack grid has no retained external
pointers or draw-time geometry allocation. Source time/RNG and owning palette
configuration use the existing analytic interface; no ECS/Link/provider/schema
or module boundary change, no architecture exception. All 5,044 raw source
color/light planes are exact across 12 cases: four seeds/palette profiles over
two loops, transformed Back/Front/Both in expanded and clipped canvases, single
and 64-stop palettes. Independent source raster observation agrees in all 577
grids / 1,523,280 cells. Static Shadow suite passes 44 cases / 37,319 assertions.
Coverage is 159/244, complete=false, without duplicate/extra/metadata mismatch.
85 algorithms remain, including 3 Shadow. Full Act/onBody/poses/hitstop/shake/
ghost/target, 12 moves, gallery parameter/background UI, OBJECT/platform CI and
performance/long-session acceptance remain required. Evidence stays local build/.

Shadow Flames standard Windows Debug build passes, including the complete
test index (4,456 files / 5,412 tests). Both static and DLL Shadow suites pass
44 cases / 37,319 assertions; source and runtime acceptance continue below.

Shadow Flames host verification: all 308 focused PixelFX tests pass; make check
passes against origin/dev. Source fixtures cover tongue/skin/heart geometry,
smoke and detached particles across loop boundaries, owning palette snapshots,
atomic rejection, ordered layers, reverse replay and draw purity. A separate
black-heart contract checks incoming destination light at independently observed
source cells in both normal and transformed clipped canvases. No gate weakened.

Shadow Flames native acceptance: the live gallery visited all 159 native IDs;
search/no-match/layout restoration, catalogue wrap, animation/pause/stop/restart
pass. Engine-owned screenshots were inspected during playback and at the quarter
loop, showing pale skin, dark heart, smoke, embers and distinct palette variants.
All six timeline assertions pass: both loops restore initial, both next frames
match the first frame, reverse restores tick 287, and restart restores initial.
No architecture exception. Next approved algorithm: shadow/shadow-tendrils.
Six-second native example smoke passes 1/1. The owned preview is stopped, the
smoke script bytes restored, and the transient snapshot removed. Evidence remains
ignored under build/pixelfx-validation/shadow-flames-*; no evidence is staged.

Shadow Tendrils native algorithm milestone: 160/244. Ported seven owning
tentacles with hooked, writhing spines, optional timed whips, back/front dimming,
rim pulses, tip lights, pool collars, splashes, ink landings, ripples, soot and
particles. Curves use bounded 125-point stack storage; source time and RNG are
injected. All 2,404 observed clock/RNG/emission-count snapshots and 16,828 tentacle
parameter/flag snapshots are exact.
Independent 104,052 curve points agree within measured 1.7763568394002505e-14;
1,051 non-finite source endpoint widths are matched explicitly. No NaN is cast
to a native raster cell. Static and DLL Shadow suites each pass 49 cases /
39,810 assertions. Unmodified pinned source passes 5,094 exact raw color/light
planes across 9 seed/palette/transform/layer cases; see the source defect below.

Source defect PIXELFX-SOURCE-001 (owner: PixelFX port maintainer): at default
seed 3, fixed tick 59, the first visible point of tentacle 4 has NaN width and
submits line(NaN,2.5,NaN,2.5). The pinned JS Painter line loop never terminates.
The original worker ceased producing frames after 58 and was explicitly stopped
by exact process identity. Source files remain pinned and clean. Normal frames
were rechecked with the completely uninstrumented reference; none of their
assertions is weakened. Tick 59 has separate diagnostic evidence: the pristine
kernel's invalid line is recorded and omitted by a caller-side command recorder,
then every valid command uses the pristine upstream Painter. Its two planes
match native output, but are NOT labelled pristine-source frame parity. A native
regression requires deterministic completion and those diagnostic planes. Native
behavior for non-finite geometry is no raster write, retaining the valid tip,
pulse, simulation and subsequent frames. Revisit/removal condition: a separately
verified upstream finite-width fix and a deliberately reviewed source-pin update;
retain the original-pin reproduction. No dependency source patch/override, no
reference gate skip and no architecture exception is introduced.

Applicable rules: sole owning simulation/trajectory/configuration state, bounded
geometry, synchronous caller-thread floor/painter borrows, structured checked
Results and atomic publication; existing module/export boundaries remain. No
ECS/Link/provider/schema change. Coverage 160/244, complete=false, without extra,
duplicate or metadata mismatch. 84 algorithms remain, including 2 Shadow.
Full Act/onBody/poses/hitstop/shake/ghost/target, 12 moves, gallery parameter/
background UI, OBJECT/platform CI and performance/long-session acceptance remain
required. Evidence stays ignored under build/.

Shadow Tendrils standard Windows Debug build passes including the full index:
4,457 files / 5,417 tests. Header consumers were explicitly rebuilt. Native and
source-quality acceptance continue below; no existing gate is weakened.

Shadow Tendrils host acceptance: all 313 focused PixelFX tests pass and make check
passes against origin/dev. No source gate or assertion is weakened. The defined
source-frame/undefined-command distinction remains explicit in the parity report.

Shadow Tendrils native workflow: all 160 catalogue IDs visited; search/no-match/
layout restoration, catalogue wrap, animation, pause, stop and restart pass.
Engine-owned playback, writhe and tick-59 screenshots were inspected. The engine
passes continuously through ticks 58/59/60. All six timeline checks pass: stable
4s/5s tail, restoration before last sink (304), restoration before source boundary
(58), exact boundary (59), reverse to 58, and initial restart. Source-defined
frames and the safe undefined-collar contract remain separately labelled.
Next approved algorithm: shadow/soul-drain. No architecture exception.
Six-second native example smoke passes 1/1; the owned preview is stopped, the
smoke script bytes restored and the transient snapshot removed. All raw evidence
remains local under build/pixelfx-validation/shadow-tendrils-* .

Soul Drain native algorithm milestone: 161/244. Ported eleven cubic funnel
streamlines, dashes and racing streaks, four hollow-faced souls with layered
wavering tails, extraction rings, crystal facets/heart, orbiting motes, landing
rings and halo. Independent soul material is an owning 1..64-color ramp fitted
to four stops. Immutable curves/births are prepared once per configuration; draws
retain no caller pointers or callbacks and allocate no geometry. All 6,644 raw
color/light source planes are exact across 13 cases: four seed/palette profiles
over two loops, transformed Back/Front/Both on expanded/clipped canvases, single
palette+soul, 64-stop palette, and 64-stop soul. Independent fixed source
geometry: 11 lines, 4 birth/line selections and 1,111 curve points are exact;
769 phase samples / 1,394 active heads have maximum measured difference
1.1102230246251565e-16. First-loop landings occur at ticks 20/107/189/316 at
120 Hz, and repeat after 384 ticks. Raw loop boundary/next-frame checks are exact.
Static Shadow suite passes 53 cases / 42,732 assertions.

Applicable rules: owning immutable geometry/material/configuration, injected
time and source RNG, checked structured Results and atomic publication, caller-
thread synchronous painter borrows and existing module/export boundaries.
No ECS/Link/provider/schema change or architecture exception. Coverage161/244,
complete=false, no duplicate/extra/metadata mismatch. 83 algorithms remain,
including 1 Shadow. Full Act/onBody/poses/hitstop/shake/ghost/target, 12 moves,
gallery parameter/background UI, OBJECT/platform CI and performance/long-session
acceptance remain required. Source defect PIXELFX-SOURCE-001 stays separately
recorded for Shadow Tendrils; Soul Drain references are unmodified-source frames.
All raw evidence remains ignored under build/.

Soul Drain standard Windows Debug build passes, including 4,458 indexed files /
5,421 tests. Both static and DLL Shadow suites pass 53 cases / 42,732 assertions.
Header consumers were explicitly rebuilt; native/source gates continue below.

Soul Drain host acceptance: all 317 focused PixelFX tests pass. make check passes
against origin/dev. No source gate or assertion is weakened. Independent geometry
comparison verifies all declared row counts as well as values and phase heads.

Soul Drain native acceptance: all 161 catalogue IDs visited; search/no-match/
layout restoration, catalogue wrap, animation/pause/stop/restart pass. Engine-
owned playback and first landing (ticks 19/20) screenshots were inspected;
crystal flare, moving hollow-faced souls, trails and distinct materials are
visible. All six timeline assertions pass: first/second loops restore initial,
both next frames reproduce the first frame, reverse restores tick 383, and
restart restores initial. Next approved algorithm: shadow/void-orb.
No architecture exception; the complete 244-effect objective remains active.
Six-second native example smoke passes 1/1. The owned preview is stopped, the
smoke script bytes restored, and transient snapshot removed. Raw evidence remains
ignored under build/pixelfx-validation/soul-drain-*; no raw evidence is staged.

Void Orb native algorithm milestone: 162/244, completing all 12 Shadow algorithms.
Ported the three-arm translucent vortex, bright filaments, paired gravitational
images of seven moving sources, Einstein ring, creeping orb rim, lifted floor
dust, accelerating motes/streaks, two gulp pulses and drawn-in rings, final
halos and core light removal. Owns a fixed 96x96 four-channel Float32 lookup
table prepared once per configuration, promoted to double per lookup as JS does.
All 36,864 table values / 147,456 bytes are bit exact to pinned source. All 769
gulp phase samples agree within measured 5.551115123125783e-17. All 6,576 raw
color/light planes are exact across 12 seed/palette/two-loop/transform/layer/
expanded-or-clipped-canvas cases. Raw loop-boundary/next-frame checks are exact.
Static Shadow suite passes 58 cases / 46,676 assertions; source fixtures cover
pulses, lensing, vortex, ring expiration and loops. Additional contracts require
opaque core extinction of incoming destination light, retained veil alpha,
owning palette/atomic rejection, ordered layers, replay/purity and analytic time.

Applicable rules: immutable owning prepared geometry/configuration, injected
time/source RNG, checked structured Results and atomic publication, synchronous
caller-thread painter borrows and existing module/export boundaries. No new
ECS/Link/provider/schema or architecture exception. Coverage 162/244,
complete=false, no extra/duplicate/metadata mismatch. 82 algorithms remain.
Shadow source defect PIXELFX-SOURCE-001 remains separately documented; Void Orb
references are unmodified-source frames. Full Act/onBody/poses/hitstop/shake/
ghost/target, 12 moves, gallery parameter/background UI, OBJECT/platform CI and
performance/long-session acceptance remain required. Raw evidence stays build/.

Void Orb standard Windows Debug build passes, including the complete test index
(4,459 files / 5,426 tests). Static and DLL Shadow suites each pass 58 cases /
46,676 assertions. Header consumers were explicitly rebuilt. Native and source
acceptance continue below; no gate is weakened.

Void Orb host acceptance: all 322 focused PixelFX tests pass and make check
passes against origin/dev. No source gate or assertion is weakened. All twelve
Shadow algorithms now have native kernels; full presentation and final profile/
platform/performance acceptance remain separate open requirements.

Void Orb native workflow: all 162 catalogue IDs visited; search/no-match/layout
restoration, catalogue wrap, animation/pause/stop/restart pass. Engine-owned
playback and first-gulp screenshots were inspected: lensed arcs, translucent
vortex, opaque dark core, two palette variants and drawn-in pulse ring are
visible. All six timeline assertions pass: both loops restore initial, both next
frames reproduce the first frame, reverse restores tick 383, restart restores
initial. Shadow's 12 native algorithms are implemented; full source Act and final
acceptance remain open. Next phase: nature (first source: acid-splash).
No architecture exception; the full 244-effect objective remains active.
Six-second native example smoke passes 1/1. The owned preview is stopped, smoke
script bytes restored and transient snapshot removed. Raw evidence remains
ignored under build/pixelfx-validation/void-orb-*; no raw evidence is staged.

Acid Splash native algorithm milestone: 163/244. Ported falling gob, two-layer
13-spike crown, shedding beads, size-drag liquid trajectories, etched ragged pit,
sinking acid level, boiling pops, pale steam, cooling scorch/stains and source
lights/particles. New module-owned NatureSimulation.h keeps Nature Drops' delayed
activation, reverse-order floor-crossing callback and direction-aligned rendering
separate from Shadow's different ink behavior. Internal preconditions use engine
asserts; borrowed landing observations return void, retain nothing and cannot
mutate/reenter the trajectory collection. Source RNG draws in every sizzle-loop
condition are preserved. No new user settings layout is needed: source options
are palette/seed. A distinct Nature test family owns these cohesive contracts.

Independent observed state: 2,404 clock/RNG/derived/flag/count snapshots are exact;
956,618 scalar checks include 14,881 drop, 26,082 stain, 61,097 wisp and 3,172 pop
records, with maximum numeric difference 2.842170943040401e-14. Steam rendering
has a measured final-bit alpha difference: at tick72, 213 source/native blend
commands have identical cells/colors; one alpha differs by1.3877787807814457e-17.
For row128/wisp9/step8, identical prior pixel1021374700 and color4292931820 yield
source1340141548/native1340141804 (one green LSB). The isolated actual source/native
wisp functions and painters reproduce the exact first full-frame pixel difference
at cell(-10,-5), proving the quantization cause without pixel correction.

Per approved Acceptance narrow floating raster contract, all 5,232 planes across
12 seed/palette/full-timeline/transformed/layer/expanded-or-clipped cases pass:
5,022 are byte exact; 241 pixel differences have maximum RGB error1, while all
alpha, coverage and light are exact. Reports retain exact=false and each raw
difference; the comparer remains strict by default and only explicit per-run
--color-tolerance 1 is used. No source edit, math bias, corrected pixel, omitted
frame or architecture waiver. Static/DLL Nature suites each pass6 cases / 4,173
assertions, including independent source alpha/light/spatial-color oracles,
owning palette/atomic failure, layer order, partitioned replay, stop/restart/end,
and Drops delay/zero-speed/below-floor/callback-order/draw fixtures.

Applicable rules: owning trajectory/configuration facts, injected time/source
RNG, structured checked Results, atomic publication, synchronous caller-thread
borrows and existing module/export boundaries. No ECS/Link/provider/schema or
architecture exception. Coverage163/244,complete=false, no extra/duplicate/
metadata mismatch. 81 algorithms remain, including 12 Nature. Full Act/onBody/poses/
hitstop/shake/ghost/target,12moves, gallery parameter/background UI, OBJECT/
platform CI and performance/long-session acceptance remain required. Evidence
remains ignored under build/.

Acid Splash standard Windows Debug build passes including the full index:
4,462 files / 5,432 tests. Final formatted sources were rebuilt. Static and DLL
Nature suites each pass6 cases / 4,173 assertions. Header consumers were rebuilt.
The new family suite is cohesive and remains below the source-owned split limit.

Acid Splash host/native acceptance: all328focused PixelFX tests pass and make
check passes against origin/dev. The live gallery visited all163native IDs;
search/no-match/layout restore, catalogue wrap, animation/pause/stop/restart pass.
Engine-owned playback, impact36, shed47 and boiling120 screenshots were inspected
for gob/crown, beads, steam and etched liquid pit with both palette variants.
All six timeline assertions pass: stable4s/5s tail, restoration before gone263,
before impact35, impact36, reverse35 and initial restart. RGB one-LSB precision
contract remains explicit; alpha/coverage/light stay exact. Next: nature/boulder.
No architecture exception; complete244-effect scope remains active.
Six-second native example smoke passes1/1. Owned preview stopped, smoke script
bytes restored, transient snapshot removed. All raw evidence remains ignored
under build/pixelfx-validation/acid-splash-*; no raw evidence is staged.

Boulder prerequisite: native NatureGeometry.h now owns bounded 5..8-corner stone
shapes and source faceted-rock rasterization. Explicit-count and random-count
preparations preserve distinct RNG consumption. Per-draw 64x64 mask/facet scratch
is local rather than a shared mutable cache; source's 62-cell raster cap, tiny
stone behavior, white flash, opacity, rotation/mirror and clipping are retained.
Independent pinned source comparison: eight seeded shapes / 58 corners, exact
RNG states, maximum coordinate difference 1.1102230246251565e-16, all384raw
color rasters / 5,111,808 bytes bit exact. Both static and DLL Nature suites pass
8cases / 5,073assertions, including384source hash fixtures and no mutation of
incoming light. This is shared geometry groundwork; Boulder kernel/configuration
and its native presentation remain in progress. Catalogue stays163/244 with81
algorithms outstanding; no unsupported effect is registered as an alias.

Rules applied: owning bounded geometry/scratch, explicit injected source RNG,
synchronous caller-thread painter/ramp borrows, no retained callbacks, checked
internal invariants and existing module boundaries. No ECS/Link/provider/schema
change or architecture exception. All raw evidence stays ignored build/.

Stone foundation host acceptance: standard Windows Debug build passes including
index4,463files/5,434tests. All8Nature host cases pass; make check passes against
origin/dev. No runtime catalogue or gallery entry was added for unfinished
Boulder. Next work is its owning size/spark configuration and ballistic/debris
composition using this verified helper. Full244-effect objective stays active.
No architecture exception; raw probes/reports remain local ignored build/.


### Boulder native algorithm (164/244)

`nature/boulder` owns ballistic flight, spinning eight-corner stone with moss and
pits, trailing dust/grit, impact white flash and force lines, split floor ring,
dent/cracks, 23 individually shaped rotating/bouncing chunks, spark/gravel/cloud
bursts and one-time landing dust. Source timing, RNG order and floor collision
are retained. Owning `PixelFxNatureSettings` exposes `size` [.5,32] (default11)
and `sparks` (1..64 opaque colors, fitted to4); seed/palette/playback/light remain
common parameters. The size bound keeps the source impact root real and the
bounded stone scratch meaningful. Prepared configuration copies buffers and
publishes atomically; no new module boundary, persistent schema or ECS lifecycle.
Caller-thread instances retain time/RNG ownership; no architecture exception.

Six profiles (default11, seed123/recolored, seed0, seed4294967295, size.5,
size32/two-stop sparks), each481 samples over0..4s, match5772/5772 pristine
pinned source color/light planes exactly. Transformed enlarged and cropped
back/front/both canvases add288/288 exact planes. No tolerance used. Static/DLL
Nature suites each pass13cases/7642assertions, including independent source
phase hashes, option ownership/bounds/roundtrip, layer composition, draw purity,
partitioned replay and stop/restart/tail. Evidence remains ignored under build/.

The catalogue contains164/244 algorithms,80 remaining (11 Nature). Full
Act/onBody/poses/hitstop/shake/ghost/target composition, complete gallery
parameter/background editing, OBJECT/platform CI and performance/long-session
acceptance remain open; this milestone does not close the full objective.


Boulder host acceptance: standard Windows build exits0 and indexes4464files/
5439tests; focused PixelFX host CTest passes335/335. `make check` exits0
against origin/dev. The actual native gallery visits164/164 IDs; search,
no-match hiding, exact layout/cycle restoration, animation, pause, settled stop
and restart all pass. Engine-owned screenshots inspect flight, white impact,
chunks/landing dust and tail. Six timeline checks pass exact end/tail,
pre-impact/impact reverse restoration and restart. Next: nature/earth-spike;
no full-objective completion claim.


### Earth Spike native algorithm (165/244)

`nature/earth-spike` owns floor fissures/heaving mound, three individually notched,
stratified and cracked spikes, four-corner floor slabs, sequential eruption and
white tip flare, rain/gravel/clouds, heated seams, split halves, top-down rubble
bands and sinking stumps. `NatureSpikeGeometry.h` retains the source Float32 row
scratch quantization, half/clip/floor tests and crack draw order. Scratch is local;
all source look/crack geometry is owned by the prepared instance. `drawRock`
accepts source polygons3..8 corners (Earth Spike's floor slabs have4); random
stones still sample5..7 and the initial Boulder explicitly uses8. No per-frame name lookup or new module edge.

Owning nature `height` [1,256] (default62) and opaque `glow`1..64 stops, fitted5,
use the existing checked prepare/commit codec. The height bound keeps the largest
overshooting spike inside the source400-row scratch limit. No ECS/persistent
format/provider/lifecycle change; caller-thread time/RNG ownership and borrowed
paint lifetimes remain explicit. No architecture exception.

Six profiles, each601samples0..5s (default, seed123/recolored, seed0,
seed4294967295, height1, height256/two-stop glow), match7212/7212 pristine pinned
source color/light planes exactly. No RGB tolerance. Static/DLL Nature suites
each pass18cases/10192assertions, including independent phase hashes and
configuration ownership/bounds/roundtrip, layer composition/draw purity,
partitioned replay, stop/restart and stable tail. Evidence stays ignored in build/.

The compiled algorithm count is165/244,79 remaining (10 Nature). The full
Act/onBody/poses/hitstop/shake/ghost/target workflow, interactive parameter and
background editing, OBJECT/platform CI, performance and long-session acceptance
remain required and open.


Earth Spike host acceptance: standard Windows build exits0, indexing4466files/
5444tests. Focused PixelFX CTest passes340/340; `make check` exits0 against
origin/dev. Enlarged/transformed, clipped64x64 and height256/glow variant600x600
canvases across back/front/both add648/648 exact raw source planes; combined
source comparison7860/7860 exact. The actual native gallery visits165/165 IDs;
search/no-matches/layout/cycle restoration and animation/pause/stop/restart all
pass. Engine screenshots inspect layered spikes, heated cracks, splitting and
rubble. Six exact timeline checks pass end/tail, pre-split/pre-eruption restore,
eruption restore, reverse and restart. Next: nature/healing-bloom. Full goal stays
open; source/visual milestones do not substitute for remaining composition,
UI, OBJECT/platform and performance/long-session acceptance.


### Healing Bloom native algorithm (166/244)

`nature/healing-bloom` owns the S-shaped growing/swaying stem, unfurling leaves,
swelling bud, far/front foreshortened petals, heart flare, healing light column,
back floor ring, beveled drifting crosses, motes, sequential petal detachment,
turning underside, twinkle-out and wilt. `NatureLeafGeometry.h` preserves the
source pointed leaf/rim/midrib/underside and stem. Prepared instances own both
`petal` and `heart` ramps (1..64 opaque colors, normalized4); no renderer or
external material references survive preparation. Time/RNG remain instance-owned
and injected. No new module edge, ECS lifecycle or persistent schema.

Source-defined nonfinite light conversion required a canonical native Painter
repair. The source floor formula intentionally remains unchanged: floor(-fw)
can give sqrt(1-u)=NaN. Source Math.min then bitwise-converts NaN channels to0;
std::min had instead saturated them to255. The independent before comparison
has1187/1202 exact planes, with15 light planes differing at208pixels each, first
tick27(.225s), maximum error255. All color planes already matched. The common
adder now explicitly maps NaN channel results to0, including0*Infinity; finite
saturation is unchanged. Independent pristine Painter fixtures cover NaN,+/-
Infinity and finite strength through both add and glow. No source patch, caller
pixel correction or math bias; no architecture exception.

Six601-sample profiles0..5s (default, seed123/recolored/petal2/heart3, seed0,
seed4294967295, single-stop materials,64-stop materials) now match7212/7212
source color/light planes exactly, without tolerance. Static/DLL Nature suites
each pass24cases/12770assertions, including independent phase and nonfinite
fixtures, ownership/bounds/roundtrip, layers/purity, partition replay and stop/
restart/tail. Ignored build/ retains exact before/after evidence.

Compiled algorithms166/244;78 remain, including9Nature. Full Act/onBody/poses/
hitstop/shake/ghost/target composition, interactive parameter/background editing,
OBJECT/platform CI and performance/long-session acceptance remain open.




Healing Bloom host acceptance: standard Windows build exits0, indexes4468files/
5450tests, and focused PixelFX CTest passes346/346. `make check` passes against
origin/dev. Enlarged/transformed and clipped64x64 back/front/both canvases add
552/552 exact source planes, including the nonfinite floor-glow boundary ticks;
combined raw source comparison7764/7764 exact, no tolerance. Actual native gallery
visits166/166 IDs and passes search, no-match hiding, exact layout/cycle restore,
animation, pause, settled stop and restart checks. Full acceptance remains open.


Engine-owned Healing Bloom screenshots inspect bloom, default/custom materials,
lifted turning petals, drifting crosses, wilt and empty tail. Six exact timeline
checks pass end/tail, pre-lift/pre-flare restoration, flare restoration, reverse
and restart. No architecture exception; ownership, checked configuration, time/
RNG determinism and synchronous renderer borrows were verified. Next:
nature/leaf-storm, using the native leaf geometry and stock/custom actors.


### Leaf Storm native algorithm (167/244)

`nature/leaf-storm` owns ascending/flung-out leaves, edge-on/underside tumble,
three green/autumn/red ramps, face-on glints, dim rear/bright front wind bands,
soft fading trails, floor dust and litter rings. Variable-life cycles retain
source birth/life and conditional RNG order, derived seed wrap, absolute-time
looping and draw order. Owning `leaves`1..64 opaque stops normalize12; palette
normalizes6. Actor option defaults to source Dummy and supports Hero/custom/
explicit None through the existing owning checked codec. No new module edge,
ECS lifecycle or persistent schema; caller-thread rendering borrows only.

Six577-sample profiles0..4.8s (default, seed123/recolored/leaves3, seed0,
seed4294967295, single-stop and64-stop materials) match6924/6924 pristine source
color/light planes exactly. Enlarged/transformed and clipped canvases add432/
432 exact planes. Actual source drawBack/drawActor/draw with both stock bodies
in14compositions matches28/28 native API-composed raw planes,1,304,576bytes; total7384/
7384 exact planes, no tolerance. Default Hero/Dummy body hashes also match the
source public drawActor. Custom body copying, malformed-mask rejection and
explicit absence are verified separately. These are source static actor composites;
full Act-driven posed-body integration remains a required open objective.

The actual source five-key Leaf Storm pose track is independently checked at
289samples: pose/shortfall errors0, joint error8.881784197001252e-16, no metadata
or presence differences. Native immutable track preparation retains no inputs;
permanent boundary fixtures cover12FPS held fields. This solver evidence does
not claim the source track is already wired into per-effect presentation.

Static/DLL Nature suites each pass31cases/15907assertions, including source
planes, material/actor ownership and bounds, static actor occlusion, track inputs,
layer purity, loop boundaries and analytic clock transactions. No architecture
exception. Evidence remains ignored in build/. Algorithms167/244,77remaining
(8Nature). Full Act/onBody/poses/hitstop/shake/ghost/target workflow, interactive
parameter/background editor, OBJECT/platform CI and performance/long-session
acceptance remain open.


Leaf Storm host acceptance: standard Windows build exits0, indexes4469files/
5457tests. Focused PixelFX CTest passes353/353; `make check` exits0 against
origin/dev. Native gallery visits167/167 IDs; search/no-matches/layout/cycle
restoration, animation, pause, settled stop and restart all pass. These gates
validate the algorithm and static actor preview, with full Act presentation still
open as stated above.


Leaf Storm engine-owned screenshots inspect default/custom actor occlusion and
leaf/wind variation. Six exact native timeline checks pass first/second loop
restoration, both subsequent frames, reverse to pre-wrap and restart. No live
posed-body claim is made. Next: nature/poison-cloud; full acceptance remains open.


### Poison Cloud native algorithm (168/244)

`nature/poison-cloud` owns three ragged gas sheets, tiled flowing noise, leading
floor tongues and thick inward-winding curls, rising/twisting ribbons, drifting
specks and wandering/floor/internal light. Tongues use bounded local arrays;
prepared instances own palette and spread, with injected absolute analytic time
and named hashed seeds. No new module edge, ECS lifecycle or persistent schema.
`spread`[0,4] (default1) retains the original zero-spread degeneracy; the bound
caps each sheet at961columns. Source loops/order and all coordinate formulas
are preserved. Private templated paint helpers also drive the diagnostic sink;
production uses the canonical Painter with no trace callback or retained borrow.

Six961-sample profiles0..8s (default, seed123/recolored/spread.65, seed0,
seed4294967295, spread0, spread4) pass11532/11532 raw color/light planes under
the approved RGB1-only contract,10534strict. There are3288differing pixels,
maximumRGB1, and zero alpha/coverage/light differences. Enlarged/transformed,
clipped64x64 and complete spread4 on1024x800 canvases add374accepted/319strict
planes,169differingRGB1pixels; alpha/coverage/light stay exact. Combined11906
accepted/10853strict,3457differingpixels, maxRGB1. Strict comparison is retained
alongside accepted reports; source algorithms and Painter are unmodified.

Independent first-difference proof at tick0,cell(41,-21): the actual source
Painter records9679blends; all three blends at this cell have the same call
indices, coordinates, colors and initial contents as the canonical native
algorithm. At call3203, source alpha.18629049562532862 versus native
.18629049562532865 (difference2.7755575615628914e-17) yields red62 versus
61.999999999999993before integer truncation. The input is sin(PI*.25): V8
.7071067811865475, MSVC.70710678118654757. This reproduces the exact full-frame
pixel difference; no math bias or output correction was introduced. Color
oracles useRGB<=1; full alpha and light hashes remain strict.

Static/DLL Nature suites each pass36cases/24120assertions: source default/zero
spread fixtures, spatial RGB oracles, owning materials, atomic finite bounds/
roundtrip, loop boundaries, back/front composition, reverse/purity, analytic
clock and stop/seek restart. No architecture exception. Evidence remains in
ignored build/. Algorithms168/244,76remaining (7Nature). Full Act/onBody/poses/
hitstop/shake/ghost/target presentation, interactive parameter/background editor,
OBJECT/platform CI and performance/long-session acceptance remain open.


Poison Cloud host acceptance: standard Windows build exits0, indexes4470files/
5462tests. Focused PixelFX CTest passes358/358; `make check` exits0 against
origin/dev. The native gallery visits168/168 IDs and passes search, no-matches,
layout/cycle restoration, animation, pause, settled stop and restart. RGB1-only
source evidence is explicit above; no claim of byte-exact RGB parity is made.


Poison Cloud temporal precision audit: source time1/120 versus481/120 has53
RGB-channel changes for default and52for seed123/spread.65/material variant,
all magnitude1, alpha/light exact. The961/120 comparison has the same bounded
source behavior. All four complete signed source/native period-difference maps
match exactly. IEEE remainder gives slightly different fractional phase inputs;
therefore an initial exact-next-frame assertion was false for the source itself.
Its diagnostic log is retained. No production time rounding was introduced.

Engine screenshots confirm layered gas, curls and default/variant spread. Whole
period boundaries, reverse before wrap and restart restore exact screenshots;
next-frame screenshots match the measured source precision, maximumRGB1 and
alpha exact. Four exact checks plus two bounded next-frame checks and the
independent signed-delta proof pass. This is explicitly distinct from claiming
exact next-frame periodicity. Next: nature/quake. Full goal remains active.


### Quake native algorithm (169/244)

`nature/quake` owns the three jolts, rift geometry/opening, glowing depth/lips,
local floor shake, heaving/rotating/bouncing slabs, independently shaped chunks,
grit/smoke/sparks, breathing dust/embers and landing dust, followed by closure
and sinking/empty tail. Rift and slab preparation preserve RNG order; fractional
emission loop bounds keep the source ceil-like iteration behavior. Slab motion
has its own source bounce/rest rules; shared Bits retain their distinct rules.
All geometry/time/RNG belong to the instance; renderer/ramp borrows are synchronous.

Owning `reach`[0,512] (default118) and `glow`1..64 opaque stops (normalized5)
use checked preparation/publication. Reach0 is valid source behavior: one rift
point, no slabs and a central particle/light burst. The positive geometry loops
are bounded by410rift points and85slabs at reach512. No new module boundary,
ECS lifecycle or persistent schema; no architecture exception.

Six601-sample profiles0..5s (default, seed123/recolored/reach154/glow2, seed0,
seed4294967295, reach0, reach512) match7212/7212 pristine source color/light
planes exactly. Transformed/enlarged and clipped back/front/both add336exact;
reach512on1536x384adds16exact. Combined7564/7564exact, zero color/alpha/coverage/
light differences, no tolerance used. Static/DLL Nature suites each pass41cases/
26706assertions, covering source jolt/closing/zero-range fixtures, option
ownership/finite bounds/roundtrip, layers/reverse/purity, partitioned replay,
stop/restart and stable tail. Evidence stays ignored in build/.

Algorithms169/244,75remaining (6Nature). Full Act/onBody/poses/hitstop/shake/
ghost/target integration, interactive parameter/background editing, OBJECT/
platform CI and performance/long-session acceptance remain open.


Quake host acceptance: standard Windows build exits0, indexes4471files/
5467tests. Focused PixelFX CTest passes363/363; `make check` exits0 against
origin/dev. Native gallery visits169/169 IDs; search/no-matches/layout/cycle
restoration, animation, pause, settled stop and restart all pass. Full goal
acceptance remains open as above.


Engine-owned Quake screenshots inspect all three jolts, heaving slabs, glowing
rift, closure and empty tail. Six exact timeline checks pass end/tail,
pre-closure/pre-first-jolt restoration, first-jolt restoration, reverse and
restart. No architecture exception; parameter ownership, checked atomic
publication, fixed-step source time/RNG and synchronous rendering borrows were
verified. Next: nature/root-snare. Full acceptance remains open.


### Root Snare native algorithm (170/244)

`nature/root-snare` owns five growing/cinching spiral roots, lit bark/grain and
rootlet hairs, front/back path strokes, floor cracks and mossy glow, leaf buds,
cinch sparkle/flash, hold motes, pre-break white flash, bark/splinter release,
custom bouncing chips and dust/smoke/light. Paths own49bounded points; inputs,
cracks, material buffers, time/RNG and chip state belong to each instance.
Shape sampling precedes life sampling exactly as the source chip literal.
Expiring chips receive the source's isolated final physics step before native
erasure, retaining reference validity without observable state/RNG changes.

Owning `glow`1..64 opaque stops normalize4; actor defaults to Dummy and supports
Hero/custom/explicit None through the existing checked codec. No new module,
ECS lifecycle or persistent schema; no architecture exception. Six601-sample
profiles0..5s (default, seed123/recolored/glow2, seed0, seed4294967295, single-
stop and64-stop materials) match7212/7212 pristine source color/light planes.
Transformed/enlarged/clipped back/front/both add360exact. Source static actor
composites with both stock bodies add36exact planes,1,769,472bytes. Combined
7608/7608byte-exact, no color/alpha/coverage/light differences or tolerance.

The actual11-key pose track, including negative preroll and snap metadata,
passes328samples: pose/shortfall error0, joint error1.7763568394002505e-15,
no metadata/presence differences. Native cinch/break flags match observed
pristine source Lights events at ticks75/211; their authored times.62/1.75
match source act.hits. Authored stop/shake/amp values are retained as evidence;
this does not claim their per-effect Act presentation is integrated. Static
actor composition likewise does not claim dynamic posing.

Static/DLL Nature suites each pass49cases/29559assertions: source growth/cinch/
flash/break/tail fixtures, glow and actor ownership/bounds/roundtrip, static
actor occlusion, track boundaries, layers/reverse/purity, partition replay and
stop/restart/tail. Evidence remains ignored under build/. Algorithms170/244,
74remaining (5Nature). Full Act/onBody/poses/hitstop/shake/ghost/target workflow,
interactive parameter/background editing, OBJECT/platform CI and performance/
long-session acceptance remain required and open.


Root Snare host acceptance: standard Windows build exits0, indexes4472files/
5475tests. Focused PixelFX CTest passes371/371; `make check` exits0 against
origin/dev. Native gallery visits170/170 IDs and passes search/no-matches,
layout/cycle restoration, animation, pause, settled stop and restart. These
checks cover the effect and static body preview; full Act integration remains
open as stated above.


Engine-owned Root Snare screenshots inspect static default/custom body occlusion,
cinch/buds/hold, white pre-break flash and bark burst. Six exact native timeline
checks pass end/tail, pre-break/pre-cinch restoration, cinch restoration, reverse
and restart. Material/input ownership, checked atomic publication, source fixed
time/RNG and renderer borrows were verified; no architecture exception. Next:
nature/spore-burst. Full Act presentation and the overall acceptance remain open.

### Spore Burst native algorithm (171/244)

`nature/spore-burst` procedurally draws the mushroom stem, frill, glowing gills,
lit spotted cap and charge/puff squash/stretch. Source-ordered emissions produce
36 clock-sampled gas puffs, grit, dust, motes, flakes, stars and layered lights.
Its `cap` and `stem` options own opaque color ramps of1..64 stops, normalized to
six and five respectively. Standard metadata:212x132, origin106,118,
duration3.2, seed9, non-looping. Time and RNG remain injected by the existing
deterministic playback owner; drawing borrows the target synchronously and does
not advance the simulation.

Six601-sample profiles cover default/zero/max seeds and one/two/64-stop cap/stem
materials:7212/7212 reference color/light planes are byte-exact. Transformed
enlarged and64x64clipped canvases add336/336 exact back/front/both planes.
Static and DLL Nature tests each pass55cases/32128assertions, including source
phase fixtures, copied material ownership, rejection/roundtrip, draw purity,
reverse replay, stop/restart and tail. Host runtime acceptance is recorded in
the implementation plan; full244effect/Act/performance acceptance stays open.

Windows host build exits0;377/377focused PixelFX tests pass. The native gallery
visits171/171 IDs and passes search/layout/cycle/animation/pause/stop/restart.
Engine-owned screenshots inspect growth, charge, puff/cloud and sink. Six exact
timeline checks pass tail, pre-sink/pre-puff restoration, puff restoration,
reverse and restart. No architecture exception;73algorithms remain, next
`nature/thorns`. Full Act, interactive parameter/background editing,
OBJECT/platform CI and performance/long-session acceptance remain open.

### Thorns native algorithm (172/244)

`nature/thorns` owns a sequential wave of hooked horns with lit grain/barbs,
source white emergence frames, a racing floor seam, leaf collars, glints, dust,
lights and distinct bouncing faceted chips. Its integral `count` accepts1..64
and preserves the source effective minimum2. Owning `glow`/`soil` ramps accept
1..64opaque stops normalized to4/8. Default232x92, origin48,78, duration2.4,
seed8, count6. Large counts keep source emissions beyond nominal duration;
`done()` remains advisory and playback time is not clamped.

Six profiles produce7932/7932 byte-exact color/light planes, including count1,
9and64, zero/max seeds and one/two/64-stop ramps. Count64 is sampled through8s.
Transformed enlarged and64x64clipped back/front/both add336/336 exact planes.
Static and DLL Nature contracts each pass62cases/34726assertions, including
source continuation after nominal duration at count64. Windows build/link pass,
indexing4475files/5488tests;384/384focused PixelFX tests pass. Native gallery
visits172/172 IDs and passes search/layout/cycle/animation/pause/stop/restart.
Engine-owned screenshots inspect emergence, hooked shading, leaf collars,
debris and sink. Six exact native checks pass tail, pre-last-sink/pre-first-
emergence restoration, emergence restoration, reverse and restart. No
architecture exception;72algorithms remain, next `nature/tornado`. Full Act,
parameter/background editing, OBJECT/platform CI and performance/long-session
acceptance remain open.

### Tornado native algorithm (173/244)

`nature/tornado` analytically draws a snaking wind column,58front/back wind
bands,16cloud-crown bands,24cyclic leaf/twig/stone trajectories,40dust puffs,
16grit cycles,7floor scuffs and crown lightning. Height accepts[1,256]; owning
`dust` and `leaves` ramps accept1..64 opaque stops and normalize to5/8 colors.
Default196x168, origin98,152, duration2loop, seed4, height112. Drawing uses the
injected analytic time and deterministic named source RNG streams; targets and
settings are synchronously borrowed, with no retained external references.

Six481-sample profiles produce5772accepted planes,5716byte-exact;64pixels have
RGB1differences only. Alpha, coverage and light stay exact. Enlarged/transformed
and clipped canvases add276accepted/262exact planes, and full height256 on a
512x384canvas adds138/138exact. Total6186accepted/6116exact. The default comparator
remains strict; explicit RGB1 comparisons follow the measured precision contract.
Actual source/native traces locate a sin difference at t=.55, cell11,-49,
blend7105: alpha differs2.7755575615628914e-16, making green49 versus
48.99999999999999 before integer truncation. No production pixel correction.

Static and DLL Nature tests each pass66cases/38948assertions, including source
phase/flash fixtures, copied material/height validation, layers/reverse/draw
purity, partitioned analytic clocks, atomic configuration and stale seek rejection.
Source/native signed fractional-loop delta maps match for default and gallery
material variants. Native runtime acceptance is recorded in the plan; full
244effect/Act/parameter-editor/platform/performance acceptance remains open.

Windows build/link passes, indexing4476files/5492tests;388/388focused PixelFX
tests pass. The native gallery visits173/173 IDs with all workflow checks true.
Engine screenshots inspect funnel, layered crown, front/back leaves/stones/dust
and lightning. Four exact native restoration checks and two bounded fractional
next-frame checks pass, with source/native signed period deltas exact. Increasing
height retains the source's default-frame clipping; enlarged `drawToCanvas`
targets expose full geometry, verified at height256on512x384. No architecture
exception;71algorithms remain, next `nature/toxic-puddle`. Full acceptance remains
open as above.

### Toxic Puddle native algorithm (174/244)

`nature/toxic-puddle` analytically draws tar swells, sliding oil sheen,
subsurface veins, luminous lips,8cyclic glossy/stretching bubbles with pop
rings and spatter, and9steady plus bubble-emitted fraying vapour ribbons.
Width accepts[1,256]; owning `sheen` accepts1..64 opaque colors normalized to4.
Default176x100, origin88,86, duration4loop, seed2, width42. Time/RNG are injected;
no drawing mutation or external references are retained.

Six961-sample profiles yield11532accepted/11284exact planes,261RGB1pixels;
alpha/coverage/light strict. Enlarged/transformed, clipped and full-width256
on768x768 add360accepted/328exact, totaling11892accepted/11612exact. The generic
comparator remains strict by default; explicit RGB1 follows measured precision.
Actual source/native blend280 at t16/120,cell-5,-21 has the same coordinates,
colors and prior pixels, but alpha differs6.938893903907228e-18 from an actual
sin-result difference; green187.99999999999997 versus188 then truncates apart.
No production rounding/pixel correction.

Static and DLL Nature contracts each pass70cases/42969assertions, covering
source phase/grid samples, sheen/width ownership and boundaries, layers/reverse/
draw purity, partitioned analytic time, atomic config and stale seek rejection.
Source/native fractional-loop signed maps are exact only1/4; per-frame checks
pass12/12planes underRGB1 with strict alpha/coverage/light, and both temporal
changes stayRGB1. Strict failures are retained; no all-deltas-exact claim.
Host/native acceptance is recorded in the plan. Full244effect/Act/editor/
platform/performance acceptance remains open.

Windows build/link passes, indexing4477files/5496tests;392/392focused PixelFX
tests pass. Native gallery visits174/174 IDs with all workflow checks true.
Engine screenshots inspect oil skin, bubbles/pop/spatter and fraying vapour.
Four exact native restoration checks pass two loop starts, reverse and restart;
fractional next-frame checks pass the measured RGB1/alpha contract and source
frame/temporal bounds. Signed delta maps remain exact only1/4as stated above.
No architecture exception;70algorithms remain, next `nature/vine-whip`. Full Act,
parameter/background editing, OBJECT/platform CI and performance/long-session
acceptance remain open.

### Vine Whip native algorithm (175/244)

`nature/vine-whip` uses24damped-spring segments to grow, coil back, lash and
retract. Its lit/notched stem carries alternating hooked thorns and three
leaves; a12pose history produces translucent lash haze/streaks. The crack emits
18sap drops, floor spots, four torn leaves, air lines and glints; separate dirt
chips, particles and light preserve source RNG/update order. `thorn` and `soil`
accept1..64 opaque stops normalized to4/8. Default216x128, origin60,114,
duration1.4non-looping, seed3. Time/RNG are injected; draw does not mutate state.

Six481-sample profiles pass5772/5772 color/light planes byte-exact. Transformed
enlarged/clipped and1024x512full-sap canvases add486/486 exact back/front/both
planes, for6258/6258exact total with no tolerance. Static and DLL Nature suites
each pass75cases/45486assertions, covering source phase fixtures, copied material
ownership/bounds/roundtrip, layers/reverse/draw purity, partitioned replay and
stop/restart/tail. Expired chip/leaf locals are stepped in isolation before
safe erasure, preserving source behavior without retaining invalid references.
Windows build/link passes, indexing4478files/5501tests;397/397focused PixelFX
tests pass. Native gallery visits175/175 IDs with all workflow checks true.
Engine screenshots inspect coiling/lash/smear, thorns/leaves, crack/sap and
retraction. Six exact timeline checks pass tail, pre-sink/pre-crack restoration,
crack restoration, reverse and restart. All13Nature algorithms are ported;
69algorithms remain, next `spawn/coin-drop`. Material ownership, checked atomic
configuration, injected time/RNG and synchronous target borrows were verified;
no architecture exception. Full Act, parameter/background editing,
OBJECT/platform CI and performance/long-session acceptance remain open.

### Coin Drop native algorithm (176/244)

`spawn/coin-drop` draws a three-layer poof and spinning gold coin geometry
(rim, face, raised mark and flat resting glyph), then simulates independent
births, gravity/drag, bouncing hits, rest/glints and staggered wink/star/ring
emission. `coins` accepts0..64; zero preserves smoke-only source behavior and
one coin uses the centered launch. Owning `smoke` accepts1..64 opaque stops
normalized to4. Default160x88, origin80,78, duration3non-looping, seed23, coins7.
Large counts keep emitting after nominal duration; `done()` is advisory.

Six profiles pass8172/8172 color/light planes byte-exact, including max64 through
9seconds. Enlarged/transformed, clipped and512x256full-coins back/front/both add
510/510exact, totaling8682/8682exact with no tolerance. Static and DLL Spawn
suites each pass7cases/2591assertions: source phase fixtures, material/count
ownership/roundtrip/bounds, zero/max-count behavior, layers/reverse/draw purity,
partitioned replay and stop/restart/tail. Actual source event observation confirms
first bounce tick71 and first/last default wink ticks261/316. Host/runtime
acceptance is recorded in the plan. Full244effect/Act/editor/platform/performance
acceptance remains open.

Coin Drop Windows build/link passes, indexing4480files/5508tests;404/404focused
PixelFX tests pass. Native gallery visits176/176 IDs with all workflow checks
true. Engine screenshots inspect poof/spin, bounce, flat coins/glints and
first/last wink. Six exact timeline checks pass tail, pre-last/pre-first-wink
restoration, first-wink restoration, reverse and restart. Cold option family,
material/count ownership, checked atomic configuration and injected time/RNG
were verified; no architecture exception.68algorithms remain, next
`spawn/disintegrate`. Full acceptance remains open as above.


### Disintegrate native algorithm (177/244)

`spawn/disintegrate` prepares the owning source body/rim, charges and flashes,
then peels body pixels along a ragged front into wind-driven motes, falling ash
and cooling fragments. Native geometry preserves original nonzero pixel colors,
height-based fallback colors for occupied zero-color cells, four-neighbor edges
and source row-major order. Empty custom masks remain valid; an absent actor is
rejected. The effect owns peel state separately from immutable body geometry.
`accent` owns1..64 opaque stops normalized to3; `wind` accepts[-4,4]. Default
176x100, origin48,86, duration3.4non-looping, seed5, Dummy actor.

Six601sample parameter profiles and four601sample Hero/custom/fallback/empty
actor profiles pass12020/12020 exact color/light planes. Enlarged/transformed,
clipped and1024x512full-strong-wind canvases add468/468 exact back/front/both
planes:12488/12488exact with no tolerance. Independent original bodyOf/rimOf
fixtures cover66157body pixels,1197rim entries and66721cell indices, including
fractional anchors, holes, raw alpha colors, empty masks and dense256x256 bounds.
Static and DLL Spawn suites each pass15cases/5588assertions. The occupancy query
uses the explicit name isOccupied; no API-shape exception is needed. All input
storage is copied during preparation, and no temporary actor references survive.

These checks validate raw kernels and actor visibility, not full source Act
onBody/posed-body carrying. Native workflow and final host/source acceptance are recorded below. Full Act, interactive
parameter/background editing, OBJECT/platform CI and performance/long-session
acceptance remain open.


Disintegrate accepted (177/244): final Windows build/link exits0 and indexes
4482files/5516tests. Focused Windows CTest passes412/412; make check passes
against origin/dev after naming the pure occupancy query isOccupied. Native
gallery visits177/177 IDs, with search/layout/cycle/animation/pause/stop/restart
checks all true. Engine-owned screenshots inspect charge, white flash, ragged
peel, wind-driven fragments, cooling ash and tail with stock/custom actors.
Six exact native timeline checks pass end/tail identity, pre-end/pre-flash
restoration, flash restoration, reverse and restart. The isolated static/DLL
Spawn suites each pass15cases/5588assertions after the final query rename.

The cold descriptor/codec is the only parameter authority; normalized accent,
wind and actor storage are owned by prepared configurations. Time and named
instance RNG stay injected; canvas spans are synchronous borrows. No source or
dependency implementation was modified, no architecture exception was used.
Algorithms177/244,67remain; next spawn/dissolve-burn. Raw body-effect agreement
does not establish source Act onBody/posed rig integration. Full Act,
interactive parameter/background editing, OBJECT/platform CI and performance/
long-session acceptance remain mandatory and open.


### Dissolve Burn native algorithm (178/244)

`spawn/dissolve-burn` retains source row-major burn keys, ascending ragged seam,
white/yellow/orange hot edge, char/live sparks, heat-browned original body colors,
six layered noise-driven flame tongues, falling/sifting ash, rising embers, fire
with smoke-on-death, steady smoke and warm floor/seam light. Owning `ash` accepts
1..64 opaque stops normalized to3. Default96x112, origin48,100, duration3.6non-loop,
seed16, Dummy actor. Actor is required; custom empty masks are valid. Original
actor visibility is always hidden because the kernel owns the burning silhouette.

Six601sample parameter profiles and four601sample Hero/custom/fallback/empty
actor profiles pass12020/12020 exact color/light planes. Transformed enlarged,
clipped and full512x384canvas back/front/both add468/468exact:12488/12488exact,
with no tolerance. Original Lights.add observation confirms ignition tick18.
Static/DLL Spawn suites each pass21cases/8349assertions: independent raw source
phase fixtures, owning ash/actor configuration and atomic roundtrip/bounds,
original visibility, layers/reverse/draw purity, partitioned replay and stop/tail.
Host/source/native acceptance is recorded below. Full Act onBody/posed carrying
is distinct from this raw body kernel; full Act, parameter/background editing,
OBJECT/platform CI and performance/long-session acceptance remain required.


Dissolve Burn accepted (178/244): final Windows build/link exits0 and indexes
4483files/5522tests. Focused Windows CTest passes418/418 after updating the
catalogue-start visibility expectation for this new always-hidden original
actor policy. Initial417/418 failure and exact assertion are retained locally;
no production visibility behavior was changed to fit the old expectation.
The repaired static/DLL catalogue case each passes1case/615assertions, alongside
21Spawn cases/8349assertions per linkage. make check passes against origin/dev.

Native gallery visits178/178 IDs; all search/layout/cycle/animation/pause/stop/
restart checks pass. Engine screenshots inspect ignition tick18, char/seam,
layered flame tongues, smoke/embers and ash. Six exact native timeline checks
pass end/tail, late-smoke/pre-ignition restoration, ignition restoration,
reverse and restart. Smoke passes1/1, alive6seconds (minimum2seconds). Owned
preview is closed and runtime logs are retained locally. Original source pin
and dependency implementations are unchanged.

Owned actor/material preparation, immutable geometry plus separately owned burn
state, checked atomic configuration, injected time/RNG, synchronous canvas
borrows and the cold descriptor authority were verified; no architecture
exception. Algorithms178/244,66remain; next spawn/egg-hatch. Full Act onBody/pose
integration, interactive parameter/background editing, OBJECT/platform CI and
performance/long-session acceptance remain open and required.


### Egg Hatch native algorithm (179/244)

`spawn/egg-hatch` prepares a rounded lit/speckled shell, seeded jagged cracks
and three nearest-crack light fields. Three damped rocks open the cracks, then
Voronoi-like seeded shell groups retain their lit exterior and hot broken edges
while tumbling/bouncing; the empty cup springs, sheds motes, and crumbles into
more pieces. Native rays, stars/sparks, chips, pale dust, contact shadow and
internal/floor light preserve original update/RNG/draw order. Each instance owns
its geometry and material state; no source-global mutable shell colors are shared.

`glow` owns1..64 opaque stops normalized to3. `speckle` preserves the source
color number in[-2147483648,4294967295], including zero/partial-alpha bit
patterns and signed/unsigned identity. Default120x96, origin60,84, duration3.1
non-looping, seed31. The source event observer confirms crack lights at ticks
42/109/175, pop at238, original shell still drawn at306 and absent at307.

Six601sample seed/material profiles plus five601sample color-number boundary
profiles pass13222/13222 exact color/light planes. Enlarged/transformed, clipped
and512x384full canvases add594/594exact back/front/both planes:13816/13816exact,
no tolerance. A positive white speckle exposed source strict comparison against
WHITE=-1 despite equal packed bits. The kernel retains source number identity
in owning geometry; no pixel correction or source modification is used. Both
positive4294967295 and negative-1 white, zero, partial alpha and palette-match
fixtures now pass. The failed pre-fix report is retained under build only.

Static/DLL Spawn suites each pass29cases/11570assertions, including source
phase/material/color-number fixtures, prepared configuration bounds/ownership/
roundtrip, cross-instance shell isolation, layers/reverse/draw purity, partitioned
replay and stop/restart/tail. Host/source/native acceptance is recorded below.
Full Act, interactive parameter/background editing, OBJECT/platform CI and
performance/long-session acceptance remain open and required; no architecture
exception is used for this kernel.


Egg Hatch accepted (179/244): final Windows build/link exits0 and indexes
4485files/5530tests. Focused Windows CTest passes426/426; make check passes
against origin/dev. A local incremental-link attempt reportedLNK1116/error487;
the named .ilk was already absent when inspected, and the standard build retry
and final acceptance build both exit0 without a source workaround.

Native gallery visits179/179 IDs, with every search/layout/cycle/animation/
pause/stop/restart check true. Engine-owned screenshots inspect lit speckled
shell, three rocks/cracks, pop/rays, hot tumbling pieces, cup crumble and tail.
Six exact native timeline checks pass end/tail identity, pre-crumble/pre-pop
restoration, pop restoration, reverse and restart. Smoke passes1/1, alive through
6seconds (minimum2seconds); owned preview is closed and tagged logs retained.

Owned geometry/color-number state, copied material configuration, source RNG
order, injected time, checked atomic preparation/commit and synchronous canvas
borrows were verified. No upward dependency, runtime string-key lookup, pixel
correction or architecture exception was introduced. Algorithms179/244,65remain;
next spawn/poof. Full Act, interactive parameter/background editing,
OBJECT/platform CI and performance/long-session acceptance remain required.
The remaining six Spawn algorithms can share one subsequent host/native
acceptance batch while retaining independent source comparisons per algorithm.


### Remaining six Spawn algorithms — batch validation in progress

The compiled source catalogue has185 implementations; accepted count remains
179/244 until this batch completes host/source/native acceptance. New kernels:
Poof112x92/origin56,80/duration1.6/seed21 (`stars`1..64 opaque stops fit3);
Respawn Beam128x150/64,136/2.5/12 (`width`0..256, default15; nonempty actor);
Shatter Death172x96/84,84/3.3/14 (required actor, empty masks valid);
Soul Rise96x140/48,126/3.2/27 (`height`0..256, default50; nonempty actor);
Spawn Portal208x150/104,122/3.8/6 (`width`0..256, default28);
Summon Rise176x116/88,94/4.6/9 (`body`/`horn`/`stone`1..64 opaque stops fit5).

Poof retains21 independently delayed/swelling/lifting/shrinking puffs, three
whole-cloud shade/body/cap passes, flare/ring/speed lines, stars and floor dust.
Respawn Beam retains guide motes, slam/strands, rising row scan and glitch/white/
cooling colors, floor rings, rim glints and original-body visibility boundary.
Shatter Death prepares grown body ice,22 seeded facets, rime spikes and recursive
cracks; its own colored pieces rotate/bounce/skid/melt and leave floor rime.
Soul Rise derives pale tones from owned body pixels, stretches/lifts/sways the
soul and melts its legs into a narrowing wisp with thinning edge motes.
Spawn Portal retains a ragged swirling hole/lip, inward motes, smoke belches,
fountain wisps with smoky trails, rubble, rings and closure pop.
Summon Rise procedurally builds eight overlapping beast parts and Float32 blur/
surface fields, rune glyphs/cracks, two heaves, warm underlight, rubble/dust/fire,
eye flares and pixelwise fade. Only immutable beast geometry is cached; material
and simulation state remain per instance.

All36 parameter profiles pass46152/46152 exact color/light planes (601samples
per profile, Summon Rise841). Thirteen Hero/custom/fallback/one-cell/empty actor
profiles add15626/15626exact. Twenty-one enlarged/transformed/clipped/full-width/
full-height/dense256x256actor canvas matrices add2880/2880 exact back/front/both
planes. Independent original stock-actor back/body/front composition adds144/144
exact planes. Total64802/64802exact, no tolerance. The source drawActor helper is
stock-only; custom evidence here concerns real kernel actor inputs and native
snapshot ownership, not a claim of upstream custom stand-in/Act rendering.

Respawn Beam and Soul Rise sample a nonempty rim. Original empty-mask runs throw
TypeError on c.x; native preparation rejects these atomically with occupied-mask
constraints. Shatter Death's empty-mask source output remains supported and is
pinned. A one-row Soul Rise can produce a NaN extra-tail x-loop that writes
nothing in JS; the native finite guard preserves all1202one-cell planes exactly.
Portal preserves source Math.max(NaN) fringe shader decisions, including width0.
Shatter preserves signed ramp/literal versus unsigned typed-color comparisons.
No source/dependency implementation or per-pixel correction was introduced.

Initial standalone static/DLL Spawn suites pass69cases/28804assertions after
fixing the new Summon tail test to respect its actual4.6second duration (and
asserting duration expiry remains false at4seconds). Three additional stock
composition cases are compiled; final suite/gates/native proof remain pending.
Full Act/posed body carrying and exact final presentation, parameter/background
editing, OBJECT/platform CI and performance/long-session acceptance remain open.
The prototype gallery still draws separate static-body/render passes; this batch
does not claim the complete Act compositor.

### Six remaining Spawn algorithms accepted (185/244)

Poof, Respawn Beam, Shatter Death, Soul Rise, Spawn Portal and Summon Rise
complete all ten Spawn kernels. Accepted algorithm count is185/244;59remain:
Status13, Melee12, UI12, Transitions10 and Moves12. The full244 goal remains
active, including Act/onBody/poses/hitstop/shake/ghost/target, custom posed body
composition, interactive parameter/background editing, OBJECT/platform CI and
performance/long-session acceptance.

The six kernels pass64802/64802 exact source color/light planes, including
parameter/seed bounds, Hero/custom/raw-alpha/fallback/one-cell/empty actor inputs,
transformed/clipped/maximum/dense canvases and144 exact static stock composition
planes. No tolerance or pixel correction is used. Empty-rim rejection for Respawn
Beam and Soul Rise is a structured cold validation result; empty Shatter Death
remains supported. The original source and dependencies remain unmodified.

Final static and DLL Spawn suites both pass72cases/29428assertions. The Windows
host build indexes4492files/5573tests; focused host CTest passes469/469, and
make check exits0 against origin/dev. The native gallery visits185/185 IDs;
search/no-match/page restoration/wrap and all six animation/pause/stop/restart
checks pass. Each new effect passes six exact image comparisons for late-phase,
phase-before/after, reverse, restart and settled tail restoration. Engine-owned
screenshots were inspected; prototype static-body passes remain distinct from
complete Act acceptance. Source event observation preserves original methods
and confirms beam slam39, shatter burst169, soul departure42, portal burst121/
closure267 and summon break127/eye flashes277 (fixed120Hz ticks).

Architecture requirements applied: owning per-instance snapshots/state,
immutable-only shared beast geometry, injected fixed-step time/seeded RNG,
transactional Result-based validation/configuration, descriptor-owned cold
options and typed hot paths, versioned configuration round trips, public
ownership/lifetime/cost documentation and per-module tests. No new module,
upward include, mutable process cache, dependency patch or exception was added.
Raw evidence remains ignored under build/ and is not staged for publication.

The required example smoke passes1/1, remains alive for6seconds (minimum2),
and exits0. The owned preview process is closed.

### First Status batch - validation in progress

Accepted algorithms remain185/244. The compiled catalogue adds Haste, Slowed
and Bleeding (188 implementations pending acceptance); ten Status kernels still
remain after this batch. Shared StatusShape owns mask-space body/up/side/outline/
top/row geometry in source order. It retains no actor pointers, global mutable
cache or per-frame string lookup. Haste owns prepared shading levels; Slowed
owns its mask geometry; Bleeding owns wound geometry. Pure kernels use supplied
absolute/wrapped time and source seeded cycle streams. Body carrying through
Act/onBody poses is still an outstanding integration requirement.

Haste100x56/origin64,48/duration1.2/seed4 retains three striped after-images,
speed streaks, heel dust, trailing smears and a leading rim. Dust is a raw packed
single color, including zero/partial/zero-alpha values; opaque1..64-stop primary
palettes are normalized to7. Slowed64x76/32,68/2.4/7 retains the heavy clock-hand
wind-up/snap/overshoot, dial markings/ripples/glow, outline lag, body film and
closed-form slow drops. Bleeding60x56/30,46/2.4/5 derives three wounds with runs,
bead growth, gravity-driven drops, crowns/drying splats, floor pool, heartbeat
body flash and spurts.

Actual unchanged source Bleeding throws a TypeError when height1 rounds its
sample row past the mask. Native validation now publishes and enforces the
source-owned minimumHeight2 before preparing geometry; all older descriptors
retain minimumHeight1. Required/empty-mask semantics are independent: these
three kernels accept empty masks but reject an absent actor. This is a cold,
transactional Result diagnostic at /options/actor/height, not a runtime clamp.

Parameter and actor profiles plus twelve transformed/clipped/extended/dense
canvas matrices pass44676/44676 planes,44641 exact. Haste and Bleeding are exact;
Slowed permits RGB1 only, with coverage/alpha/light/visibility strict. All32
nonexact parameter/actor planes (931 pixels) were traced through the actual
original draw calls and the actual native Slowed/Painter paths. Coordinates,
prior pixels and output alpha agree; only body-blend alpha differs by at most
2.7755575615628914e-17 from Math.exp versus native exp. Every differing output
pixel is reproduced by that measured blend; diagnostic full frames exactly
match the untouched source/native probe outputs. Three further maximum-actor
canvas color planes use the same measured body-film tolerance. The original
strict failed comparison and original-method wrappers are retained under build/.
No original source modification, dependency patch, math clamp or pixel
correction was introduced.

Tests and static stock composition, host build/source gates and native gallery
acceptance are in progress. All244 effects, full Act/posed custom composition,
interactive parameter/background authoring, OBJECT/platform CI and measured
performance/long-session validation remain the goal.

### First three Status algorithms accepted (188/244)

Haste, Slowed and Bleeding complete this batch. Accepted algorithm count is
188/244;56remain: ten Status, twelve Melee, twelve UI, ten Transitions and twelve
Moves. This is algorithm acceptance, not completion of the full Act/editor/
platform/performance goal. All ten Spawn and all thirteen Nature kernels remain
accepted; no previously accepted implementation was replaced with a template.

Independent reference validation accepts44820/44820 color/light planes,44784
exact. This includes nine Haste material/seed profiles, six each for Slowed and
Bleeding, fifteen Hero/custom/raw-alpha/minimal/empty actor profiles, twelve
back/front/both transformed/clipped/extended/maximum-actor canvas matrices and
144 static stock-body composition planes (143 exact). Only Slowed uses RGB1,
with strict alpha/coverage/light/actor policy; the measured exp/body-blend proof
and the initial strict failed report are retained locally. Slowed's composited
RGB1 frame is also pinned at source pixel coordinates. No source-dependent pixel
correction or math clamp was added. Full Act/onBody carrying remains open.

Standalone static and DLL Status suites both pass18cases/8028assertions. Windows
host build exits0, indexes4497files/5591tests, and focused host CTest passes
487/487. make check exits0 against origin/dev. Native gallery visits188/188 IDs;
all search, page restoration, no-match, cycle wrap and three animation/pause/
stop/restart checks pass. All24 exact native timeline image comparisons pass,
including one/two loops restoring initial frames, reverse, phase restoration
and restart. The required example smoke passes1/1 and stays alive6seconds
(minimum2), exit0; the owned preview is closed. Engine-owned screenshots were
visually inspected for all three default/custom variants.

StatusShape owns its source-ordered silhouette geometry; prepared analytic
kernels own shading/wound data and borrow immutable settings only during draw.
Time is injected, RNG streams match source order, validation/configuration is
transactional and Result-based, active parameters remain descriptor-owned and
cold, and unchanged source-dependent actor requirements are observable in the
parameter schema. Haste dust preserves raw packed scalar colors (including0
and alpha bits), while palette arrays remain opaque. Bleeding height1 is
rejected at preparation with /options/actor/height; minimumHeight2 is exposed
from the same descriptor, older descriptors retain1. Versioned configuration
round trips, ownership, rejection, layering and replay are tested. No new module,
upward include, mutable process cache, dependency workaround or architecture
exception was introduced. Evidence under build/ stays local and unstaged.

Next: remaining ten Status algorithms. Full Act/posed stock and custom body
composition, hitstop/shake/ghost/target, live parameter/background editing,
OBJECT/platform CI and measured performance/long-session acceptance remain
required before the244-effect goal can be marked complete.

### Next five Status algorithms - host/native validation in progress

Accepted count remains188/244; compiled source catalogue is193. Stunned, Sleep,
Confused, Charmed and Cursed add their distinct source procedures, not a shared
placeholder effect. Stunned60x64/origin30,56/duration2/seed2 uses five depth-split
orbiting stars/birds, actual two-state bird wings, analytic/bitmap star forms,
glitter, flares and twinkles; birds is a four-stop ramp, beak a raw packed scalar.
Sleep60x82/26,74/2.4/3 uses actual four-size Z masks, oldest-first ordering,
closed-eye skin/lids, breathing light and source RNG motes. Confused60x78/30,70/
2.4/6 uses a72-segment spiral, three question-mark glyphs, overshoot sizing,
popping rays and eye darts; marks is a four-stop ramp. Charmed60x76/30,68/2.4/8
uses two exp heartbeat peaks, actual heart geometry/rings and masks, eye glints,
skin-derived blush and floating hearts. Cursed64x84/32,76/2.4/9 uses a seeded
tileFbm stain, winding smoke, burning eyes and source skull/tear/scanline glyphs.

StatusVisual owns no mutable state and samples borrowed static glyphs through
synchronous trusted typed callbacks; explicit special-color0 remains distinct
from no mapping. StatusEyes prepares owning eye components. Stock Hero uses the
original DFS detector, with neighbor order, color-count insertion ties and stable
sort preserved. Dummy and Custom use explicit eye arrays; Custom[] means no eyes,
not auto-detection. Declared eyes can number64 (the automatic detector's two-eye
cap does not apply) and can extend their drawn second row beyond a height1 mask.
Sleep's unused source lid-color read has no output/RNG effect and is omitted.
Cursed preserves the original [0,0] row substitution when height1 rounds a smoke
sample past its mask. All five reject an absent actor but accept empty masks.

All81920 reference color/light planes are exact, with tolerance0:30 material/
seed profiles,25 Hero/custom/raw-alpha/one-cell/empty profiles, three beak raw
color profiles, four one-cell declared-eye profiles and four64-eye profiles;
twenty back/front/both transformed/clipped/extended/maximum-actor canvas matrices;
and248 static stock-body composition planes. The six independent source icon
fixtures and actual Hero/Dummy/Custom eye results are also pinned in unit tests.
Standalone static/DLL Status suites both pass48cases/21166assertions. Initial
local compile correction used the existing shader callback (not shade); the
reference composition wrapper now honors optional drawBack for Sleep, exactly
as the canonical reference adapter does. Neither changed upstream sources.

Host/source/native acceptance remains pending. Full Act/onBody/posed/custom
composition, hitstop/shake/ghost/target, parameter/background authoring,
OBJECT/platform CI and performance/long-session validation remain required.
After these five kernels, five Status algorithms still remain: Burning, Frozen,
Poisoned, Rage and Petrified. No architecture exception or dependency workaround
was introduced; raw evidence remains ignored under build/.

### Five more Status algorithms accepted (193/244)

Stunned, Sleep, Confused, Charmed and Cursed complete this batch. Eight of the
thirteen Status kernels are accepted; the remaining five are Burning, Frozen,
Poisoned, Rage and Petrified. Total algorithm acceptance is193/244,51remain:
Status5, Melee12, UI12, Transitions10 and Moves12. The complete Act/editor/
platform/performance goal remains active and is not claimed by kernel counts.

All81920 reference color/light planes are exact (tolerance0), including66
material/seed/actor profiles, twenty layered transformed/clipped/extended/dense
canvas matrices and248 static stock-body composition planes. Shared icon and
eye data also have independent original-source regressions. Standalone static
and DLL Status suites both pass48cases/21166assertions. The Windows host build
exits0 and indexes4504files/5621tests; focused CTest passes517/517; make check
exits0 against origin/dev. All193 native catalogue IDs are visited, with search,
no-match/page restoration/wrap and all five animation/pause/stop/restart checks
passing. Forty exact native timeline image comparisons pass, including one/two
loops returning to initial frames, phase restoration, reverse and restart.
Engine-owned screenshots were inspected for all five default/custom variants.
Required example smoke passes1/1, alive through6seconds (minimum2), exit0; the
owned preview is closed.

Architecture requirements applied: immutable prepared owning eye/shape data,
no retained actor/input pointers or mutable process cache; injected time and
source RNG order; descriptor-owned cold typed options; transactional Result/
nodiscard configuration with versioned round trips and unchanged unknown-field
rejection; documented synchronous callback borrows/costs; source-owned scalar
colors and bounded ramps; and per-module tests. No ECS System, Link, new module,
upward include, dependency workaround or architecture exception was introduced.
Existing opaque ramp semantics and raw packed scalar colors are preserved.
Evidence under build/ remains local and excluded from staged source changes.

These are kernels plus static-body presentation. Complete Act/onBody poses,
custom posed carrying, hitstop/shake/ghost/target, live parameter/background
editing, OBJECT/platform CI and measured performance/long-session validation
remain mandatory before the244-effect goal can be completed.
### Three body Status kernels - validation in progress

Burning, Poisoned and Rage now have native procedural implementations. Acceptance
remains193/244 until host/native and source gates finish. The cold actor option
descriptor can expose a `constraints.geometryRule` object with stable `id` and
human-readable `description`. Burning requires source-valid hotspot/scorch sites;
Poisoned requires a cell below row4 with occupied left, right and upper neighbors.
These rules validate a prepared snapshot before commit. A rejected configuration
does not change the active effect. Other effects do not acquire these restrictions.
The version1 persisted configuration and actor formats are unchanged.


### Three body Status algorithms accepted (196/244)

Burning, Poisoned and Rage are accepted as procedural kernels plus static-body
presentation. Burning preserves the source hotspots, char ramp, flame tongues,
smoke/embers and body blink; Poisoned preserves recursive veins, recoloring,
blisters and sweat; Rage preserves heartbeat body judder, eyes/brows, anger mark,
floor cracks, steam, stress lines and grit. All options are prepared and owned.
The new cold geometry rules use structured Result failure before publication;
existing descriptors without rules retain their input contracts. No ECS model,
module boundary, stored schema or cross-domain lifecycle changes were introduced.

Independent pinned-source verification covers42 parameter/actor profiles,
12 back/front/both canvas matrices including dense256x256 actors, and74 static
Hero/Dummy compositions:52,036/52,036 exact planes, RGB/light/alpha tolerance0,
with matching actor visibility. Source-invalid body shapes are rejected; sparse
source-valid shapes remain accepted, including raw-zero and zero-alpha pixels.
Rage's original seed-only global crack cache can reuse the first actor's geometry
across actors. Static composition references load a fresh original module per
actor; native prepared instances own the actor-specific cracks. Full Act is not
covered by these static compositions.

Static and DLL Status suites pass61cases/28,233assertions. Windows host build
passes with4508 indexed files/5634 tests; focused host CTest passes530/530.
Native gallery visits196/196 IDs, checks navigation/default/custom parameter
variants and playback, and passes24 exact timeline restoration checks across
the three additions. The example smoke run stays alive6seconds (minimum2).
The final source gate passes against origin/dev. Injected clock/named RNG,
owning snapshots, typed hot paths and atomic preparation rules apply; no
architecture exception was taken.

48 kernels remain: Frozen/Petrified, Melee12, UI12, Transitions10 and Moves12.
Complete Act/onBody/poses/custom carrying/hitstop/shake/ghost/target, live
parameter/background editing, OBJECT/platform CI, measured performance and
long-session validation remain required before all244 can be called complete.

### Petrified accepted (197/244)

Petrified now preserves the complete source kernel: mottled stone recoloring,
stuttering creeping front with grit/dust/sparks, recursively branching cracks,
pre-break shudder/white flash, tumbling chunks/flakes, grit, dust kicked up by
resting debris and transient lights. Palette/seed/actor are configurable. Time
and RNG belong to each instance; source fixed120Hz accumulation and RNG call
order are retained, including threshold-adjacent samples. Actor pixels, shape,
stone map and cracks are owned. The source samples body pixels while emitting,
so an absent/empty mask is rejected before construction with structured Result;
one-pixel and height1 masks remain accepted. No stored format or module-boundary
change is required, and no architecture exception was taken.

Independent pinned-source verification covers13 parameter/actor profiles at601
times each,4 transformed/clipped/extended/dense256x256 back/front/both canvas
matrices and48 static Hero/Dummy compositions:16,190/16,190 exact planes,
RGB/light/alpha tolerance0, with matching actor visibility. Static and DLL
Status tests pass67cases/32,452assertions. The host build passes with4509 files
indexed into5640 cases; focused PixelFX CTest passes536/536. The first host
index attempt could not open Ninja's dependency log; the same complete build
passed after that process exited. This was an indexing failure, not a weakened
compile/test check.

The native gallery visits197/197 IDs, including default and custom-actor
Petrified variants. Search, navigation, playback and6 exact timeline restoration
checks pass. Engine-owned screenshots show stone, pre-break white flash and
burst. The6-second example smoke run passes (minimum2seconds). The final
`make check` passes against origin/dev. These results cover kernels/static body,
not full Act composition. No performance gain is claimed.

47 kernels remain: Frozen, Melee12, UI12, Transitions10 and Moves12. Full
Act/onBody/poses/custom carrying/hitstop/shake/ghost/target, live parameter and
background editing, OBJECT/platform CI and measured performance/long-session
validation remain mandatory before completion of all244.

### Frozen accepted; all thirteen Status kernels (198/244)

Frozen preserves chilled body recoloring, one-second shiver, growing frost
ferns, snow ledges, hanging/growing/dripping icicles, steaming breath, falling
and thrown flakes, sparkle, floor frost and cold light. Palette/seed/actor are
configurable. Prepared shape/ferns/icicles/snow/mouth are instance-owned; no
global mutable plan cache or runtime Node/Python is used. Empty masks, raw-alpha
pixels, height1 actors and up to64 declared eyes preserve source behavior.

Its original random comparator makes root order and subsequent RNG consumption
dependent on the JS sorting implementation. Native FrozenOrder reproduces the
canonical reference Node26.4.0/V8-14.6.202.34-node.21 comparison sequence, using
owned indices, insertion/natural runs/stack collapse/directional merges/gallops.
It never passes a random comparator to a standard C++ sort. This is cold bounded
preparation, not per-frame sorting; outputs retain no RNG/actor/callback borrow.
The42496-candidate bound follows the source vertical band and256x256 silhouette,
and is exercised by a full checkerboard actor. Independent original-runtime
fixtures cover125 RNG cases plus44 equal/inconsistent comparison streams;
complete order hashes, first/last indices, comparison counts and subsequent
RNG values match. These define the canonical source-runtime contract, not an
assertion that arbitrary JS engines order inconsistent comparators identically.

Source drawing parity covers16 parameter/actor profiles at601 times,5
back/front/both canvas matrices including full and checkerboard256x256 masks,
and34 static Hero/Dummy compositions:19,840/19,840 exact planes, tolerance0,
with matching actor visibility. Static/DLL Status tests both pass75cases/
38,259assertions. Ordering fixtures are in a separate source-owned test file;
raster/playback contracts remain consolidated within their file-size limit.
The Windows host build passes, indexing4512 files/5648 cases; focused PixelFX
CTest passes544/544. The198-ID native gallery, playback controls and8 exact
timeline restoration checks pass. Engine-owned screenshots show stock/custom
body frost, icicles and breath. The6-second example smoke passes (minimum2).
The final `make check` passes against origin/dev.

V8 BSD/PSF attribution and modification summary are retained beside the native
order adaptation. Stylize-owned BuildSettings installs PixelFX's MIT license,
NOTICE and FrozenOrder.LICENSE.txt into `share/eve/licenses/pixelfx`. Actual host
component installation is byte-identical to all3 source files; the disabled
module rule emits no files. This is an installation-rule check, not a new runtime
trim claim. No dependency source was patched/overridden, no stored schema, ECS
model, module boundary or cross-domain lifecycle changed, and no architecture
exception was taken. Ownership, injected time/named RNG, typed hot paths and
explicit cold preparation costs apply. No performance gain is claimed.

46 kernels remain: Melee12, UI12, Transitions10 and Moves12. Complete Act/onBody/
poses/custom carrying/hitstop/shake/ghost/target, live parameter/background
editing, OBJECT/platform CI and measured performance/long-session validation
remain mandatory before the244-effect goal can be completed.

### First three Melee kernels accepted (201/244)

Claw swipe retains the three staggered hot claw lenses, closing scars, flecks,
drips and lights. Spear sweep retains its tilted cone crescent, far/near halves,
rolling dust, grit, glint and edge lighting. Thrust retains the streak bundle,
speed lines, long star, pierce ring and particles. Their owning Melee settings
expose sparks (1..64 opaque stops, normalized4); Spear sweep also exposes raw
stone tint, and Thrust exposes reach0..512. Palette/seed and optional stock/custom/
empty/absent actor snapshots use the existing configuration boundary. All inputs
are copied into prepared programs; invalid changes leave active state unchanged.
Existing effects ignore the new inactive fields. Stored version1 options and
actor schemas retain strict unknown-field rejection.

MeleeSwing implements the original circle/cone projection, inverse quadratic,
finite-difference tangent, smear bounds, layered crescent and dotted traces.
Trusted typed shader/gain callbacks borrow state only synchronously. Draws do
not retain callbacks or mutate simulation/RNG. A first Spear sweep comparison
exposed a sand/grit velocity difference: two RNG calls in one C++ addition were
evaluated in a different order from JS. Separate ordered samples fix the cause;
the subsequent full profile comparison is exact. No numeric correction/tolerance
was added to drawing to mask that error.

Independent pinned-source verification covers41 parameter/actor profiles at601
times each,12 transformed/clipped/extended/dense back/front/both canvas matrices
and102 static Hero/Dummy compositions:50,890/50,890 exact planes, RGB/light/alpha
tolerance0 and matching actor policy. This includes raw/zero-alpha stone,
reach0/.5/1/12/128/256/512 and a source-computed coincident streak endpoint;
reach512 is also tested on the extended canvas.112 geometry samples match to
relative tolerance1e-10, and5 raw stone-material samples match exactly.

Static/DLL Melee suites both pass11cases/9,895assertions. The Windows host build
passes with4517 indexed files/5659 tests; focused PixelFX CTest passes555/555.
The native gallery visits201/201 IDs, supports default/material/custom-actor
variants and playback controls, and passes18 exact timeline restoration checks.
Engine-owned screenshots show claw marks, low cone sweep/dust and thrust/star
phases. The6-second example smoke passes (minimum2), and final `make check`
passes against origin/dev. Source references, raw frames and screenshots remain
local under build/. Geometry/material data are owned, clock/RNG are injected,
and no ECS model, module boundary, persistent-format channel or cross-domain
lifecycle changed. No architecture exception or performance gain is claimed.

43 kernels remain: Melee9, UI12, Transitions10 and Moves12. These are kernels
plus static-body presentation. Full Act/onBody/poses/custom carrying/hitstop/
shake/ghost/target, live parameter/background editing, OBJECT/platform CI and
measured performance/long-session validation remain required before all244 can
be called complete.

### Second three Melee kernels accepted (204/244)

Sword slash preserves its separate elliptical crescent, echo, tangent sampling
and dotted trace. Double slash preserves both crossing cuts, the white crossing
flash, shavings and slivers; draw-time hot state uses local geometry copies.
Dagger flurry preserves six seeded small cuts and its delayed cone finisher.
Their prepared configurations own palette/sparks/seed and optional actor data;
Sword slash additionally exposes radius0..512. Invalid preparation leaves the
active configuration unchanged. Existing schema1 strict field handling remains.

Pinned-source comparisons cover38 parameter/actor profiles at601 times each,
12 transformed/clipped/extended/dense canvas matrices and102 static Hero/Dummy
compositions:47,284/47,284 exact color/light planes, tolerance0. Radius includes
0/.5/1/12/42.5/128/256/512, with512 also exercised on an extended canvas. Custom,
empty and absent actors are covered by the kernel profiles. These measurements
do not claim full Act pose/carrying parity.

Static and DLL Melee suites each pass19cases/19,469assertions, including repeated
draws, reverse seeks, crossing-flash/finisher times and atomic radius failures.
The Windows build passes (4520 indexed files/5667 tests); focused PixelFX CTest
passes563/563. The native gallery visits204/204 IDs and passes playback controls
and18 exact timeline restoration checks for these three effects. Engine-owned
phase screenshots and a6-second example smoke (minimum2) pass. Final make check
passes against origin/dev; raw comparison frames and screenshots stay in build/.

Ownership, injected clock/RNG, cold Result preparation and typed hot-path rules
apply. No new module, ECS system, persistent channel or cross-domain lifecycle
was introduced; no architecture exception or performance gain is claimed.
40 kernels remain: Melee6/UI12/Transitions10/Moves12. Full Act/onBody/poses/custom
carrying/hitstop/shake/ghost/target, live parameter/background editing, OBJECT/
platform CI and measured performance/long-session validation remain required.

### Third three Melee kernels accepted (207/244)

Uppercut retains the rising crescent, takeoff dust, outlined three-pass comic
star, burst lines, twinkles and sparks. Spin slash retains its tilted far/front
cone smear and echo, closing flare/ring, trace and layer-tagged particles. Whip
crack retains the accelerating rolling loop, sampled pre-crack tip trail,
recoil, pop, air ring, smoke and flying slivers. The common star helper borrows
owned ray data synchronously; Whip points are sampled without per-draw vector
allocation. The instance owns its short simulation trail.

Prepared options expose sparks for all three, raw Uppercut outline, Spin radius
0..512 and Whip length1..512. Positive whip length is required by its authored
divisions. Source default/seed/palette and optional stock/custom/empty/absent
actor configurations retain strict schema1 handling and atomic failure. An
initial Uppercut preparation rejection exposed an incorrect outline descriptor
range; the complete raw-color range and alpha policy were corrected, then all
profiles were repeated. No drawing tolerance or numeric correction was added.

Independent pinned-source checks cover50 parameter/actor profiles at601 times,
12 transformed/clipped/extended/dense back/front/both canvas matrices and102
static Hero/Dummy compositions:61,708/61,708 exact color/light planes, tolerance0.
This includes5 raw outline colors,8 spin radii,7 whip lengths and extended-canvas
radius/length512. These are kernels and static-body compositions, not full Act.

Static/DLL Melee suites each pass30cases/30,608assertions. The Windows build
passes (4524 indexed files/5678 tests), focused PixelFX CTest passes
574/574, and the gallery visits207/207 IDs. Playback checks and18 exact
timeline restorations pass, with engine-owned screenshots inspected at crescent,
cross-body ring and whip-crack phases. The6-second example smoke (minimum2) and
final make check pass against origin/dev. Evidence remains local under build/.

Owned snapshots, injected time/RNG, Result preparation, typed synchronous hot
paths and documented operation costs apply; no new module/ECS/persistent channel
or cross-domain lifecycle, architecture exception or speedup claim was introduced.
37 kernels remain: Melee3/UI12/Transitions10/Moves12. Full Act/onBody/poses/custom
carrying/hitstop/shake/ghost/target, live parameter/background controls, OBJECT/
platform CI and measured performance/long-session checks remain open.

### All twelve Melee kernels accepted (210/244)

Scythe reap retains the wide dark cone crescent, cold glints, pixel-threshold
tear front, rising vapour and independent curling wisp threads. Heavy cleave
retains its raised glint, accelerating hot arc/echo, ember trace, flat impact
star, floor cracks, rolling dust, grit and tumbling rocks. Hammer smash retains
the comet wedge, rays, dent, cracked floor, force dome, rocks, chips, settling
sparkles and landing dust. Source helpers construct owned crack polylines,
visit prefixes synchronously and shade tumbling rocks from the raw stone ramp.
Wisp histories are owned by the Scythe instance and bounded to14 source points.

Scythe exposes source palette/seed and optional actor; Heavy/Hammer additionally
expose raw stone tint with packed alpha/zero preserved. Their source recipes do
not expose sparks: the particles derive their ramps internally as authored.
Generic material ownership tests therefore exercise their active palette. No
inactive parameter was made active to satisfy a reused test. Strict schema1
unknown-field rejection and configuration-failure atomicity remain unchanged.

Pinned-source verification covers40 parameter/actor profiles at601 times each,
12 transformed/clipped/extended/dense back/front/both canvas matrices and102
static Hero/Dummy compositions:49,688/49,688 exact planes at tolerance0. Independent
crack-construction fixtures cover24 configurations,564 points, fork fractions
and the following RNG sample exactly, including zero-count/negative-start cases.
Raw stone values include zero, zero-alpha, partial-alpha, white and black.
Across all12 Melee kernels,169 profiles/48 canvas matrices/408 static-body
compositions account for209,570 exact planes; the source/native Melee ID sets match.

Static/DLL Melee suites each pass42cases/42,308assertions. Windows host build
passes (4528 indexed files/5690 tests), focused PixelFX CTest passes
586/586, and the native gallery visits210/210 IDs with playback controls
and18 exact timeline restorations. Engine-owned screenshots cover tearing wisps,
hot cracked-ground impact and hammer dent/dome/debris phases. The6-second smoke
(minimum2) and final make check pass against origin/dev; evidence stays in build/.

Owned snapshots, fixed injected time/RNG, cold Result preparation, typed hot
paths and documented costs apply. No module/ECS/persistent channel or cross-domain
lifecycle changed; no architecture exception or performance gain is claimed.
All12 Melee kernels are implemented; full Act pose/carrying/hitstop/shake/ghost/
target presentation still remains.34 kernels remain: UI12/Transitions10/Moves12,
alongside live parameter/background controls, OBJECT/platform CI and measured
performance/long-session validation before completion of the244-effect objective.

### First three UI kernels accepted (213/244)

Damage number retains its impact star/ring, thrown number, two landings, squash,
white flash and lifting fade. Combo counter retains speed lines, afterimages,
impact, spring settling, beat pulse, diagonal shine, draining timer and departure.
Exclamation retains its spring-driven mark, changing proportions, surprise ticks,
wobble, glint and disappearance burst. They are separate native algorithms.

Owned UI settings expose Damage text0..64 UTF-8 codepoints, Combo label0..64 and
count-999999..999999 with source half-up display rounding. Unsupported glyphs
retain source blank spacing; this uses the upstream pixel alphabet, not a new
Unicode font. The common font lookup now matches dotless i/long s reaching ASCII
I/S through source uppercase lookup. Exclamation accepts stock/custom/empty/None
actor snapshots; Damage/Combo have no actor option as in the original metadata.
The strict schema1 codec, copied preparation data and atomic failures remain.

UiWord owns prepared glyph layouts. Typed stack items borrow into a synchronous
renderer with per-instance scratch allocated during preparation; no shared
mutable scratch, per-draw allocation, retained caller string or hot field lookup.
Renderer scratch is the only draw-time mutation; simulation/RNG are unchanged by
draw. Documented costs scale with bounded word area and outline neighborhood.

Independent pinned-source checks cover43 parameter/actor profiles at601 times,
12 transformed/clipped/extended/dense back/front/both canvas matrices and34 static
Hero/Dummy compositions:53,158/53,158 exact color/light planes at tolerance0.
Extended canvases also verify64-character Damage text and Combo label/count
-999999 beyond the normal preview crop. Eight independent glyph layouts,40 word
rasters and24 shake samples match, including mini/big fonts, unknown/Unicode
spacing, fractional scale, negative slant, overlap, alpha, bevel, outline, shadow,
shine and flash. Static/DLL UI suites each pass12cases/10,627assertions, including
owned strings, empty/64/65-codepoint bounds, malformed UTF-8, count boundaries,
round trips, reverse seeks, clocks and draw purity.

Windows build passes (4533 indexed files/5702 tests), focused PixelFX CTest passes
598/598, and the native gallery visits213/213 IDs with playback controls and18
exact timeline restorations. Engine-owned phase screenshots and the6-second
example smoke (minimum2) pass. Final make check passes against origin/dev.
Raw data, comparison reports and cleanup audit manifests remain under build/.
After the interrupted low-space runs, all affected profiles and canvases were
rerun successfully. At the user's cleanup request, obsolete exact native plane
copies were removed while source frames, reports, screenshots, current UI data
and required builds were retained; cleanup manifests identify the datasets.

Ownership, injected clock/RNG, cold Result preparation, typed synchronous hot
paths and documented costs apply. No new module/ECS/persistent channel or
cross-domain lifecycle was introduced; no architecture exception or performance
gain is claimed.31 kernels remain: UI9/Transitions10/Moves12. Full Act/onBody/
poses/custom carrying/hitstop/shake/ghost/target, live parameter/background UI,
OBJECT/platform CI and measured performance/long-session verification remain
required before completing all244.

### Second three UI kernels accepted (216/244)

Crit number retains the irregular star polygon, signed-remainder spokes, cooling
bands, speed lines, mini-font stamped plate, shine, tag disappearance and final
pop. Health bar hit retains its rounded HUD plate/heart, segmented fill, flashing
lost chunk, delayed ghost drain, sparking edge, chips and closing fold. Heart
lost retains its shaded mask, advancing crack, independently tumbling halves,
empty socket, trails and final sparkles. All three are actor-less as authored.

Crit exposes owned text/label0..64 valid UTF-8 codepoints. Health exposes from/to
fractions0..1; the prepared kernel derives effective to=min(to,from), retaining
the caller's requested values in configuration. Equal/reversed inputs still emit
the source hit/chips; no shortcut suppresses their RNG or animation. Heart uses
its source palette/seed. Strict schema1 fields, snapshot ownership and failure
atomicity remain unchanged. UiHud adds synchronous rounded-panel and source-code
heart primitives; no external/baked animation asset or shared mutable state.

Independent pinned-source checks cover42 parameter profiles at601 times each
and12 transformed/clipped/extended/dense layer matrices:51,888/51,888 exact planes,
color/light tolerance0. Crit's extended3072x768 canvas includes64-character text
and label even at its largest initial size. Blood-loss fractions include zero,
one, equal values, reversed values and small/boundary losses. Sixteen independent
panel rasters and24 heart palette/sprite rasters match fractional coordinates,
corner radii, shine, alpha, raw colors, flash overlay, scale, flips and rotation.
Static/DLL UI suites each pass22cases/21,806assertions, including content bounds,
malformed values, requested/effective health ownership, reverse clocks and draw
purity. Prepared glyph layouts/scratch and source ordered RNG are retained.

Windows build passes (4538 indexed files/5712 tests); focused PixelFX CTest passes
608/608. The gallery visits216/216 IDs with parameter variants/playback controls
and18 exact timeline restorations. Engine-owned screenshots inspect crit shine/
pop, health ghost drain and split-heart/socket phases. The6-second smoke (minimum2)
and final make check pass against origin/dev. Evidence stays under build/.

Owned facts/snapshots, injected clock/RNG, cold Result preparation, typed hot paths
and operation costs apply. No new module/ECS/persistent channel or cross-domain
lifecycle changed; no architecture exception or speedup is claimed.28 kernels
remain: UI6/Transitions10/Moves12. Full Act/onBody/poses/custom carrying/hitstop/
shake/ghost/target, live parameter/background UI, OBJECT/platform CI and measured
performance/long-session checks remain mandatory for all244 completion.

### Countdown, Coin collect and Confetti

`ui/countdown` exposes `from` (1..3, source half-up rounding), `word` (0..64
UTF-8 codepoints) and `go` (1..64 opaque colors, prepared to eight stops).
C++ stores these in `ui.countdownFrom`, `ui.word` and `ui.go`. Unknown font
characters retain source spacing. Number and GO particles share the instance RNG.

`ui/coin-collect` exposes `count` (1..12) and initial `from`
(-999999..999999), both using source half-up rounding. C++ uses `ui.count`
and `ui.coinFrom`; health-bar fractions remain `ui.from`/`ui.to`. The source
fountain, seven-point trails, ordered arrivals and HUD counter are simulated;
all possible counter glyph layouts are prepared before playback.

`ui/confetti` exposes seed and primary palette, normalized to seven stops.
Its two poppers, paper flight/flutter/landing and eleven-point ribbon chains
are instance-owned procedural state. Paper and ribbon storage is bounded and
prepared; drawing does not advance time or consume RNG. These three effects
have no source actor dependency. The gallery shows defaults and palette/seed
variants, with START! and seven coins starting at 100 in the counter variant.

Pinned-source verification covers 43 configuration profiles at 601 ticks each,
plus 12 mirror/rotation/clipping/expanded-canvas matrices across all three layers:
53,522 color/light planes match exactly at zero tolerance. This is kernel and
canvas evidence; full Act integration and final platform/performance acceptance
remain open.

### Achievement, Quest complete and Level up

`ui/achievement` exposes `title` and `text` (0..64 UTF-8 codepoints), and
`ribbon` (1..64 opaque stops, normalized to five). C++ owns these as `ui.title`,
`ui.text` and `ui.ribbon`. Mini-font glyph layout is prepared once; the source
plate, raised-star medal, spring motion and traveling shine remain procedural.

`ui/quest-complete` exposes `text` and `ribbon`, normalized to seven stops.
The stitched ribbon unrolls, letters type in, the scalloped seal stamps and draws
its check, and the ribbon rolls away. Source glyph clipping is retained.

`ui/level-up` exposes `text` plus the existing stock/custom/absent actor snapshot.
Its gathering streaks, rising light column/rings, body tint/rim/aura and falling,
bouncing, departing letters are simulated from the instance's seed and time.
The kernel default is the source Dummy snapshot. Static back/body/front stock
composition is verified; the source Act poses/hitstop/onBody carrying remain part
of the explicitly open full-Act milestone. Prepared word and actor data are
owned; drawing consumes no RNG and retains no borrowed inputs.

This batch matches 75,254 source color/light planes: 61 configuration/actor
profiles at 601 ticks, twelve layer/transform/canvas matrices and 48 Hero/Dummy
static composition frames. Separate tint/light/halo helper fixtures cover108
source rasters. Maximum256x256 actors, transparent pixels, empty masks, missing
actors, Unicode/empty/64-character text and independent ribbon ramps are covered.
The gallery compares defaults with changed text/materials and a custom Level up
actor. Final full-Act, platform and measured performance acceptance remain open.

### Blinds, CRT off and Pixel dissolve

`transitions/blinds`, `transitions/crt-off` and `transitions/pixel-dissolve`
author a320x180 screen with top-left origin. All expose primary palette/seed and
raw packed `color` through `transition.color` in C++. Zero and partial-alpha
cover colors are retained. With default opaque color, the screen is covered at
1s and both planes are empty at the declared duration. Blinds uses fixed-tick
dust simulation; CRT off and Pixel dissolve evaluate absolute time.

Pixel dissolve prepares its procedural block geometry and stable double-key
ranks once, preserving source Float32 rank storage. That preparation sorts
bounded block keys and is amortized across draws and seeks; drawing allocates no
block/glint list and uses no shared mutable seed cache. Screen glints and local
pixel-center clipping follow source transition rules; the existing weather
rounded-edge/local-glow rules are retained as their separate source policy.

The three effects match37,932 source color/light planes from30 profiles and
twelve layer/transform/canvas matrices, including mirrored, rotated and half-pixel
placements. Additional104 source clipped-write/rectangle/glint rasters cover
screen edges, blend/add writes and subpixel placement. The native gallery adds
a static procedural sky behind transition previews so covering and revealing are
visible; this does not complete the separate planned background-selection UI.
Full Act, remaining effects and final platform/performance acceptance remain open.


### Iris and Star iris

`transitions/iris` and `transitions/star-iris` are native fixed-step screen
transitions. Both expose raw packed `color` and an owning `focus` pair in logical
pixels. Iris defaults to `[160,100]`; Star iris defaults to `[160,96]`. Each
coordinate accepts finite numbers in `[-4096,4096]`, including fractional and
offscreen focus. The parameter catalogue declares `point2`, size2 and
`logical-pixels`; coordinates are never normalized or treated as palette stops.
The palette editor rejects a point parameter. Preparation copies both components
and rejects malformed input atomically before publishing a configuration.

Iris preserves the source breathing spotlight, beveled rim, collapsing glint and
opening sparks. Star iris uses bounded stack arrays for ten outline points and
scanline crossings, with lit rim edges and tip particles. Each instance owns its
time, named effect RNG, particles and lights; draw consumes no random values.
Both retain the upstream clipping and screen-space light behavior under transformed
canvases. Iris has a faint rim light at time0; both covering shapes initially stay outside
the screen. These authored initial states are covered by pinned-source goldens.
Both become fully opaque during their hold and have empty color/light planes at
2.2s. An ended native scene matches the stopped procedural background exactly.

The gallery shows default focus on the left and `[210.5,72.25]` with a different
covering color on the right. Both support owned playback, pause, single-frame
forward/reverse seeking and restart. This is a preset demonstration; live
parameter/background editing and complete source Act behavior remain open.

The source proof covers24 configurations, each601 fixed-tick samples, plus eight
transformed canvas matrices across back/front/both layers:30,096 exact color/light
planes without tolerance. Configurations include two seed extremes,1/5/64-stop
ramps, fractional/offscreen/corner/minimum/maximum focus and transparent/partial
covering colors. Static and DLL regression suites retain representative original
frame hashes, owning configuration/atomic failure checks, partitioned clocks,
reverse restore and draw purity. Raw frames are regenerable and cleaned after
acceptance; reports, source hashes, screenshots and metadata remain local.


### Diamond wipe and Glitch wipe

`transitions/diamond-wipe` covers the screen with a slanted wave of faceted
procedural diamonds, cools to the covering color, then shrinks away with ordered
glints. `transitions/glitch-wipe` uses prepared torn bands, quantized freezes and
backward jumps, a ragged split-color edge, data slivers and broken-tile outlines.
Both are analytic: owned playback and prepared seeking evaluate absolute time.
They expose the primary ramp, seed and raw packed `color`. Glitch additionally
exposes raw packed `split` (default `0xff6f2dff`, RGB255/45/111); zero and partial
alpha are preserved for both scalar colors. The gallery's right example changes
cover and split independently. Diamond rejects the undeclared split option.

Bounded immutable geometry is prepared once per configuration: at most195 lattice
candidates for Diamond and36 bands for Glitch. Instances retain no global
seed-keyed cache or external data. Diamond revisits its lattice to draw deferred
glints in source insertion order after all facets; draw allocates no glint list.
Glitch's band thickness, direction, timing and fractional tick offsets belong to
its owning prepared program; draw derives noise and quantized position solely
from supplied time and those inputs. Both preserve screen clipping and source
rectangle behavior under mirroring, fractional placement and rotation.

Pristine pinned-source comparison covers25 configurations x601 samples and eight
transformed canvases across back/front/both:31,298 exact color/light planes, no
tolerance. Cases include seed0/max,1/5/64-stop ramps, raw transparent/partial/white
covering colors, four split variants and a combined partial-color/max-seed case.
Static and DLL probes each pass28 cases /7,034 assertions, including source frame
hashes, geometry ownership, partitioned clocks, reverse seeks, draw purity,
initial/end emptiness, full opaque hold and transactional configuration rejection.
All generated raw frames remain regenerable; accepted reports, embedded source
goldens, metadata and native screenshots remain local when raw copies are cleaned.

The complete244 objective still includes the remaining effects and full source
Act/onBody/poses/custom carrying/hitstop/shake/ghost/target, live parameter and
background editing, OBJECT/platform validation and measured performance/long runs.


### Slash wipe, Shatter wipe and Burn away

All ten source screen transitions are compiled. The final three use native
fixed-tick simulations on320x180 planes with top-left origin. Slash wipe(1.7s)
grinds across the picture, cuts a second seam and separates the two lit halves.
It exposes the primary eight-stop ramp, raw packed `color`, seed and independent
`sparks` ramp:1..64 opaque inputs normalize to four stops. Shatter wipe(2.8s)
drops and bounces a glass pane, propagates shared cracks from an impact, then
turns independent shards with ordered depth, lit edges and splinters. Burn away(3s)
closes and opens with prepared scalar fronts, char veins, flames, embers, ash and
lit smoke; its primary ramp normalizes to nine stops. All raw covering colors,
including zero and partial alpha, retain source semantics.

Source raster details are explicit: convex fills use pixel-center scanlines,
source mirrored row mapping and skip color0; half-plane clipping preserves vertex
insertion order. Indexed field writes retain distinct unrotated/rotated mapping.
Field samples, reciprocal gradients and vertical gradients are stored as Float32,
with double extrema, promoted gradient arithmetic, stable equal-value ordering
and source lower-bound queries. Burning fronts depend on these storage boundaries.
The independent helper corpus covers1152 fill rasters,40 half-plane cases, four
complete fields and48 indexed-write rasters, including fractional/mirrored/rotated
placements, raw color0, dithered alpha and sampler-side light. Both static and DLL
suites pass all2664 helper assertions.

Heavy simulations use a single exclusive prepared-simulation recipe. A configuration
owns its immutable recipe; each kernel owns a share of its immutable maps/patterns.
Burn's fields and Shatter's crack geometry are generated/sorted once per configuration,
then reused by resets and prepared seeks. Mutable clock, seeded RNG, particles,
shard positions and flags remain instance-owned. Config replacement, stale-seek
rejection, moved owners and surviving/abandoned candidates are tested. Existing
simple simulation and analytic construction routes retain their behavior. Source
preparation can dominate a draw; retain the program across frames and seeks.
Neither drawing nor seeking resamples the heavy geometry, and draws consume no RNG.

Pristine source proof covers34 configurations x601 samples plus twelve transformed
canvas matrices across back/front/both:42,740 exact color/light planes without
tolerance. Seeds0/max,1/5/64-stop ramps, four covering colors and four independent
spark configurations are included. Static and DLL final-transition suites each
pass38 cases /12,683 assertions in addition to the helper corpus. Prepared clocks,
reverse seeks, layers, pure draws, material ownership and atomic failure are covered.
The fixtures use prepared seek/commit/draw for simulations; analytic-only render()
is deliberately rejected. Source Slash and Burn emit light at time0 while their
covering planes are empty; their original initial hashes remain in regression tests.

The native gallery provides default and parameter variants. Full source Act,
onBody/poses/custom carrying/hitstop/shake/ghost/target, the remaining twelve Moves,
live parameter/background editing, OBJECT/platform validation and measured
performance/long-session checks remain required for the complete244 objective.


All twelve Moves now compose their native child effects: Arrow shot, Pistol shots,
Fireball throw, Ice lance throw, Rocket launcher, Lightning call, Meteor call,
Holy smite, Dash strike, Earth stomp, Ground pound and Sword combo. Child cues
retain source ordering, first-tick creation, independent seeds and raw child time.
This establishes the effect kernels; complete posed Act composition, hitstop,
targets, ghosts and custom-body carrying remain separate required acceptance.
Ice-lance throw retains recorded one-LSB translucent RGB differences under the
plan's existing narrow allowance; coverage, alpha and light remain exact.

Moves kernel/gallery validation (2026-11-06): the catalogue matches244/244source
IDs and metadata. The remaining11Moves cover66configurations/20208planes
(20193strict, all20208accepted under the existing RGB-only allowance); Arrow shot
adds1832strict planes. Native gallery traversal covers244IDs, and all12Moves
pass72event/reverse/restart/end/tail checks. Windows SHARED build and six-second
smoke pass. All867focused cases are verified:866passed in the full run, and the
old delayed-effect visibility fixture passes after sampling the authored.9s phase.
Full Act and final UI/platform/performance acceptance remain open.


PoseTrack::sampleAuthored(seconds) returns the same effective rig pose as sample,
plus typed PoseField defined-property presence for Act composition. Raw endpoints
and snap-held keys preserve omission; interpolated samples include STAND defaults.
An explicit source undefined value is absent for this query (the two stock cases
are Uppercut's ghost reset). Snapshots own their values and survive track destruction.
poseFieldNames() borrows immutable process-lifetime names in typed field order.
The mask's ordinal bits are not a serialization format. Full Act remains in progress.


The internal ActRuntime now prepares compiled source metadata and native kernels
through an opaque token, then publishes by transfer. It preserves raw source
update sequencing (including partial windup crossing, hitstop and shake). Replay
requires the same injected dt sequence. `drawEffectLayers` intentionally excludes
body/targets; owning frame preparation also provides source-rig/target composition
and explicit input-actor RGBA presentation. The script bridge below exposes these
native owners. Live gallery integration and remaining fidelity/platform gates
remain in progress. No JavaScript is used at runtime.

stop() starts the parent fade; done() stays advisory. Source Moves children use
independent painter fade, and unlight operations can clear existing light after
fade completion. Callers decide when to stop requesting draws; draw dispatch is
not suppressed globally. This behavior is verified against pristine source on
both empty and previously colored/lit canvases.


## Owned Act playback from EveScript

Load the stylize and image modules before using the following classes. All calls
are owner-thread-affine. Results use the common `ok/value/status` boundary; check
`ok` before reading `value`.

```squirrel
function checked(result) {
    if (!result.ok) throw "PixelFX Act operation failed";
    return result.value;
}
local session = eve.PixelFxActSession();
local catalogue = checked(session.getCatalogue()); // default metadata, no effect programs
local effect = eve.PixelFx("moves/arrow-shot");
// Configure effect using the existing prepared PixelFx configuration API first.
local preparation = checked(session.prepare(effect)); // explicit cold compilation
local act = checked(preparation.publish());            // ownership transfer, once
local timing = checked(act.getTiming());               // null means no authored Act
local bounds = checked(act.prepareBounds());           // explicit raster/cache work
local box = checked(bounds.getValue());                // owning default source box
local advanced = act.step(1.0 / 60.0);
if (!advanced.ok) throw "Act step failed";
local snapshot = checked(act.getSnapshot());
```

`session.prepare(effect)` copies the effect's settings, not its current playback
state. Each Act owns its clocks, kernel, RNG and target state; sharing a session
shares only source pose-cache history. The session script object may be released
before its Acts. `step` applies the supplied delta exactly once (finite0..1s,
total wall time at most1e6s); the caller applies playback rate once, independently
of effect-specific options named `speed`. Preserve the same update sequence for
replay. `stop()` starts the source fade; snapshot `done` remains advisory.

`getCatalogue()` returns `eve.pixelfx.catalogue`, version1, with an `effects` array
of canonical id/width/height/originX/originY/duration/loop records. Inspect it once
and prepare only selected effects. `getSnapshot()` returns an owning
`eve.pixelfx.act-snapshot`, version1, with clocks, freeze/shake, pose, defined-field
presence and last-step hit timestamps. These timestamps do not substitute for a
synchronous script callback. `getTiming()` returns owning source timing with
schema `eve.pixelfx.act-timing`, version1, preserving authored order, raw defined
pose fields and absent versus null hit skin. Readers may ignore additive fields.

Bounds cover the default source canvas, authored poses and target margins;
arbitrary custom image/transform extents are not included. Bounds preparation
warms the shared source cache, so preserve bounds/frame call order for replay.
The completed bounds value survives Act/session destruction. `getStatistics()`
reports entries/hits/rasterEvaluations/clears without raster work.

For rendering, allocate background and output RGBA8 images once through the
existing Image API. Use two distinct matching images for each pair,1..4096 per
dimension. Keep background images unchanged between frames unless feedback is
intentional. The following assumes those four images already exist:

```squirrel
local frame = checked(act.prepareFrame(backgroundColor, backgroundLight, {
    originX = 48, originY = 80, x = 0.0, y = 0.0,
    flip = false, targets = true, body = "source"
}));
local presented = frame.copyTo(outputColor, outputLight);
if (!presented.ok) throw "Act frame presentation failed";
```

Preparation borrows and copies backgrounds; it does not write them or advance
clocks/RNG. The returned frame owns all pixels and can outlive every Act, session
and original image. `copyTo` validates both output images before writing, reuses
owned scratch and performs a linear plane copy/conversion without simulation.
Frame options are closed: originX/originY (integers within±4096), x/y (finite
within±4096), flip/targets (booleans), body (`"source"`, `"actor"` or `"none"`). Defaults are
zero origin/placement, false flip/targets and source body. Rotation is not exposed
by this adapter yet. Allocation during preparation may throw `std::bad_alloc`.

`body="source"` selects the pristine-style posed stand-in. `body="actor"` selects
the actual copied input actor RGBA, independently of its effect mask; arbitrary
images follow pose translation and upper-row lean/crouch shear without inferred
limbs. Source-rig numerical differences in five normal Acts remain open. This
bridge and its244-effect VM traversal do not by themselves complete native Act seeking or final platform proof.


The frame adapter also accepts optional integer `width`/`height` in1..the matching
background image dimension. They select a top-left logical canvas; native effect
work and owned frame scratch use that smaller extent. Output images still match
the original background image dimensions. `copyTo` clears both full outputs to
transparent and copies the logical rectangle using the correct destination stride.
This allows fixed GPU textures to be reused without calculating padded pixels.
Without these options the original full-image behavior is unchanged.

`body="none"` uses source body:false composition: effect passes and optional
targets/straw, without the external stand-in, its shadow, ghosts or dust. Effects
that own actor-colored pixels may still draw them. Empty input actors likewise do
not create a substitute body shadow. The Act workbench now consumes the owned
session/catalogue/frame API directly, retaining only the selected two programs;
its bounded60Hz timeline navigation uses native prepared replay tokens.

`act.stepObserved(delta, observer)` uses the same native update as `step`, with
one synchronous `observer(authoredHit, event)` call for every crossed hit. The
receiver is the VM root table. `authoredHit` preserves the raw authored object;
`event` contains index/sourceTime/actTime/effectTime. Both arguments are owning
script values and may be retained. The callback runs after target hit and
freeze/shake assignment, before pose, dust and target stepping. It may query the
Act or call stop; recursive step/stepObserved returns a failed Result.

Only the owning VM thread may invoke this API. The call retains no observer after
returning and holds no locks across script execution. A thrown script error is
reported through the common VM error reporter; stepObserved returns a
CallbackFailure diagnostic at /act/observer. Native partial progress is preserved:
the throwing hit remains pending, and retry after hitstop can invoke it again.
This is source exception behavior, not transactional rollback. Invalid callback
or delta arguments are rejected before simulation mutation. Cost is the existing
simulation step plus copying two small values and one VM call per emitted hit.
`act.prepareReplay(commands)` explicitly performs expensive reset/replay work and
returns the same owning PixelFxActPrepared token used by session.prepare.
Publish it once, then replace the visible Act when ready. Commands are a closed
array of `{op="step", delta=...}` and `{op="stop"}` records, in caller order.
Deltas are the original raw updates, including zero updates; no time-rate,
quantization or wall-time-to-tick conversion is applied by this API.

The replacement uses the original immutable recipe and retained render session,
starting at initial windup rather than the originating Act's current state.
Preparation neither changes that Act nor renders, advances the source pose cache,
or invokes observers. It rejects invalid commands before constructing replacement
state. The token survives release of the originating Act and command array.
One million commands and conservatively counted effect substeps bound native
replay work; ordinary numeric delta limits and the standard bounded script Value
conversion still apply. No caller input or callback is retained.

This operation is owner-thread affine and rejected from the originating Act's
hit callback. Its cost is one mutable-kernel recreation plus every recorded update;
use it at explicit timeline changes. It represents successful updates and stops,
not external callback side effects or partial progress from a throwing callback.
Record applicable stop operations explicitly. Rendering history is shared and
not rolled back: preserve bounds/frame ordering separately when source cache
first-winner history matters. Absolute wall time alone cannot reconstruct the
source's frame-dependent hitstop/target/ghost behavior.
The gallery presents the completed color and light planes with one common
Sprite2D center/angle/scale transform (Q/E, Z resets), fitting the rotated canvas
inside each preview. Rig/custom body, effects, targets, ghosts and dust remain
aligned; the observed shake vector follows the same view transform. This final
presentation rotation changes neither source raster algorithms nor simulation
and does not trigger another frame preparation or texture allocation. Primitive
Act placement retains its documented effect-local angle semantics; source Act
body/target direct writes are not silently reinterpreted as a different API.