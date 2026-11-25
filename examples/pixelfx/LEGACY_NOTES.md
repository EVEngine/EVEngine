# Historical PixelFX implementation notes

These notes preserve the earlier per-effect parameter examples and validation
milestones. Their page-selection keyboard commands and milestone counts describe
the former eager gallery, not the current Act workbench. Use [README.md](README.md)
for current controls and [the native API](../../docs/usr/modules/stylize-pixelfx.md)
for current runtime contracts; compiled parameter discovery remains authoritative.

---

# Procedural PixelFX

Native procedural CPU port of 244 algorithms from patch-gg/pixelfx (MIT),
including all thirteen Nature effects, all ten Spawn effects, and Status
Haste/Slowed/Bleeding/Stunned/Sleep/Confused/Charmed/Cursed/Burning/Poisoned/Rage/Petrified/Frozen, plus Melee Claw swipe/Spear sweep/Thrust/Sword slash/Double slash/Dagger flurry/Uppercut/Spin slash/Whip crack/Scythe reap/Heavy cleave/Hammer smash, plus UI Damage number/Combo counter/Exclamation/Crit number/Health bar hit/Heart lost/Countdown/Coin collect/Confetti/Achievement/Quest complete/Level up, plus transitions Blinds/CRT off/Pixel dissolve/Iris/Star iris/Diamond wipe/Glitch wipe/Slash wipe/Shatter wipe/Burn away. The
searchable catalogue is derived from compiled effect descriptors. No sprite
sheets, external effect images or JS runtime are used. Full Act/editor/
platform/performance acceptance remains open.

Run after building this source tree:

```powershell
make run/win32-debug GAME=examples/pixelfx
```

Press 1 for combat effects and 2 for fire props. The upper group uses upstream defaults; the lower group changes palette, seed,
speed, light intensity, ray count, bolt length or impact-line count. SPACE pauses,
R restarts. Nearest filtering preserves logical pixels. Color uses alpha blending;
light is a separate additive sprite. Both textures are allocated once and updated.
Analytic one-shots repeat every 1.2 seconds in the gallery. Simulated one-shots restart
after their declared duration plus 0.8 seconds, allowing source tail particles to finish.

`eve.Stylize()` registers `eve.PixelFx`. Create with a supported id, query dimensions,
allocate two distinct RGBA8 ImageData objects, then call
`fx.renderTo(absoluteSeconds, colorImage, lightImage)`. The effect never retains images
or GPU resources. `setParameter(name, value)`, `setSeed(integer)` and
`setPaletteColor(index, redByte, greenByte, blueByte)` validate before mutation.

Fire effects support nine-color primary ramps and typed material ramps (wood/iron for the torch,
root/wax/brass for the candle, iron for the brazier). setRampColor(name, index, r, g, b) edits
a material ramp; setParameter("height", value) changes flame height. Palette resampling happens
when configuring, outside the frame loop.

All 244 effect kernels are now present. Full Act composition and final acceptance
remain in progress. See
[port plan](../../docs/dev/plans/2026-11-03-pixelfx-port.md) and
[native API notes](../../docs/usr/modules/stylize-pixelfx.md).
The isolated branch passed the full Windows SHARED build and a 6-second real-engine smoke.
Engine-owned MCP screenshots and animation were captured under build/pixelfx-validation.
The final 244-effect gallery and its acceptance are still in progress.

Press 3 for the spirit-flame page, showing its default and seeded palette variant.

Press 4 for fireball and fire tornado. The variant changes radius/speed and height.
playbackSpeed sets the clock rate; speed is the physical fireball parameter.

Press 5 for fire breath and flamethrower. Variants adjust cone spread, physical speed,
stream reach and the three-color pilot ramp. playbackSpeed remains independent.

The gallery also checks the canonical batch workflow for every default/variant instance:
getParameters and getConfiguration, prepareConfiguration(snapshot.value), then
commitConfiguration(prepared.value). It checks every Result and verifies consumed-token rejection.
The script tables reuse common Value conversion and the same source declarations as C++.

Press 6 for the simulated flame pillar: default on the left, changed seed/height/stone/palette
on the right. advance(dt) and drawTo drive an owned fixed-120-Hz kernel; results are checked.
R uses prepareSeek(0)/commitSeek to restart all timelines. S fades the selected effects and
disables automatic re-trigger until R. Hidden simulations are paused by the gallery.
Equal setters are checked not to invalidate a previously prepared seek during initialization.

Press 7 for campfire: defaults on the left; changed seed, height, wood, palette,
playback speed and light gain on the right. Both are native procedural drawings.

Page 1 now draws the default halo over the original 24x38 dummy and the variant over a
caller-authored procedural silhouette. Actor preparation owns mask, source pixels and anchors;
body textures are drawn once with checked drawActorTo, separately from effect/light textures.
This demonstrates static stock/custom actor input; skeletal poses and act choreography remain pending.

Press 8 for native rig geometry: stand, sword, greatsword, dagger, spear and hammer on the
upper row; scythe, claw, bow, pistol, launcher and handle on the lower row. Each image is generated
by eve.PixelFxRig.renderTo from a versioned pose, and getSkeleton checks the same input. This
page demonstrates explicit pose/weapon rendering; automatic act choreography is still pending.

Press 9 for a prepared native pose track: held 12-FPS limbs, independently smooth floor movement
and source easing. SPACE freezes the explicit sample time; R restarts it. The track is prepared
once and retains no input table/VM nodes; each visible frame uses renderTrackTo on the reusable
native builder and texture. This demonstrates track sampling, not complete createAct choreography.

Key 0: native Fire Ring defaults and a seed/reach/palette/playback variant.

Key P: native Arrow (upper row) and Energy Disc (lower row); defaults on left, parameter variants on right.

Key G: native Bullet Tracer, Pistol Muzzle and Shotgun Muzzle. Defaults are on the left; length/speed, muzzle height, smoke and casing material variants are on the right. R restarts shots, SPACE pauses, S fades.

Key B: native SMG Burst (upper row) and Plasma Shot (lower row). Defaults are on the left; height/material and size/speed/palette variants are on the right.

Key M: native Magic Missile defaults and height/reach/seed/palette variant. Duration expiry does not erase its source tail; the gallery allows it to finish before replay.

On page M, E inspects the exact duration tick and pauses. It uses integer tick preparation to avoid SQFloat32 rounding before the endpoint; isDone() remains true while native source tail pixels can still be drawn.

Key C: Cannon Shot (upper row) and Rocket (lower row). Key H: Homing Missile. Defaults are on the left; seed/height/speed/material/palette variants on the right. Both missile flight and impact use native procedural geometry and simulation.

Key F: Signal Flare. Key X: Small Ground Explosion, including landed burning brands and shock rings. Defaults are on the left, configurable variants on the right.

Key V: Big Explosion, with three detonations, rolling mushroom plume, molten rocks and landed fires. Defaults on the left; seed/stone/palette/playback variant on the right.

Key J: Barrel Explosion, including bulging drum, bouncing lid, tumbling hot plates and burning fuel. Defaults left; barrel/fire material, seed and playback variant right.

Key K: Dirt Explosion, with clod breakup, landing dust and crater; earth/fire material variant on right. Key L: Implosion, with spiral grit, contracting rings, compressed core and outward pop; seed/palette/playback variant on right.

Key T: Firework Burst, with spherical stars, gold-to-color trails and crackle. Key U: Firework Rocket, with palm fronds, heavy trails and tip crackle. Defaults left; seed/secondary color/palette/playback variants right.

Key O: Plasma Explosion, with boiling shell, tilted ring and branching lightning. Key Y: Cartoon POW, with extruded letter-by-letter text; POW! defaults left and BAM? color/playback variant right. Text uses the owning configuration transaction.

Key N: Grenade, with lob/bounce/roll, fuse and shrapnel blast. Key D: Smoke Bomb, with calibrated throw, thick rolling cloud and thinning wisps. Defaults left; seed/material/palette/playback variants right. Smoke spark input is normalized through the same four-step source option rule.

Key Q: Nuke, with lifting rolling cap, feeding stem, condensation veil/bell and late rim fraying. Defaults left; dust/fire palette, seed and playback variant right.

Key W: water ripples (top) and rain splash (bottom), default left and seeded palette/rate variant right. Both use owned analytic playback; SPACE pauses, S fades, R restarts.

Key A: wet-stone drip (top) and underwater bubbles (bottom). Defaults left; height/source boolean, palette/seed/rate variants right. The source-free drip is configured through the same prepared transaction as native and JSON consumers.

Key Z: simulated water splash. Default incoming stone/crown left; seeded palette/rate and stone=false variant right. Stone visibility does not suppress the source collision or late bubbles.

Key I: stone-basin fountain above floating water orbs. Defaults left; seed/palette/rate variants and larger radius right. The basin rim occludes near-side water; the orb includes tilted currents, front/back motes and recurring drips.


Press Tab or / to search actual effect IDs. Type a substring such as water/whirlpool;
Up/Down cycles matches, Enter leaves editing, Space pauses, R restarts and S fades
the visible pair. Escape leaves editing; Escape again restores the previous page.
A no-match query hides all effects. Existing page keys also restore their layouts.
Default and parameter variants fit equally sized side-by-side canvases in search.
HUD font: DejaVu Sans, license in assets/FONT-NOTICE.txt; no upstream gallery font
is imported. The catalogue is derived from created effects, not a second ID list.

Search water/water-jet for the pressurized stream and length variation, or water/waterfall for the prepared-geometry cascade and height/width/ledge variant. Both remain analytic and use the same pause/restart/fade controls.

Search `water/wave-crash` for the rolling swell, curled lip and crashing spray. The paired variant changes seed, palette, rate and light; use Space, R and S to pause, restart and fade it.

Search `water/geyser` for the complete buildup, eruption and collapse. The right variant uses height 72.5 with changed seed, ramp, rate and light. All twelve water IDs are now searchable.

Search `fire/meteor` for the falling burning rock, impact and cooling crater. The right variant changes the rock ramp, seed, fire palette, rate and light. All twelve fire effects are now searchable.

Search `weather/snow` for the three depth layers, near crystals and settling snow bank. The right variant changes seed, palette, playback rate and light.

Search `weather/fog` for three bank depths and front streaks, or `weather/clouds` for growing moonlit clouds. Paired variants change seed, palette, rate and light.

Search `weather/god-rays` for light shafts and dust, or `weather/aurora` for folded curtains, bloom and strands. The paired aurora variant also changes the `upper` hue ramp.

Search `weather/rain` for four rain depths, wind sheets, mist and wet-floor crowns/rings. The right variant changes density to0.6, seed, palette, rate and light.

Search `weather/blizzard` for wind-driven snow and spindrift, or `weather/shooting-stars` for meteors, flares and lingering trains. The right meteor variant also changes its `sky` material.

Search `weather/falling-leaves` for three leaf shapes and resting litter, or `weather/cherry-blossoms` for spiraling petals and whole flowers. The right leaf variant also changes its `reds`/`golds` materials.

Search `weather/fireflies` for three flight depths, independent flashes, wings,
light trails and illuminated meadow tips. The right variant changes `grass`
material as well as seed, palette, rate and light.

Search `weather/storm` for stepped/forked lightning, restrikes, cloud flashes,
driving rain, wet reflections and ground spray. The right variant changes
`bolt` as well as seed, palette, rate and light. All twelve weather IDs are native.

Search `lightning/spark-arc` for climbing bowed arcs, hot rods, snapping and
ballistic sparks; `lightning/ball-lightning` shows a layered wandering orb,
corona, wake and floor zap. Paired variants also change the `sparks` ramp.

Search `lightning/plasma-ball` for a glass globe with front/back filaments and
contact fingers; the right variant uses five `tendrils`. `lightning/thunderclap`
shows the flash, bolts, shock rings and dust/grit; its variant changes `reach`,
`sparks` and the owning custom actor snapshot. Full Act choreography is pending.

Search `lightning/zap-hit` for an incoming arc, overload, fern burn and kinked
sparks; `lightning/lightning-strike` shows the charging sigil, stepped leader,
three strokes, ground cracks and cooling/crumbling channel.

Search `lightning/tesla-coil` for shaded windings/torus and arc volleys/floor
zaps; its variant changes `copper`, `steel` and `sparks`. `lightning/storm-cloud`
shows billows, scud, internal flashes and rain; its variant changes `cloud`,
`rain` and `sparks`.

Search `lightning/electrified` for the actor aura, surface arcs, periodic jolts
and grounding. The right variant uses an owning custom actor and changed
`sparks`/seed/palette/rate/light. Full Act pose animation remains pending.

Search `lightning/charge-up` for inward energy, accelerating pulses, actor edge
light and the flash/bolt/ring release. The right variant uses a custom actor,
`charge=0.95` and changed sparks/material/seed/rate/light. Full Act remains open.

Search `lightning/chain-lightning` for sequential leaps, three nodes, bowed
restruck links and ground/break sparks. The paired custom actor/material
variant uses the source fixed visual chain; full Act target reactions are pending.

The `ice/ice-crystal` pair demonstrates geometry-derived rotating facets,
specular glints, a floor shadow and independently layered circling frost motes.
Its configured variant changes height to47.5 as well as palette/seed/rate/light.
The source has no actor option; all geometry and seeded motes are generated
procedurally by the native implementation.

Search `ice/snowflake-burst` for a fixed-step cold gathering/pop and crisp
six-arm flakes that spin, drift, twinkle, settle and melt. The paired variant
uses count13, changed seed/palette/rate/light; the default has9 authored flakes.
The source count is validated as an integer in1..128 through the same owning
configuration transaction as the other effects.

The `ice/ice-lance` pair shows a shaded spear, flowing ridge pulse/glint,
three faceted fletching blades, peeling frost, tumbling chips and cold air rings.
The variant uses length55.5 and changed seed/palette/rate/light. Source length
is validated in16..256 logical pixels; fractional lengths are supported.

Search `ice/ice-spikes` for the complete advancing frost wave, clustered blade
growth/glints/tremble/white flash and shattering into tumbling/skidding panes,
splinters and frost dust. The paired variant uses reach141.5/height43.5 and
changed seed/palette/rate/light. Reach accepts16..512 and height4..128 logical
pixels, including fractions, through owning configuration transactions.

Search `ice/icicle-fall` to inspect dripping meltwater, rime/tremble/root snap,
falling ice, centroid-based polygon fragmentation and impact frost/debris.
Its configured variant changes seed/palette/rate/light. The source dimensions
are fixed; there are no added root-height/length controls. Pausing, fading and
restarting follow the ordinary checked timeline API.

Search `ice/ice-shatter` for the faceted block, hit/jolt, two growing crack
waves, white flash and recursively cut tone fragments with skids/scuffs/dust.
The variant changes seed/palette/rate/light. Geometry and hit point remain the
fixed source layout, with no added actor/size controls.

Search `ice/freeze` for frost climbing Dummy/custom actor pixels, rim feathers,
sealing glass facets/light sweeps, cracks/burst and top-down thaw. The variant
uses an owning custom actor plus changed seed/palette/rate/light. Full authored
onBody pose/hitstop composition remains open. Native None omits actor overlay
while keeping the block/floor; it is not a source null-actor parity claim.

Search `ice/frost-nova` for the gathering snowflake, expanding frost wave,
three rings of crystals, drifting snow and melting ice. The paired variant
changes reach to76 plus seed/palette/rate/light. Reach uses native source
pixels (16..512); separate full caster/target Act composition remains open.

Search `ice/frost-breath` for the analytic loop: cold gust, layered billows,
wind streaks, snow and tumbling crystals. The paired variant changes spread
to0.37 and power to1.4 plus seed/palette/rate/light. Spread is a cone half-angle
in radians (0..1); power scales billow velocity (0..4). As upstream, snow,
streaks, shard motion and the gust remain active when power is zero.

Search `ice/blizzard-vortex` for the two-second analytic snow funnel, layered
wind bands and orbiters, foot puffs and mouth cloud. The paired variant changes
height to93.5 source pixels plus seed/palette/rate/light. Height accepts4..256;
larger funnels can use the caller-owned canvas API to retain outlying clouds.

Search `pickups/coin-spin` for the thick turning coin, embossed face, reeded
edge, sweeping flash, glints and bobbing floor shadow. The paired variant
changes seed/palette/rate/light. Coin radius/thickness/hover are source-fixed;
no baked sprite or extra geometry controls are used.

Search `pickups/gem-spin` for the brilliant-cut ruby with real facet shading,
pavilion light, rolling crown flashes, floor refraction and seeded glints.
The paired variant changes seed/palette/rate/light. The source mesh dimensions
and cut stay authored; its49 planes are prepared once and reused each frame.

Search `pickups/crystal-shard` for the leaned broken amber solid, sheared face,
chipped corners, rising crack pulse, ring of splinters and break embers.
The paired variant changes seed/palette/rate/light. Its22-plane geometry,
projection and fracture path stay authored, prepared once per configuration.

Search `pickups/xp-orb` for the figure-eight orb, tapering comet tail and falling
flecks. Search `pickups/loot-beam` for the rarity column, dropped sword, pulsing
rings, racing glint and upward motes. Both paired variants change source
seed/palette plus common rate/light. Paths, dimensions and sword materials stay
authored source geometry, with no baked sprite or invented shape controls.

Search `pickups/power-star` for bevelled arms, pulsing swell, rotating rays and
point sparks. Search `pickups/key-glint` for the rocking ornate key, traveling
metal glint and falling tip sparks. Paired variants change seed/palette/rate/
light. Shape dimensions and event timings remain authored source geometry.

Search `pickups/heart-pickup` for the glossy heart, double-beat squash/stretch,
ring and drifting miniature hearts. Search `pickups/potion` for the rocking
flask, lagging liquid surface, rising/popping bubbles and cork wisps. Paired
variants change seed/palette/rate/light; source shape and timing constants stay
authored. Curved surface and flask scratch layers are owned per draw.

Search `pickups/collect-burst` for the pickup white flash, cross, double ring,
curved star trails and motes. Both columns use procedural native simulation;
the right column demonstrates a different seed/material configuration.

Search `pickups/chest-open` for the rattling banded chest, sampled hinged lid,
fan rays, bouncing coins/gems and pixel dissolution. The right column uses
six gold coins and a cooler wood ramp; four gems remain in both columns.
`coins` accepts0..64. `wood` accepts1..64 opaque packed colors and is normalized
to7 source steps. Its numbers preserve signed/unsigned32-bit representation
when already7 steps, matching source strict equality in dissolution; resizing
follows the source hex conversion. Configuration round trips preserve this.

Search `critters/butterfly` for burst/glide wingbeats, veined iridescent wings,
eyespots and dust. `spots` is an independent4-step material ramp. Search
`critters/bird` for downstroke/upstroke/glide poses, a translucent wing echo,
breast shading and a drifting feather; `breast` is an independent4-step ramp.
Both accept source seed and common palette/playback/light controls. The right
column varies phase/materials. Shapes and alpha are generated natively per frame.

Search `critters/bat` for four discrete wing frames, finger bones, transparent
downbeat echo and glowing eyes/trails; `eyes` is a4-step independent ramp.
Search `critters/eyeball` for seeded veins, gaze overshoot, blinking lids,
optic nerve and partially occluded orbiting motes. `flesh` and `iris` are
independent5-step ramps; `hover` accepts finite logical pixels in0..256.
The right column changes phase/materials and eyeball height. All pixels are
procedural; eye veins are owned immutable geometry prepared per configuration.

Search `critters/fish-school` for seven procedural fish in two depth rows,
waving bodies/tails, translucent fins, dart/return motion, bubbles and caustics.
`stripe` is an independent3-step material ramp. The right column varies phase,
palette and stripe color. Fish pixels are generated natively per frame.

Search `critters/flying-skull` for bone shading, chattering jaw, layered crown
fire, mouth puffs, smoke, embers and ballistic floor sparks. `bone` is an
independent7-step ramp; `hover` accepts0..256 logical pixels, `floor` is a
boolean controlling ground shadow/light and floor sparks. The right column
uses a cooler bone ramp, greater height and floor disabled through the checked
configuration transaction. Crown geometry is prepared; flame/particle motion
is sampled procedurally from time and source seed streams.

Search `critters/frog` or `critters/ghost` for native procedural motion. The right frog changes the belly ramp; the right ghost changes hover height to 22.5. Both retain the shared pause, stop, restart, seed, speed, palette and light controls.

Search `critters/wisp` for a procedural soul flame. The right variant changes hover height to46.5 along with the common seed/palette/speed/light settings.

Search `critters/snake` for procedural slithering and tongue flicks. The right variant changes the three-step belly material, plus the common seed, speed, palette and light settings.

Search `critters/jellyfish` for the native translucent bell and lagged strands. The right variant changes the four-step glow material together with common seed, speed, palette and light settings.

Search `critters/slime` for native squash/hop/splat animation. The right variant changes hop height to34.5 together with common seed, speed, palette and light settings. Hop accepts0.01..256 (default24); zero is rejected to keep source floor-light normalization finite.

Search `holy/blessing` for the native light cone. Default dummy is on the left; the right uses a custom actor mask with the common seed, speed, palette and light variants. Landing trajectories rebuild from each owning actor configuration. Full Act animated poses remain in progress.

Search `holy/feather-burst` for delayed loose feathers, puff, down and glints. The right variant changes opaque glow color and common seed, palette, playback speed and light gain. The native simulation uses the shared fixed120Hz timeline and checked seek/configuration preparation.

Search `holy/divine-shield` for the native rotating globe. The left uses the default dummy and radius29; the right uses a custom actor and radius34.5 plus common seed, palette, speed and light variants. Radius1..256 is supported, including the no-floor-intersection branch below22. Full Act pose animation remains in progress.

Search `holy/heal` for native petals, rising hoop, actor recolor/restoration and heart riders. The default dummy is left; a custom snapshot and common seed/palette/speed/light variants are right. This effect requires an actor snapshot; none is rejected atomically. Full Act poses remain in progress.

Search `holy/heal-aoe` for a native expanding floor ring and rising healing
crosses. The right variant changes reach to80.5 and applies the common seed,
palette, playback speed and light gain controls. `reach` accepts1..256 logical
pixels; all geometry, particles and lighting are procedural.

Search `holy/holy-pillar` for the native shaft fan, sun, floor sigil and falling
feathers. The right variant uses eight shafts and common seed/palette/speed/light
controls. `shafts` accepts integer3..64, default7. No raster animation is bundled.

Search `holy/smite` for the falling light hammer, shock ring/dome, cooling
cracks and stone chips. The right variant uses another opaque stone material
alongside common palette/seed/speed/light controls. `stone` is packed RGBA,
default0xff66505a; configuration is prepared and published atomically.

Search `holy/resurrection` for the procedural shaft, climbing body recolor,
unfolding wings, wingbeat, halo and feather scatter. The right variant combines
a custom actor with a two-stop ash ramp resampled to six steps. `ash` accepts
1..64 opaque packed colors; `actor` requires a stock or custom snapshot.
Native catalogue/playback checks, engine screenshots and standard smoke pass.
Full pose/composition integration remains in progress.

Search `impacts/blunt-hit` for a native hot core, inked wedges, soft dust and
faceted clods. The right variant changes warm/dust/clod opaque colors and sets
height32.5, alongside common palette/seed/speed/light controls. Height accepts
0..256 logical pixels. Native catalogue/playback checks, phase screenshots
and standard smoke pass.

Use `,` / `.` to pause and inspect the previous/next120Hz frame. Simulations
use their active tick counter; analytic effects use the nearest120Hz position
of their own clock. `R` while paused reconstructs frame zero without resuming.
Each seek uses checked preparation/publication; a rejected preparation retains
the current frame and reports the rejection. This is useful for sub-frame
flashes and short wedge/star stages that are difficult to capture by timing.

Search `impacts/bullet-impact` for a directional wall hit, hot half-star,
rebounding spark cone, wall chips/dust/smoke and cooling pock mark. The right
variant changes stone and height32.5; stone is opaque packed RGBA and height
accepts0..256 logical pixels. Native catalogue/playback checks, exact early-
frame engine screenshots and standard smoke pass.

Search `impacts/crit-hit` for the four-beat critical star, broken ring,
flare, sparks and falling shards. The right preview sets aim=-0.5 and
height=32.5 through checked configuration preparation/publication, alongside
its seed, palette, speed and light variant. Pause and use comma/period to
inspect the120Hz keyframes; R restarts at tick0 while preserving pause.

Search `impacts/hit-spark` for the directional five-ray cross, diamond center,
late long-ray remnant, bouncing sparks and chips. The right variant changes
aim to-0.5 and height to32.5, together with its seed/palette/speed/light.
Pause and comma/period inspect all three120Hz flash beats; R resets at tick0.

Search `impacts/hit-flash` for actor white/red/rim/cooling beats and comic hit
lines. Left uses the stock dummy; right uses the caller-authored colored actor,
12lines and side=-1, with seed/palette/speed/light variation. Pause and step
through the120Hz beats. This preview layers effect planes over a static actor; Full Act/onBody
pose composition remains pending.

Search `impacts/parry` for crossing blade edges, three-beat gold star, breaking
ring and bouncing spark fan. Right sets height32.5 with seed/palette/speed/
light variation. Pause and step ticks0/3/7for the star,18for broken arcs,
48for late sparks; R restarts without clearing pause.

Search `impacts/ricochet` for an incoming tracer, floor hit, outgoing streak
and cooling skid; right changes stone, seed, palette, speed and light. Search
`impacts/shield-block` for a double-sided energy lattice, surface ripples,
glancing sparks and cell disappearance; right uses a custom actor and spark
ramp. Actor previews put back-color below the body and front-color above it,
with combined light added last. No per-frame image allocation is introduced.
Integer layer and aliased-image rejection checks run for both shield previews.
Full Act brace poses, hitstop, shake and dust composition remain pending.

Search `impacts/heavy-landing` for the dent, two floor dust rings, pebbles and
landing dust; right uses custom actor and stone. Search `impacts/ground-slam`
for hot cracks, paired crescent fronts, erupting rocks, rolling dust and cloud;
right changes stone and reach to72.5. Back dust renders below the static body.
PgUp/PgDn moves30ticks(.25s)for inspecting late phases; comma/period still
moves one tick and clamps at0. R preserves pause. Full Act FALL/crouch/ghost/
hitstop/shake composition remains pending.


Search `scifi/jetpack`, `scifi/teleporter-pad` and `scifi/hologram` for native
periodic exhaust/fire-to-smoke, lifting teleport rings and a projected rotating
crystal with scanline tears. Right previews change the opaque metal ramp;
jetpack length is40.5 and hologram split is0xff40e0ff. Typed configuration
validates metal1..64opaque colors (normalized to6), length0..128 and opaque
split. Ghost emitters reconstruct source1/240-second motion and periodic
carry-over from owned recipes; direct seek and drawing do not mutate recipes.
Verification of this batch is in progress; the complete244-effect goal and
full Act/12moves, workflow and platform/performance acceptance remain open.


Search `scifi/laser-grid` for sliding and scissoring security beams, lit steel
posts, beam-distance haze/dust, intersection flares and floor scorch/sparks.
The right preview changes seed, palette and metal ramp through checked
configuration; the metal ramp accepts1..64opaque stops, normalized to6.
Independent phase fingerprints and configuration/stale-seek tests cover the
native analytic kernel. Final batch verification is in progress.

Laser-grid validation:strict default/seed/palette/metal and transformed-layer
parity, native controls and engine screenshots pass, as do complete Windows
build,200focused host tests,6s smoke and make check. Full244goal remains open.


Search `scifi/energy-core` for beating plasma inside a front/back steel cage,
two tilted orbit rings, prepared cage arcs, sparks/motes and shock rings.
The right preview changes seed, palette and opaque metal ramp through checked
configuration. The ramp accepts1..64stops and normalizes to6. Instances own
their emission recipes and arc paths; drawing and seeking use supplied time.
Independent source-frame fingerprints cover heartbeat and arc-slot edges.
Final batch verification is in progress; the244goal remains open.

Energy-core validation:2510strict source parity planes, native controls and
engine screenshots pass, as do complete Windows build,201focused host tests,
6s smoke and make check. Full244goal remains open.


Search `scifi/scanner` for locking brackets, a body scan slice, cooling
outline, periodic data bits and a percentage/OK mini-font readout. The right
preview uses a custom actor, seed and palette; both use the existing layered
body/effect compositor. Actor snapshot preparation owns mask/pixel/anchor
inputs and rejects absent or malformed inputs. Full onBody Act pose remapping
remains pending in the complete244-effect scope. Batch validation is ongoing.

Scanner algorithm validation:6984strict source parity planes, native controls
and screenshots, complete Windows build,203focused host tests,6s smoke and
make check pass. Full onBody Act pose remapping and the244goal remain open.


Search `scifi/force-field` for a translucent front/back hex dome, two incoming
shots, sphere ripples, projected cracks, a climbing scan and glancing debris.
The right preview uses custom actor, radius72.5and a custom spark ramp. Radius
is0.125..256logical pixels; sparks accepts1..64opaque stops normalized to4.
Full brace-pose/hitstop/shake Act composition remains pending. Force-field
numeric comparison permits explicitly measured RGB1quantization differences
from reference inverse trig; alpha, coverage and light must remain exact.
The default comparison tool remains strict. Final batch gates are in progress.

Force-field validation:2654planes accepted with documented RGB1quantization
tolerance; alpha/coverage/light exact. Native controls and screenshots, complete
Windows build,205focused host tests,6s smoke and make check pass. Full brace
Act/hitstop/shake and the244goal remain open.


Search `scifi/tractor-beam` for the hovering pod and cone, climbing rings,
rocks popping/hovering/spiraling into the lens, dust, grit and light streaks.
The right preview changes seed/palette and the opaque hull/stone ramps. Both
ramps accept1..64stops, normalized to6hull and5stone entries. Prepared rock/
emission data is owned by the instance; near/far layers share injected time.
This source effect has no actor input. Final batch verification is in progress.

Tractor-beam validation:3230strict source parity planes, native controls and
engine screenshots, complete Windows build,207focused host tests,6s smoke
and make check pass. Full244goal and final composition/platform/performance
acceptance remain open.


Search `scifi/warp-out` for the charge, white body flash, head-first breakup,
upward stream and beam lift-away. Right uses a custom actor, seed and palette.
`PixelFx::actorVisibility()` / EveScript `isActorVisible()` queries source
external-body composition policy using current bounded/wrapped playback time.
The gallery applies it to the body sprite after advance/seek, including initial
render and restart. Manual drawActor/drawActorTo still draws the original
snapshot independently. Existing effects default Visible; query allocates or
mutates nothing. Full Act pose/hitstop/shake composition remains pending.
Warp-in is source-audited but not yet implemented. Final batch gates ongoing.

Warp-out validation:2938strict planes and1469matching body-policy samples;
native end/tail empty-body and seek/restart exact restoration pass. Final
Windows build,211focused host tests,6s smoke and make check pass. Warp-in
and full Act/12moves/platform/performance scope remain open in the244goal.

Warp-in: search `scifi/warp-in` for row-wise arrival and beam closure. The right
preview uses a custom actor and width27.5. External actors stay hidden until
1.4+.42seconds, then remain after the beam disappears. Empty masks are rejected
by this effect during configuration preparation. Full Act pose mapping is pending.

EMP blast: search `scifi/emp-blast` for the charging puck, rolling scanline dome
and short-out into falling dead pixels. The right preview changes the radius
to42.5and fringe color, along with seed/palette/light. Radius accepts0..256;
fringe is an opaque packed color. Comma/period step individual120-Hz ticks.

Glitch: search `scifi/glitch` for three bursts of RGB separation, horizontal
tears, interlace and residual dead pixels. The right preview uses a custom
actor and changed seed/palette/light. The effect draws its own body; the generic
external actor layer stays hidden. Full snap-pose Act mapping remains pending.

Rune circle: search `arcane/rune-circle` for a perspective floor circle,
rotating runes and rising motes. The right preview changes source seed/palette
and playback/light. It loops every4seconds; comma/period and PgUp/PgDn support
precise phase seeking. Analytic pixels follow the exact injected clock.

Star fall: search `arcane/star-fall` for11shuffled falling stars with tapered
trails and independent landing rings/flashes. The right preview changes seed,
palette and playback/light. Default first landing is tick51, variant tick48;
comma/period support exact frame seeking. The declared one-shot lasts2.5s.

Mana burst: search `arcane/mana-burst` for gathering energy, expanding rings
and nine flying sigils that pop in sequence. The right preview changes source
seed/palette and playback/light. Release occurs at tick27; default first pop
at78and variant first pop at80. Comma/period support exact phase seeking.

Spell charge: search `arcane/spell-charge` for spiralling streaks/motes,
collapsing tilted rings and a crackling orb. The right preview uses a custom
actor and changed seed/palette/light. The source loop is1.6seconds; integer
frame seeking preserves exact phases. Full HOLD/READY Act poses remain pending.

Arcane orb: search `arcane/arcane-orb` for a translucent glass sphere with
spiral arms, tilted rings and orbiting motes. The right preview changes seed,
palette, playback and light. Its procedural loop lasts2.4seconds; comma/period
and PageUp/PageDown allow exact frame navigation.

Arcane beam: search `arcane/arcane-beam` for a banded beam with twisting
strands, source/end flares, shock rings, sparks and vapour. The right preview
changes seed, palette, playback/light and actor. The procedural loop lasts
1.2seconds. Source AIM/PULL Act poses and target reactions remain pending.

Arcane missiles: search `arcane/arcane-missiles` for five homing bolts,
tapered twisting ribbons and sequential pops. The right preview changes
seed/palette/playback/light and actor. Default hits occur at ticks94,103,115,
132,146; seed123hits at94,104,111,131,145. Use comma/period for exact
frame stepping. Full OPEN/KICK/GATHER Act poses remain pending.

Blink in: search `arcane/blink-in` for spiralling bits that gather into a
line and open into a white silhouette. The right preview changes actor, seed,
palette and playback/light. External body is hidden before0.48seconds and
visible thereafter. Source LANDED/onBody Act remains pending; comma/period
steps through line32, open44and arrival58at120Hz.

Blink out: search `arcane/blink-out` for gathering light, white flash,
squeeze-to-line, burst and fading violet afterimage. The right preview changes
actor, seed/palette and playback/light. The external actor hides from0.32s;
frame39flashes and63pops at120Hz. Full onBody/IN Act remains pending.

Magic circle cast: search `arcane/magic-circle-cast` for rings and a hexagram
written by pens, sequential runes, a climbing column and breakup glints. The
right preview changes seed/palette/playback/light. Flash occurs at frame183,
breakup291and the last rune goes out382at120Hz. The source lasts3.9seconds.

Portal: search `arcane/portal` for an oval rift with fibrous arms, moving rim
hot spots and inward-pulled motes/streaks. The right preview publishes
width24/height23instead of17/29, plus changed seed/palette/playback/light.
Width/height accept[.125,128]; its procedural loop lasts2.4seconds.

Time stop: search `arcane/time-stop` for a springing clock, slowing ticks and
motes, cold freeze, cracks and falling shards. The right preview changes
seed/palette/playback/light. At120Hz the first frozen frame is211and shatter
291; comma/period can compare the boundaries exactly. Duration is3.6seconds.

Black hole: search `shadow/black-hole` for bent accretion bands, a photon ring,
incoming motes and heated rock orbits around a dark center. The right preview
changes the rock material, seed/palette/playback/light. Its four-second loop
wraps at480ticks; comma/period steps across the seam at120Hz.

Curse: search `shadow/curse` for gathering wisps, a stamping skull with burning
eyes, cracks and tumbling bone chunks, plus a violet flood over the body. The
right preview changes actor, bone ramp, seed/palette/playback/light. At120Hz,
use comma/period to compare stamp66,first crack207and breakup238. Source Act
poses/hitstop/shake remain pending.

Dark explosion: search `shadow/dark-explosion` for a gathering core, black-star
flash, burning dark sphere, shock rings, falling obsidian shards and a second
pop. The right preview changes seed/palette/playback/light. At120Hz compare
release63,collapse195and pop221with comma/period. Duration3.8seconds; continue
stepping to inspect the smoke tail.

Eclipse: search `shadow/eclipse` for a dark moon, fine pearl corona, breathing
curved streamers and a diamond bead. The right preview changes pearl ramp,
seed/palette/playback/light. Its4second loop wraps at480ticks; comma/period
steps across the seam at120Hz.

Grasping hands: search `shadow/grasping-hands` for four independent arms rising
from ink, curling their five-digit claws and being dragged back under. The right
preview changes seed/palette/playback/light. Default seed5emerges at37and first
clench flashes81at120Hz; compare80/81with comma/period. Duration3.1seconds.

Hex sigil: search `shadow/hex-sigil` for a burning pen tracing a seven-pointed
star, glyphs popping in, white flare and crumbling embers. The right preview
changes seed/palette/playback/light. At120Hz compare312/313for the flare and
328/329for breakup. Source duration4.2seconds; the glyph geometry has seven
points despite the effect name.

Life steal: search `shadow/life-steal` for seven blood orbs hovering then whipping
home, tapered trails, impact ink and caster tint. The right preview changes actor,
seed/palette/playback/light. Default seed11first arrives97and last145at120Hz;
compare96/97with comma/period. Source duration2.2seconds; full source Act target/
pose/hitstop/shake presentation remains pending. The source `dark` option is
stored but does not change this effect's pixels.

Necro skulls: search shadow/necro-skulls for five orbiting skulls, ghost tails and
a cold green flame. The right preview changes bone and flame palettes, seed,
playback and light. Three-second loop; compare ticks359/360/361at120Hz for wrap.

Shadow flames: search shadow/shadow-flames for pale-skinned violet fire with a
black heart, smoke, detached licks and embers. The right preview changes palette,
seed, playback and light. Loop duration 2.4 seconds; compare ticks 287/288/289 at
120 Hz for wrap and single-step with comma/period.

Shadow tendrils: search shadow/shadow-tendrils for seven hooked tentacles rising
from an ink pool, rim pulses, splashes and sinking. The right preview changes
palette, seed, playback and light. Source duration 3.2 seconds. Default seed 3
starts tentacles at ticks 38/45/49/53/59/66/69 at 120 Hz. The native preview safely
completes tick 59, where the pinned upstream JS line walker hangs on NaN collar
coordinates; the guide records this source defect and its separate validation.

Soul drain: search shadow/soul-drain for four pale souls racing down eleven
curves into a glowing black crystal. The right preview changes soul material,
primary palette, seed, playback and light. At 120 Hz compare 19/20 for the first
landing and 383/384/385 for the 3.2-second loop boundary.

Void orb: search shadow/void-orb for a translucent three-arm vortex, lensed arcs,
creeping pale rim and black core. The right preview changes palette, seed,
playback and light. At 120 Hz use ticks 76/77/78 around the first gulp and
261/262 around the second; compare 383/384/385 for the 3.2-second loop.

Acid splash: search nature/acid-splash for a falling gob, crown with shed beads,
etched boiling pit and pale steam. Right preview changes palette, seed, playback
and light. At120Hz compare35/36for impact,46/47for shed spikes,263/264/265for
acid disappearance. Steam RGB has the documented measured1-LSB quantization
contract; alpha, coverage and light remain exact. Duration2.8seconds.


Boulder: search `nature/boulder` for a rightward ballistic stone, impact flash,
rotating fragments and landing dust. Left uses size11/source sparks; right uses
size16/custom sparks plus the common seed/palette/playback/light variant.
Size also changes the impact time; default impact first appears at tick58.
Comma/period and PageUp/PageDown allow exact reverse/forward inspection.


Earth Spike: search `nature/earth-spike` to inspect three stratified stone spikes,
floor cracks, eruption, split halves and crumbling/sinking stumps. Left uses
height62/source glow; right uses height78/custom glow plus common parameters.
Default main eruption begins at tick41; compare40/41, then180/181for main split.
Height and glow are owning checked configuration options.


Healing Bloom: search `nature/healing-bloom` for sprout, bud, healing flare,
opening petals, lifting/turning petals, drifting crosses and wilt. Left uses
source materials; right configures both petal and heart plus common parameters.
Compare111/112for the initial flare and203/204/205for the first lifting petal.
Petal/heart accept1..64opaque stops and normalize to4before atomic publication.


Leaf Storm: search `nature/leaf-storm` for rising/flipping green/autumn/red leaves,
wind bands passing behind/in front of the body and floor litter/dust. Default
uses stock Dummy; right uses an owning custom actor and three-stop leaf palette.
The current body layer is the rest-body preview; per-effect Act posing remains
in the full acceptance plan. Compare287/288/289for the2.4s loop boundary.


Poison Cloud: search `nature/poison-cloud` for layered violet/green gas sheets,
floor tongues/curls, rising ribbons and drifting specks. Left uses spread1;
right uses spread.65with the common seed/material/playback/light variant.
Spread accepts0..4; zero preserves the source's collapsed sheets and NaN light
conversion semantics. Compare479/480/481for the4s loop boundary.


Quake: search `nature/quake` for three jolts, widening glowing rift, heaving
slabs, tumbling stones and landing dust. Left uses reach118/source glow; right
uses reach154/custom glow plus common parameters. Reach accepts0..512; zero
preserves the source central burst without slabs. Compare7/8,59/60and110/111
for the three default jolts, then263/264/265for closure start.


Root Snare: search `nature/root-snare` for roots emerging, cinching, holding and
crumbling into bark. Left uses stock Dummy/source glow; right uses a custom
actor and glow plus common parameters. Current body preview is static; full
Act posing/hitstop/shake remains in the acceptance plan. Compare74/75for cinch,
204/205for the white pre-break flash and210/211for the bark release.

Spore Burst: search `nature/spore-burst` for a spotted mushroom growing,
charging, squashing into a glowing spore cloud and sinking. The right preview
changes cap/stem materials independently as well as common parameters. Source
cap and stem ramps accept1..64 opaque stops and normalize to6and5 respectively.
Compare96/97for puff onset, then258/259for sink onset; both previews support
the gallery's frame stepping, pause, reverse seek, stop and restart controls.

Thorns: search `nature/thorns` for sequential hooked spikes, white emergence
flashes, unfolding leaf collars, dirt chips and sinking. The right preview uses
count9 and independent glow/soil materials. Count accepts1..64 integers with
the source minimum of two emitted thorns; glow/soil accept1..64 opaque stops,
normalized to4/8. Large counts retain source emissions after nominal duration;
`done()` is advisory and does not clamp the simulation. Compare14/15for the
first emergence,59/60for the last default emergence, and210/211for last sink.

Tornado: search `nature/tornado` for a wind funnel, layered rotating crown,
leaves/rocks/dust orbiting front and back, and crown lightning. The right preview
uses height144 plus custom dust/leaves and common parameters. Height accepts
1..256; dust/leaves each accept1..64 opaque stops, normalized to5/8. Compare
149and166for the two crown flashes,239/240/241for the first wrap. Fractional
next-frame cycles retain the source's measured RGB1 behavior; start/reverse/
restart restoration remains exact.

Toxic Puddle: search `nature/toxic-puddle` for oily tar, glowing veins/lips,
glossy bubbles stretching and popping, spatter and fraying vapour. The right
preview uses width63.5 and a custom sheen. Width accepts1..256; sheen accepts
1..64opaque stops normalized to4. The default source-sized frame clips large
widths; larger `drawToCanvas` targets expose the full geometry and glow. Loop
starts, reverse seek and restart restore exactly; fractional next-frame cycles
retain measured RGB1 variation, with alpha/light strict in raw comparisons.

Vine Whip: search `nature/vine-whip` for a thorny spring-driven vine growing,
coiling back, lashing with a smear, cracking with sap/torn leaves, then retracting.
The right preview changes thorn/soil ramps plus common parameters. Each material
accepts1..64opaque stops normalized to4/8. Compare43/44for lash onset,66/67for the
crack,129/130for the return dirt burst. Pause/frame-step/reverse/restart provide
exact restoration; larger `drawToCanvas` targets show sap beyond source bounds.

Coin Drop: search `spawn/coin-drop` for the poof, spinning coins, bouncing hit
glints, flat resting coins and staggered winks. The right preview uses11coins
and a custom smoke ramp plus common parameters. Coins accept0..64, including
smoke-only zero; smoke accepts1..64opaque stops normalized to4. Compare70/71for
first impact,260/261for first default wink,315/316for the last. Large counts
continue after nominal duration; frame-step/seek can inspect the longer tail.


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
