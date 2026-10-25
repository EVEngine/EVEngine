# 植被风场

[返回图形渲染模块](../graphics.md)

### Native vegetation wind reference (C++)

`VegetationWindState` holds caller-owned direction, strength, published `phase` and private-source-equivalent
`updatePhase`. Normally callers use the checked operations rather than editing either phase directly.
`advanceVegetationWind(state, forward, strength, dt)` follows Pcg WindManager's
smoothed globals, downward direction component and phase advance; dt is explicit.
The phase subtracts 100 once when greater than 100, matching the source rather than
silently applying a different modulo rule. Invalid or overflowing results preserve state.

`evaluateVegetationWind(input, state)` implements PW_GeneralWind distance attenuation,
main/branch/leaf flex, object scale/orientation compensation and source volume normalization.
`VegetationWindInput` includes position, world offset, camera, object transform, width/height,
flex/frequency, maximum distance, injected sin(time/4) and sin(time), and enabled/billboard/alphaTest
switches. The root's source 0/0 normalization resolves to zero displacement. A singular transform,
invalid dimensions/globals or unrepresentable result returns a diagnostic. Double intermediates
and float output define tolerance-based behavior, not GPU bit equality.

These functions provide the CPU reference. The same deformation is now implemented in grass
vertex shaders, with typed state/profile and checked Squirrel operations. No wall clock, callbacks, scene references
or global state are retained. Caller serializes access to the single mutable state owner.

`VegetationWindProfile` separates material controls from vertex inputs and defaults to disabled.
`packVegetationWind(state, profile, seconds)` returns a validated 14-float snapshot intended for
slots 18–31 of the grass shader: direction, strength, phase, signed distance, flex, frequency,
sin(seconds/4) and sin(seconds). Zero distance encodes disabled wind; negative distance selects
the source billboard branch. Disabling alphaTest zeros leaf flex without changing alpha discard.
The calculation is pure and validates disabled profiles too. Grass shaders reserve slots 18–31:
`windGlobals` vec4 at 18, `windPhaseDistance` vec2 at 22, `windFlex` vec3 at 24,
`windFrequency` vec3 at 27, and `windSineTime` vec2 at 30. Defaults disable motion.
`applyGrassWind(shader, state, profile, seconds)` checks the complete wind schema and packet
before publishing any uniform bytes. Invalid input/schema leaves the prior shader storage intact.
It performs no GPU allocation and retains no references. Call on the Graphics owner thread outside
draw/capture callbacks. Re-baking resets shader defaults; reapply the caller-owned snapshot afterward.
The terrain-stamping example uses this API with `terrainGrass.getShader()` and an explicit clock.

`GrassFoliageSettings` selects the static PW/Pcg foliage surface profile. Its script fields are
`baseR`, `baseG`, `baseB`, `alphaCutoff`, `normalStrength`, `renderDistance`, `fadeRange`,
`hardRenderDistance`, `density`, `snowMinimumHeight`, `snowFadeDistance`, `snowProgress`,
`snowR`, `snowG`, and `snowB`.
The profile consumes albedo, tangent normal and packed metallic/occlusion/thickness/smoothness maps;
the renderer applies distance alpha fade, height snow, native directional/ambient lighting and shadows.

`TerrainDetailOverwriteSettings` ports Pcg's terrain detail override without merging its three controls:
`pcgDetailDistance` starts the shader fade, `pcgFadeoutDistance` is the fade span,
`unityDetailDistance` is the outer hard cutoff, and `unityDetailDensity` deterministically thins instances.
`detailResolutionPerPatch` is classified with Pcg's exact 2/4/8/16/32 quality mapping and all other values
fall back to VeryLow64. Apply it with `GrassField.setTerrainDetailOverwrite(settings)`; the checked Result
value is the quality ordinal (VeryLow64=0 through Ultra2=5). The call requires an uploaded foliage profile,
copies its settings, publishes all shader values atomically, and retains no references. The getters
`getTerrainDetailHardDistance()` and `getTerrainDetailDensity()` expose the applied runtime state.

The vertex path deforms the camera-facing card in world-relative coordinates, uses the effective
instance up scale for the source width/height compensation, and converts back to object space.
World/view positions follow deformation while the root varying remains unchanged. Both GLSL and
WGSL contain the formula. Vulkan runtime screenshots verified time-dependent movement and exact
restoration after disabling wind. Native Dawn/WebGPU runtime also accepted and executed the WGSL;
fixed two- and four-second captures differ, while repeated disabled captures are byte-identical.
Lighting/material output still differs across the backends, so this establishes deformation behavior
rather than cross-backend pixel identity. The terrain-detail adapter also transports deterministic
healthy/dry RGB coloring; PBR foliage and the remaining original material effects remain outside this path.


### Squirrel wind controls

`eve.VegetationWindState()` owns `strength` and `phase` fields, `setDirection(x,y,z)`,
`getDirectionX()`, `getDirectionY()` and `getDirectionZ()`. `eve.VegetationWindProfile()` exposes
`maximumDistance`, `enabled`, `billboard`, `alphaTest`, `setFlex(x,y,z)` and
`setFrequency(x,y,z)`. Configuration is mutable; checked operations validate complete snapshots.

`eve.advanceVegetationWind(state,x,y,z,strength,dt)` updates the state from a forward direction
and explicit nonnegative dt, returning the standard Result table. Invalid values preserve state.
`eve.initializeVegetationWind(state,x,y,z,strength)` implements Pcg's distinct
`InstantWindApply` path: direction and clamped strength change immediately, and phase becomes
`pow(strength*0.5+0.5,3)*0.1`. It preserves `updatePhase`, matching Pcg's private `m_windTime`;
the next advance resumes that accumulator and republishes it. It does not reuse smoothing or read a hidden clock.
`eve.applyGrassWind(shader,state,profile,seconds)` applies the snapshot without advancing it,
returning a Result table and preserving shader storage on invalid values/schema. Squirrel uses its
native float clock argument. Null or wrong object types fail VM argument conversion with a type
exception before entering the operation. Caller owns state/profile; Graphics owns the grass shader.

```squirrel
local windState = eve.VegetationWindState();
local windProfile = eve.VegetationWindProfile();
windProfile.enabled = true;
assert(eve.initializeVegetationWind(windState,1.0,0.0,0.2,0.5).ok);
// dt and seconds are injected by the caller's update loop.
assert(eve.advanceVegetationWind(windState,1.0,0.0,0.2,0.5,dt).ok);
assert(eve.applyGrassWind(field.getShader(),windState,windProfile,seconds).ok);
```

### Tree prototype vertex wind

`gfx.newTreeWindShader()` creates a backend-native Mesh3D vertex shader and reuses the engine's
default PBR fragment stage. Vulkan consumes embedded SPIR-V; WebGPU consumes matching WGSL.
The generic `newMeshShaderFromSpv` and `newMeshShaderFromWgsl` contract treats either empty stage
as the corresponding default Mesh3D stage.

`eve.TreeWindProfile()` owns `bendFactor` and `setDimensions(width,height)`. Dimensions are the
unscaled source prototype crown width and height. `eve.applyTreeWind(shader,state,vegetation,tree,seconds)`
validates the complete 16-float schema before publishing wind values and dimensions. Pcg bend
scales all three flex channels. Invalid input or shader layout preserves all prior parameter bytes.

The vertex shader keeps the prototype origin fixed and applies PW main-stem, branch and leaf motion
in world-relative coordinates. Graphics owns the shader; state/profile objects remain caller-owned.
No wall clock or scene link is retained. Call on the Graphics owner thread outside draw callbacks.

