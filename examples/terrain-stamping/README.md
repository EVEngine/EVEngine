# Terrain stamping

This scene compares a procedural source terrain on the left with the processed
terrain on the right. It uses the native rotated stamp, distance mask, contrast,
terrace, smooth and thermal kernels. Four mesh chunks per terrain share height
samples at their boundaries. All input data is generated locally; no Unity package
or downloaded art is needed. Colors use the existing terrain albedo bake and the
built-in lit mesh material, so this scene does not depend on runtime GLSL compilation.
The processed terrain also bakes the Pcg GTS snow and rain albedo branches over
the splat result. This validates their profile math and ordering; normal,
smoothness, mask and displacement outputs remain outside this albedo-only pass.

Run the current engine with `eve run examples/terrain-stamping`. For an engine-owned
capture, add `--debug --mcp-port=7529` and call `eve_screenshot` after the
`TERRAIN_STAMPING_READY` message. The `pcg_validation` script table exposes readiness,
frame count and changed samples for integration checks.

This is an integration scene for the implemented kernels, not the complete Pcg
world workflow. Authoring UI and world-scale orchestration are still being ported.
Numerical tests live in `test/terrain_stamp/`; rendering must also be
checked using the newly built host executable.

`TERRAIN_COLLISION_MASK_PASS layers=1` evaluates an owned Pcg radius/layer collision-mask stack before stamping.
`TERRAIN_POLYGON_MASK_PASS nodes=3` rasterizes a closed Pcg-style PolyMask on the same real runtime path.
`TERRAIN_WORLD_BIOME_MASK_PASS entries=1` stores, resamples, invalidates, rejects stale data, and rebuilds one
stable-GUID world-biome baked mask.
`TERRAIN_GLOBAL_SPAWNER_MASK_PASS` feeds that scalar output through Pcg's image-mask transform, curve, and blend pass.
`TERRAIN_NOISE_MASK_PASS types=5` evaluates the deterministic native NoiseMask path with fractional fBm and warp.
`TERRAIN_SMOOTH_MASK_PASS passes=2` evaluates Pcg's horizontal and vertical Smooth ImageMask passes.


Startup also runs `validate-image-mask.nut` against real ImageData and owned procgen
heightmaps. `TERRAIN_IMAGE_ADAPTER_PASS` confirms RGBA8 quantization, RGBA32F row order,
and atomic rejection of nonfinite pixels/invalid settings. This requires the full
image module; the CPU-only tests exercise the separate scalar-plane kernel.


The stamp now executes through TerrainGenerationSession: reset baseline, record,
undo, redo, then copy the owned result to the terrain used for rendering.
`TERRAIN_SESSION_PASS operations=5 staged=1 persisted=1` marks the successful stamp/contrast/terrace/smooth/thermal
history and full replay. Thermal undo/redo restores sediment together with terrain;
both outputs are copied from the authoritative session before rendering.


The right terrain now includes a native grass detail layer: integer density, stable
PointSet placement, then direct GrassField foliage upload using the original static RGBA grass image,
a tangent normal map and a packed metallic/occlusion/thickness/smoothness mask.
`TERRAIN_GRASS_PASS` checks count preservation, empty resource selection and invalid-width
rollback. This loads the Pcg-authored texture documented in assets/SOURCE.md and translates the active
PW foliage shader behavior: distance alpha fade, cutoff, base tint, height snow, packed mask channels,
normal strength, shadowed directional light and thickness backlighting.

Invalid RGBA32F texture input is rejected while preserving the existing grass field.

For close inspection, call `pcg_validation.closeView()` through eve_eval before capturing.

Grass instances use independent width and height ranges; zero-width upload is rejected without replacing the field.

Terrain trees are exported into PointSet, rendered from one shared procedural tree mesh, and use
the backend-native tree wind shader. `pcg_validation.treeView()` selects the first tree; the existing
`freezeWind(seconds,enabled)` helper applies the same explicit snapshot to grass and trees.

Startup also builds a two-tile owned height workspace, stamps across the shared seam, then performs
an all-tile undo/redo. `TERRAIN_MULTITILE_PASS tiles=2` verifies the runtime binding and transaction.
It also creates a Pcg-centered two-tile `TerrainWorldWorkspace`; `TERRAIN_WORLD_PASS` verifies stable
tile naming, unified history, script-driven flatten and spawn clearing, and complete-world undo.
`TERRAIN_SPAWN_PLAN_PASS rules=4 persisted=1 modifier=1 probe=1` snapshots, restores, then executes an owned terrain
modifier stamp, texture rule, detail rule, and reflection-probe rule in one stable-ID plan.

`TERRAIN_BIOME_PRESET_PASS spawners=1 persisted=1 staged=1` saves and restores that Pcg-style preset, cancels one
partial private run, then executes its biome-active stack to one world publication
through one world transaction.
It also applies one shared detail window through an owned two-tile detail workspace and restores its
all-layer history; `TERRAIN_MULTIDETAIL_PASS tiles=2` verifies that Squirrel path.
The tree path repeats this with one shared RNG stream and renders the copied east-tile points;
`TERRAIN_MULTITREE_PASS points=4` verifies its transaction before the first present.
`TERRAIN_PROBE_PASS` verifies deterministic ReflectionProbe placement and the shared Reflection/Light probe contract.
`TERRAIN_PROBE_PROVIDER_PASS` verifies graphics-owned publication, a bounded real cubemap-face capture tick,
removal, and stable batch republishing.
`TERRAIN_WATER_FLOW_MAP_PASS` runs Pcg's separate legacy droplet tracer, axis flip, and in-place smoothing path
before rendering; it is intentionally distinct from the hydraulic flux/sediment simulation.
`TERRAIN_VELOCITY_FLOW_PASS` executes `HeightMap.FlowMap`'s fixed-step four-direction flux and normalized final
velocity magnitude as a third, separately validated water-derived scalar resource.
`TERRAIN_DERIVED_MAP_PASS` evaluates the separate `HeightMap.CurvatureMap` differential geometry and
`HeightMap.Aspect` orientation conventions before the render path starts.
`TERRAIN_TERRACE_REMOVAL_PASS` runs Pcg 4.2.2's classify, adaptive smooth, gradient-noise, directional-filter,
and Gaussian de-terracing pipeline with an explicit deterministic noise seed.
The splat path paints normalized texture weights across two owned tiles and restores all layers;
`TERRAIN_MULTISPLAT_PASS tiles=2` verifies its copy and undo/redo transaction.
`TERRAIN_SPLAT_ALBEDO_PASS pixels=1024` confirms that the three-layer packed texture result is uploaded as
RGBA8 and used by the processed terrain before the first present.
`TERRAIN_GTS_HEIGHT_BLEND_PASS layers=3 transition=0.18` confirms all three normalized splat weights are
recomputed from transformed material heights before the albedo and later GTS stages consume them.
`TERRAIN_GTS_PACKED_LAYERS_PASS layers=3 outputs=4` confirms real per-layer textures, UVs, normals, AO,
smoothness, geological strength and detail strength replace the former constant-color visualization path.
`TERRAIN_GTS_LAYER_PROJECTION_PASS triplanar=1 stochastic=1` confirms one layer uses the exact GTS
world-space three-plane projection and another uses its TriangleGrid/hash anti-tiling branch.
`TERRAIN_GTS_LAYER_DISPLACEMENT_PASS top=4 outputs=2` confirms the same packed layer set drives the GTS
top-four albedo-alpha displacement and tessellation pass before snow modifies those outputs.
`TERRAIN_GTS_GLOBAL_BLEND_PASS camera=137,96,151` confirms the shared near/far raster is generated from the
actual demonstration camera using the GTS squared-distance formula.
`TERRAIN_GTS_COLORMAP_PASS near=0.22 far=0.72` confirms the source-order near/far colormap blend.
`TERRAIN_GTS_GEOLOGICAL_PASS near=8 far=19 outputs=2` confirms height-axis near/far geological color and
packed tangent-normal overlays are committed together before weather.
`TERRAIN_GTS_DETAIL_PASS near=7 far=23 outputs=3` confirms near/far detail normals, layer-weighted
micro-shadowing, and the detail raster subsequently consumed by snow shading.
`TERRAIN_GTS_WEATHER_PASS snow=1 rain=1 pixels=1024` confirms that the same image then receives height/slope snow
and bounded-altitude rain darkening before upload.
`TERRAIN_GTS_VARIATION_PASS scales=3 intensity=0.72` confirms that three world-space repeating samples modulate
the result after weather, matching the GTS material order.
`TERRAIN_GTS_PBR_PASS outputs=4 time=2` executes the packed mask, renderer-ready tangent normal, displacement and tessellation
branches with an explicit deterministic rain-animation time.
`TERRAIN_GTS_MATERIAL_PASS packedNormalMask=1 parallax=1` uploads the packed tangent normal/AO/smoothness
texture and displacement-derived height texture to every processed-terrain renderable.
`TERRAIN_TEXTURE_MASK_PASS layer=1` confirms the selected Pcg terrain-texture weight is copied into a scalar
mask source before the same splatmap is rendered.
The game-object path executes one multi-resource rule through a two-tile owned workspace with a shared
Pcg RNG stream and seam collision domain; `TERRAIN_MULTIOBJECT_PASS` verifies copy and undo/redo.


Grass now uses the PW wind main/branch/leaf deformation with an explicit update clock.
The same update owns a looping, listener-relative procedural wind-noise Source and applies Pcg WindManager's
source-specific volume transition through `VegetationWindAudioState`; `TERRAIN_WIND_PASS audio=0.2 looping=1`
checks the real audio object before the first presented frame.
For repeatable comparisons, call `pcg_validation.freezeWind(2.0, true)` or
`pcg_validation.freezeWind(0.0, false)` after `closeView()`. Strength 1 intentionally
exercises large source deformation. The example uses typed VegetationWindState/Profile, checked advanceVegetationWind and
applyGrassWind operations. TERRAIN_WIND_PASS verifies invalid distance rejection at startup.

`TERRAIN_NEIGHBORHOOD_FILTER_PASS` runs Pcg-compatible `HeightMap.DeNoise`, `GrowEdges`, and
`ShrinkEdges` over the generated terrain while preserving their order-dependent in-place traversal.
`TERRAIN_HEIGHTMAP_FILTER_PASS` covers Pcg's four-neighbor smooth, radius-five sliding-window smooth,
and odd-square in-place convolution through the public Squirrel API.
`TERRAIN_SLOPE_QUANTIZE_PASS` validates Pcg's normalized `SlopeMap` gradient and scalar `Quantize`
midpoint rounding through the same runtime path.
`TERRAIN_HEIGHTMAP_ARITHMETIC_PASS` runs scalar and raster arithmetic, clamping, mismatched-raster
resampling, and masked lerp through the public bindings.
`TERRAIN_HEIGHTMAP_TRANSFORM_PASS` runs the unmasked Pcg normalize and direct-exponent power transforms;
the same API also exposes invert and source-formula contrast.
`TERRAIN_HEIGHTMAP_COPY_PASS` covers conditional Copy and resampled CopyClamped with a smaller output raster.
`TERRAIN_HEIGHTMAP_FLIP_PASS` verifies Pcg's rectangular matrix-transpose meaning of `HeightMap.Flip`.
`TERRAIN_HEIGHTMAP_MEASURE_PASS` reads min, max, average, and scanner edge base level from current samples.
`TERRAIN_TERRACE_QUANTIZE_PASS` executes Pcg's multi-terrace curve overload using native sampled curve rows.
`TERRAIN_SLOPE_QUERY_PASS` executes all three Pcg point-slope formulas and reports their distinct units.
`TERRAIN_HEIGHTMAP_WRITE_PASS` covers clamped fill, safe-border writes, Pcg row/column orientation, and reset.
`TERRAIN_PCG_RAW_PASS` decodes headerless Pcg RAW8 bytes through `Procgen.loadTerrainBytes`; RAW16 little-
and big-endian paths share the tested native decoder.
`TERRAIN_LEGACY_HYDRAULIC_PASS` runs the separate `HeightMap.ErodeHydraulic` CPU loop with periodic rain,
persistent four-way flux, hardness-controlled dissolution and atomic terrain/sediment publication.
`TERRAIN_LEGACY_THERMAL_PASS` runs both active legacy CPU erosion variants: synchronous cardinal
redistribution and ordered steepest-neighbor transfer with a resampled hardness map.
`TERRAIN_HEIGHTMAP_ACCESS_PASS` verifies border-clamped integer reads, Pcg normalized bilinear reads,
data presence and two-dimensional power-of-two classification.
