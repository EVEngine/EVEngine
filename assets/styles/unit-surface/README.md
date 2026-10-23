# Unit surface effects

26 parameterized unit-space effects. The canonical preset catalogue is
`unitSurfacePresets` in `style.nut`; `unitSurfacePresetNames()` returns reference
order. SDK installation includes this directory at `share/eve/styles/unit-surface`.

[Same-mesh concept target](reference/same-mesh-26.png) ·
[Pilot reference](reference/pilot-same-mesh.png) ·
[Original art directions](reference/ART_DIRECTION.md)

Concept images are imagegen art, not engine frames. Current captures and review
live in `examples/unit-surface-effects/comparison-current`; earlier comparisons
are retained separately. Runtime success does not imply visual acceptance.

## Prepare, attach, update

Copy the resource directory into the game's `styles/unit-surface`, then prepare
pipelines once, outside update/render loops:

```squirrel
dofile("styles/unit-surface/style.nut");
local neutral = unitSurfacePrepareNeutral("styles/unit-surface");
local effect = UnitSurfaceEffect("styles/unit-surface", "shield", {opacity=0.65});
local body = effect.newBase(mesh, neutral);
local overlay = effect.newOverlay(mesh);
// Retain body/overlay/effect/neutral for the whole unit lifetime.
```

In the simulation update, set the same transform on all returned renderables,
then inject the root unit frame and simulation clock:

```squirrel
body.setPosition(x,y,z);
overlay.setPosition(x,y,z);
effect.setUnitFrame(x,y,z, 0.0,0.0,0.0,1.0, 1.0);
effect.set("time", seconds);
```

`setUnitFrame(x,y,z,qx,qy,qz,qw,uniformScale)` validates the complete candidate,
normalizes the quaternion, and publishes only changed values. Every child mesh
uses the same root frame, even when each child has its own transform/scale.
Patterns and impact centers consequently remain in unit space. Nonuniform root
scale and zero quaternions are rejected. Without this call, authored vertices
are assumed to already be in a shared unit coordinate system. Coordinates and
radii are unit-space distances, not screen pixels.

The example's original armored mesh is reproducible with
`python3 examples/unit-surface-effects/generate_mannequin.py`. All armor, joints
and helmet vertices share one unit origin. No external model download is needed.

## Render layers

- `newBase(mesh, neutralShader)` returns a caller-owned renderable. Material presets
  use their prepared material pipeline; light-only presets borrow neutralShader.
- `newOverlay(mesh)` adds a surface light/marking layer. Its CPU Material explicitly
  declares the transparent queue; GPU shader blending alone does not declare draw order. It can be used over an
  existing game material, but that alone cannot perform material replacement,
  actual disappearance or refraction.
- `hit`: optional `newImpactRing(unitAnnulusMesh)` places a prepared XY ring at the
  hit point. The example authors a unit-radius annulus and shares it across units.
- `teleport`: optional `newFragments(fragmentMesh)` uses a prepared mesh of cubes
  centered on a 0.125-unit lattice, with cube half-width below 0.0625. Unit-space
  cell identities and the same clock drive both the clipped base and rising debris.
  The example batches 347 authored cells into one draw.

- `frost` / `wet`: optional `newFragments(fragmentMesh)` reuses the same authored
  0.125-lattice cube mesh as sparse crystal tips or falling droplets. The frost
  shader tapers cube tops into points; wet stretches and moves sparse seeds.
- `shadow`: optional `newFragments(ribbonMesh)` takes authored curved unit-space
  ribbons. UV.x is the signed ribbon width (-1 to 1), UV.y is its length (0 to 1).
  Animated turbulence and soft width/end fades produce independent flowing wisps.
  These are transparent ribbons, not a volumetric smoke solver.
  Pass the same root transform as the base. These detail stages share the existing
  time, seed, intensity, opacity and emission parameters and add one draw each.

- `heal` / `slowTime`: optional `newFragments(orbitMesh)` takes three unit-radius
  XY ring tubes centered at Z ring IDs 0, 1, 2 (tube radius below 0.05). The detail stage
  turns these into three horizontal ellipses with rising or slow tilted motion.
  Ring tubes retain visible thickness at grazing angles; the ring pipeline is double-sided.
  One prepared mesh batches all three rings; normal depth testing keeps the rear
  arcs behind the body. The example generator supplies `orbits` and `wisps` meshes.

Material replacement is enabled for frost, burn, poison, petrify, stealth, phase,
shadow, overheat, wet, sand, metallize, bark, fracture and teleport. Stealth's base uses
alpha blending without depth writes; phase and teleport actually clip the base.
Fracture supports base cutouts when manual progress is enabled. It does not
produce detached polygonal geometry. The other presets add markings/light.

The neutral/material shaders provide a procedural lookdev material rather than
sampling the unit's existing albedo/normal maps. The example encodes material
identity in UV.x (0 metal, 1 undersuit, 2 visor) and optional plate-edge distance
in UV.y. Holy armor uses that edge distance. Imported textured characters need
an appropriate base-material integration; arbitrary UVs are not these metadata.
The metal preset uses an analytic studio reflection approximation, not a scene
reflection capture. These are explicit limitations, not silent material fallbacks.

## Parameters and playback

| Parameter | Contract |
|---|---|
| time | Nonnegative caller-injected simulation seconds |
| speed | Nonnegative phase multiplier; zero freezes phase |
| colorR/G/B | Nonnegative tint; HDR values allowed |
| intensity | Nonnegative overlay/ring brightness |
| opacity | 0–1 overlay opacity; also controls stealth base opacity |
| scale | Positive density, at least 0.01; defaults are per preset |
| rimPower | Positive Fresnel exponent, at least 0.01 |
| coverage | 0–1 noise/coating density, where relevant to the preset |
| edgeWidth | 0.005–0.3 pattern-space line width |
| distortion | Nonnegative procedural warp strength |
| seed | Nonnegative deterministic spatial variation |
| progress | 0–1 manual coverage/cutout amount |
| manualProgress | Exactly 0 or 1; enables manual progress for petrify/fracture/teleport |
| duration | Positive phase duration, at least 0.01 seconds |
| emission | Nonnegative overlay/ring HDR emission multiplier |
| hitX/Y/Z | Signed unit-space center for impact, charge and lifesteal |
| hitRadius | Positive impact falloff radius |

`setHit(x,y,z,radius)` validates all four values before publishing any of them.
Unknown parameters, invalid numbers, reserved stage/frame fields and invalid
frames throw without partial state mutation. Each instance owns its uniform
values; every prepared stage receives the same time/progress/frame values.

Hit repeats with its duration; inject time from zero for each impact and hide the
accent/overlay after the burst for a one-shot. Teleport loops unless manualProgress
is enabled. Petrify grows to its coverage and holds, unless driven manually.
To stop a material effect, hide the accent/overlay and restore the neutral/base
material. Setting intensity to zero only disables light, not a material mutation.
Use `effect.setBaseEnabled(body, neutral, enabled)` to restore both the base shader
and its opaque/transparent queue. The example's O key performs this full comparison.

## Ownership, cost and supported path

Graphics owns shader/mesh GPU resources and must outlive wrappers and renderables.
The wrapper owns the script Material objects used for queue classification; retain
the wrapper until all of its returned renderables have been destroyed.
The caller owns returned renderables and synchronizes their transforms, hides or
releases them when the target is destroyed, and recreates wrappers after graphics
reload. The wrapper retains no target, ECS pointer, callback or borrowed matrix.
Methods are main/render-thread affine and invoke no unknown callbacks.

Preparation reads two SPIR-V files and creates the required pipelines, amortized
over the effect lifetime. Playback updates only finite scalar uniforms and draws
one base plus one overlay, with an optional one-draw ring, debris, crystal, droplet, wisp or orbit layer. This is a cost
shape, not a discrete-GPU benchmark. The block is exactly 32 floats / 128 bytes.

The static mesh Vulkan path is supplied. Skinned targets need their current
deformed mesh or a compatible skinning vertex stage; arbitrary skeletal meshes,
WebGPU variants and mobile performance are not verified. The same clock/seed
sequence is deterministic on the same device; vendor floating-point results need
not be bit-exact. No wall clock or global RNG is read by the shaders/wrapper.

Rebuild with `bash assets/styles/unit-surface/build.sh` (`glslc`, `spirv-val`, or
GLSLC/SPIRV_VAL overrides). It validates Vulkan 1.2 SPIR-V and synchronizes the
standalone example copy. Canonical source is this directory, not the copied files.
