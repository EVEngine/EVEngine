# Editable house and cave generation

Generation scripts choose stages and architectural/geological policy. Native algorithms own
component constraints, dense fields, deposition anchoring and mesh extraction. No stage creates
render entities; upload and scene publication follow a successful complete build.

## House workflow

`examples/housegen/generators/house.nut` contains editable dimensions, seed, per-floor room
types and an optional upper-floor GridGraph inset. The default settings preserve the original
showcase. `main.nut` displays the completed layout.

1. Create `eve.HouseGeneration()` and check `configure(request, kit)`.
2. Check `nextFloorGrid()` to obtain an independent `Grid2D`.
3. Edit that grid directly or execute a `GridGraph` over it.
4. Check `emitFloor(grid, "living,kitchen,bedroom")`. Repeat for each requested floor.
5. Check `finish()` and use the returned `HouseLayout` for rooms, component points or rendering.

Preparing the same floor twice is idempotent. Emission rejects a wrong-size or empty mask,
unsupported upper-floor cells, removal of the stair column and missing interior components.
Failure leaves the prepared floor and RNG state available for correction and retry. The session
copies its component library, so clearing or destroying the original kit cannot invalidate a build.

The first floor may replace the default footprint. Subsequent masks remain directly supported.
`HouseLayout` retains authoritative floor/room/component semantics; PointSet is a derived
placement projection. It is not a replacement for the architectural domain model.

`HouseGenerator::generate` and `eve.HouseGen.generate` are compatibility entry points.
Their canonical implementation uses the same staged generator and legacy default schedule.

## Three-dimensional cave workflow

`examples/cave-generator/generators/cave.nut` owns the geological preset and the sequence of
surface operators. Edit that sequence to omit, repeat or reorder operations. Deposits are optional.

1. Create `eve.CaveGeneration()` and check `configure(params)`.
2. Check `buildSkeleton()`. Optionally check `setChamber(index,x,y,z,rx,ry,rz)`.
3. Check `sampleField()` to evaluate the current three-dimensional skeleton and geological fields.
4. Optionally check `field()`, edit its independent `ScalarField3D`, and check `replaceField(field)`.
5. Check selected `evolveSurface` calls: `reactivity`, `reactivePatches`, `condensation`, `curvature`.
6. Optionally check `deposit()`, after all desired surface edits.
7. Check `buildMesh()` to obtain an independent `MeshBuild` with the existing material groups.

The field spans normalized `[-1,1]` coordinates along each axis; samples are stored in X/Y/Z
order and nonnegative density means rock. `ScalarField3D.sample` and `setSample` take integer
sample indices and return Result tables. Replacement requires the configured dimensions. `carveEllipsoid` performs a native volume operation;
`extractMesh(isolevel)` also makes the same owning field useful for general parametric models,
independently of the cave session. The example exposes extra chambers as editable shape records.
After deposition, field replacement and surface retreat are rejected because deposits must
remain anchored to their host surface. Reconfigure to start another build.

Repeated mesh extraction does not mutate the staged field. An empty/invalid extraction retains
the previous field so the caller can correct it and retry. Unknown surface operations fail explicitly.
No script runs once per voxel inside the native geological loops.

`mesh.cave` is a compatibility recipe using the same canonical stages in their original order.
The three-dimensional density, erosion and Marching Cubes algorithms remain intact. Two-dimensional
cellular cave generation is a separate algorithm and is not used to replace this recipe.

## Ownership, determinism and cost

Sessions are transient owning values, used synchronously on their owner thread. They retain no
VM callbacks, borrowed libraries, render resources or ECS Links. Snapshots own their storage;
independent copies can be processed separately. No mutation can publish a partially built scene.
Configure or emit failure retains the previous session. Resource allocation exceptions follow the
engine's normal allocation policy rather than being represented as successful partial stages.

The legacy schedule preserves the original seeded random draw order. This is an algorithm-version
compatibility contract, not a cross-platform floating-point bitwise guarantee. New script-specific
random choices should use named `ProcgenContext.seedFor` streams, independently of the native
legacy stream; editing a material must not consume layout random draws.

Dense sample/evolution/deposition work stays in native code. Field snapshots and transactional
stages copy volume data, so batch field edits before replacement. Mesh extraction and upload are
separate explicit expensive steps; reuse their outputs until configuration or inputs change.

Persist script URI, seed and application configuration. Rebuild transient sessions after hot reload.
Existing `eve.house-layout` version 1 remains the layout persistence format, including its existing
unknown-field policy. This change adds no persisted session or graph schema. The examples stage
native generation before replacing their displayed result and rebuild from imported scripts on reload.

## Verification contract

`test/unit/procgen/procgen_generation_stages.cpp` covers default recipe composition, owning field copies,
floor-graph edits, invalid stage transitions, failed extraction/retry, library destruction and script
composition without graphics startup. Existing house/cave tests continue to cover geometry and
geological operators. Runtime acceptance additionally requires both examples to survive their smoke
run and fresh engine-owned frame captures; compiling alone does not establish visual parity.
