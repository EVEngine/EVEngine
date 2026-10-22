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
the final frame. Cache files are ignored by Git. EVECOLL1 v2 reads old v1 box clips;
EVEDUST1 v1 rejects unknown versions/trailing fields and loads transactionally.
