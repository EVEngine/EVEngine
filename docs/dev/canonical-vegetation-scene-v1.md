# Canonical vegetation scene v1

`eve.vegetation-scene/1` is the owning scene-level state translated from one complete TVE 12.6 manager component
set. The definition is JSON with `schema = "eve.vegetation-scene"`, `schemaVersion = 1`, the lowercase Unity
`sourceGuid`, and required `control`, `details`, `motion`, `volume`, and `elements` values. Import publishes them as
one asset or publishes nothing; a partial manager set is a parse error.

`details` contains the three field layers; global color/alpha/overlay/wetness multipliers; color and overlay mask
ranges; alpha threshold; perspective controls; motion-highlight RGB; bending and flutter triplets; and interaction
amplitude. `control.values` is ordered as season, alpha, overlay, wetness, emissive, subsurface, size,
overlay-smoothness, overlay-normal, overlay-subsurface, overlay-scale, wetness-contrast, wetness-normal, noise-tiling,
proximity-fade, distance-fade-bias and default-conform-height. It also stores linear/HDR global and overlay RGBA plus
optional lowercase overlay-albedo, overlay-normal and 3D-noise Unity GUIDs. `motion.values` is ordered as wind power,
noise tiling, bending, branch, flutter, time speed, animated-time switch and distance fade. `motion.direction` is the
normalized canonical-world XZ direction restored from the referenced Unity Transform hierarchy. `volume.values` is
render scale, visibility mode, sorting mode and edge fade. Its required `colors`, `extras`, `motion`, and `vertex`
triplets each store render mode, texture width and texture height. Consumers validate exact lengths and ranges before
publication.

`elements` is the complete ordered list of executable `TVEElement` material snapshots in the scene. All 19 TVE 12.6
Element shaders are identified by their stable Unity shader GUID and normalized into the Colors, Extras, Motion or
Vertex channel. Each record owns its source file ID, shader GUID, normalized kind and channel, enabled/visibility
state, canonical right-handed world transform,
nine-bit layer mask, intensity, channel value, four seasonal values, optional mask-texture GUID and its color/alpha/
falloff remap, independent RGB (alpha/multiply/additive) and alpha (multiply/additive) blend modes, direction and motion
modes, inversion, volume fade, and motion power. Legacy `_ElementLayer`
indices are migrated to the corresponding layer bit. Elements whose serialized material has no recognized executable
shader are retained in the import audit as source-preserved and are not silently interpreted as another field type.
The record also owns the complete bounded material property list. Every property has its original name and type plus
normalized texture GUID, resolved package `textureAsset`, vector and scalar slots. This retains old TVE property generations such as `_MainTexRemap`,
`_WinterColor` and `_ElementEffect` for explicit runtime migration rather than discarding values that are absent from
the current shader generation. Property names are unique, limited to 128 bytes, and each element is limited to 256
properties.

Readers reject unknown schema IDs, versions, missing required fields, extra fields, malformed GUIDs, non-finite
numbers, invalid enum values and values outside their documented runtime ranges. Version 1 has no migrations. A
future shape change increments `schemaVersion` and adds an explicit migration in `AssetMigration.cpp`; readers must
never reinterpret a newer version. The asset owns all numeric and GUID text. Runtime resolution borrows no Unity
source data and must resolve texture GUIDs through the package source map before publishing a complete render
snapshot. `loadVegetationSceneElementMasks` resolves `_MainTex` and `_NoiseTex` references through those package asset
identities, strictly decodes their complete mip payloads, and returns owning linear masks under an aggregate decoded
image plus float-mask allocation budget. A missing referenced image fails the detached candidate before publication.
`evaluateVegetationSceneElementPixel` implements all 19 admitted TVE 12.6 Element fragment families. The caller
supplies the already sampled main/noise textures, per-instance parameters, particle vertex color, velocity direction,
world position/normal, terrain height, season and volume fade. This keeps model-, terrain-, particle- and time-derived
facts under their authoritative runtime owners. Evaluation returns the source RGBA, Unity-compatible color-write mask,
and independent RGB/alpha blend selections without consulting global shader state or retaining borrowed data.
`composeVegetationSceneElementPixel` applies the shader's actual volume-pass state: source-alpha composition,
destination-color multiplication, additive writes, destination-alpha multiplication, separate alpha addition, and
Unity's R/G/B/A ColorMask bits. Ordered callers therefore reproduce TVE's render-target accumulation without relying
on backend-specific fixed-function state.
`bakeVegetationSceneChannel` performs ordered CPU rasterization into one owning channel atlas, allowing Colors,
Extras, Motion and Vertex to retain TVE's independent resolutions and world mappings. The four-channel
`bakeVegetationSceneElements` API remains a same-geometry convenience path. The rasterizer maps
texel centers to world XZ, applies each Element's inverse canonical transform, samples the resolved mask with the
shader-specific UV orientation, evaluates manager edge fade, filters the selected channel layer, and composes the
pixel. Optional normal, terrain-height and time-filtered-noise arrays have exactly one value per texel; malformed or
missing inputs fail before the caller publishes the returned candidate.
The rasterizer deliberately returns TVE render-target values because hardware blending occurs in that encoded
domain. `convertVegetationSceneChannelToNative` is the explicit publication boundary: after every Element has been
composed, it decodes Motion RG from `[0,1]` to EVEngine's signed world-XZ convention. Colors, Extras and Vertex
remain value-compatible with TVE 12.6; Plant Standard consumes only Vertex A for global object size.

`EvpackVegetationSceneLoader` performs this strict decode, then `projectVegetationScene` combines Global Details,
Control and Motion with an existing material/object snapshot without reading global state. Scene state is immutable
after archive loading. `VegetationSceneGpuRuntime::create` is the graphics-thread publication boundary. It bakes all
four independently sized sets of nine layer selections from caller-owned base atlases, converts each completed atlas to native channel conventions,
uploads four coherent RGBA16F arrays, projects manager controls, and binds the resulting textures to one owned PBR
snapshot. Coherence requires one revision while each field keeps its own dimensions and world-XZ transform. The
combined four-channel upload budget is 256 MiB. The caller supplies the nonzero scene revision, explicit time through the base motion snapshot, and any
borrowed motion/fade noise leases. Candidate upload or binding failure releases every allocation before returning.
The runtime must outlive every draw borrowing `surface()`; starting explicit `release()` first invalidates all four
material field pointers, then preserves any failed texture entries for retry without leaving a dangling draw borrow. Restore or hot reload validates
and builds a detached candidate first, so failure keeps the previous runtime observable and usable.
`VegetationSceneGpuRuntime::replace` rejects stale or non-increasing revisions, constructs and binds the complete
candidate before publication, then swaps the owning projection in one graphics-thread operation. Superseded field
sets are released after the swap. A failed factory release remains owned in an observable deferred-release queue and
is retried by `release()`; the successful publication receipt reports both the new revision and pending cleanup count.

`VegetationSceneLiveElements` owns runtime-only Element volumes above the immutable imported Element prefix. Handles
contain registry owner, stable slot index and generation. Registration and withdrawal first validate a detached
complete scene and publish it through the revision-checked GPU replacement boundary; only a successful GPU swap
commits the registry mutation. Foreign owners, stale generations, withdrawn slots and stale registry/GPU revisions
fail without changing either authority. Withdrawal advances the slot generation before reuse, and generation
exhaustion retires the slot permanently. Snapshots append active live slots in stable index order, so equal-priority
composition remains deterministic.

Unity scene import publishes the hierarchy and its TVE manager in one transaction. The resulting
`eve.scene-template/3` retains the same lowercase `sourceGuid` and owns a runtime-required dependency named
`vegetationScene` to the `eve.vegetation-scene/1` asset. `VegetationSceneAssociationLoader` loads a requested scene
template, enumerates the selected EVPACK variant's vegetation scenes under a bounded asset-count budget, and requires
exactly one matching source GUID. A missing match returns `NotFound`; duplicate identities return `Conflict`; neither
case publishes an instance. `VegetationSceneInstance::create` then owns the loaded hierarchy, manager snapshot, mask
images, four-channel GPU projection and live Element registry as one graphics-thread lifetime. Its registration and
withdrawal methods use the current GPU revision, and `release()` withdraws runtime-only Elements before releasing the
manager projection. Generic scene templates may omit `sourceGuid`; they remain loadable but are ineligible for this
automatic association.
