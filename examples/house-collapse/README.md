# Cottage collapse and rising fluid dust

Run `make run/win32-debug GAME=examples/house-collapse` (Linux: `make run/linux-debug GAME=examples/house-collapse`).

The authored two-storey cottage has actual door/window openings, shutters, timber
posts/beams, floors, course-built pitched tile roof, chimney and a landscaped base.
332 source components become 507 true convex Voronoi fragments. Four timed events
break a local wall, roof bearings, timber frame and finally the remaining structure.
Large roof courses and beams retain stronger internal connections than masonry.

`config.collapse` controls each structural group's fragment count, internal cohesion,
connection strength and exterior colour independently. It also controls seeded cook,
contact tolerance, break budget, fixed timestep, duration and each impact's position,
radius, strain, impulse and direction. Foundation/landscape remain anchored. The
collision floor and visible studio floor both have top Y=-0.45; debris outside the
raised plot falls onto that floor rather than an invisible plane above it.

The first launch bakes 16 seconds at 60Hz (961 frames), saves embedded meshes and
poses, reloads the file and destroys the rigid physics world. The content key includes
source/recipe/script bytes, preserving earlier bakes when inputs change. Later launches
load the clip without cooking or rigid simulation. `playbackAsset` optionally selects
an existing clip explicitly; leave it empty for automatic cache selection.

`config.dust` controls gas spacing, burst counts, lifetime, updraft, drag, vorticity,
lateral spread, colour, opacity, fog extinction/support radius, bake/display frequency,
froxel grid and depth image resolution. Each impact emits gas into the existing
VolumeFluid solver. Negative buoyancy raises dust, drag slows outward motion and
the last two seconds fade out. Fixed 24Hz samples use 144Hz internal substeps. A
compact `.evedust` sidecar saves XYZ/RGBA, keyed by the rigid asset and dust script.
Later launches create neither a rigid world nor a fluid solver. Pause/seek affect
both tracks directly without simulation catch-up.

Rendering reuses the engine's existing `FogVolume` / `AtmosphereVolume` /
`Volumetric` froxel path. Native injection visits only each gas particle's projected
froxel footprint, with the existing soft sphere edge and seeded world-space noise.
The existing fog shader interpolates between neighbouring froxels and Z slices;
there is no low-resolution gas-image overlay. A tile-clamped, separable reconstruction
filter smooths sparse dust footprints in the background before upload. Grid lighting is uniform incident
light with scattering/extinction, not a new volumetric shadow-scattering solver.

Fragment batches contribute their current submitted meshes to the GPU GBuffer in
that same frame. The existing fog compositor samples this live scene depth, including
the studio floor. Background fog frames supply media only; their older CPU depth
exports never overwrite the current scene occlusion. This prevents a standing-house
silhouette from lingering when fragments collapse or the playback time jumps.
One process-lifetime GBuffer dispatcher keeps weak renderer registrations, expired
before mesh release and pruned on registration/dispatch. No callbacks retain dead renderers.

The scene uses a shadow-casting directional Light3D, CSM receivers, a real studio
floor, requested 8x MSAA and high spatial FXAA. TAA is disabled to keep the paused
scene stable. Exterior draws are batched by authored structural group, plus one cut
material batch. Geometry is updated once before the forward and shadow passes.

Controls: Space play/pause; R replay; 1 intact, 2 walls, 3 roof, 4 frame, 5 final;
Left/Right scrub; A/D orbit; S shadows; F AA; V dust. Playback starts paused and holds
the final frame. Cache files are ignored by Git. EVECOLL1 v3 verifies a noncryptographic checksum over the complete payload and reads old v1/v2 clips (legacy input has no integrity guarantee);
EVEDUST1 v2 adds a noncryptographic checksum over its header and samples, with bounded,
transactional loading. Legacy v1 remains readable without an original integrity guarantee;
resaving migrates it to v2. Unknown versions and trailing fields are rejected.

默认缓存现在采用 EVECOLL1 v4：配方同时记录原生生产代码指纹、Box3D 依赖修订、
脚本与模型摘要、参数、固定步长和 fracture 随机流种子。相关 C++ 源码的未提交修改也会
使下一次构建的缓存身份改变。只有缓存不存在才会烘焙；损坏或不可读资产会报告错误。
`playbackAsset` 显式指定的资产仍允许跨构建播放，不要求匹配当前生产配方。

### Two independent playback instances

Press **M** to enter comparison mode. The left house keeps its current clock and
moves left; a second renderer shows the same frozen clip on the right, initially
paused near the final frame with reverse playback selected. No new physics world
or bake is created. CPU samples are shared; each renderer owns its GPU resources.

The existing Space, 1–5 and arrow controls affect only the left instance. On the
right, **P** toggles playback, **B** reverses its rate, **9** seeks to the start and
**0** seeks near the end. The right instance loops independently. Both times are
shown in the header. Press **M** again to release the second renderer/player and
return the primary instance to its original placement without resetting its clock.

Comparison mode hides dust because the current dust renderer does not yet support
instance-space placement. This mode validates shared rigid-fragment playback;
transformed multi-instance dust remains separate unfinished work.
