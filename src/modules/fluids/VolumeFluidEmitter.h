#pragma once
#include "common/Export.h"


#include <cstdint>
#include "common/Result.h"
#include "fluids/VolumeFluid.h"

namespace eve::fluids {

/** @brief Local nozzle domain; disk/square lie in XY, edge lies on X. */
enum class VolumeFluidEmissionShape { Edge, Square, Disk, Sphere, Cube, Distribution };

/** @brief Exact 3D metrics derived by Fluid3DEmitterBlueprintBase from resolution and density. */
struct VolumeFluidEmitterBlueprintMetrics {
    /** @brief Recommended solver rest spacing and particle diameter in metres. */
    float particleSize = 0.1f;
    /** @brief Per-particle rest mass in kilograms. */
    float particleMass = 1.f;
    /** @brief Fluid support radius in metres. */
    float smoothingRadius = 0.2f;
};

/** @brief Owning Fluid3DEmitterBlueprint values used to prepare a native 3D emitter. */
struct VolumeFluidEmitterBlueprint3D {
    unsigned  capacity   = 1000;
    float     resolution = 1.f, restDensity = 1000.f, smoothing = 2.f;
    float     viscosity = .05f, surfaceTension = 1.f;
    float     buoyancy = -1.f, atmosphericDrag = 0.f, atmosphericPressure = 0.f, vorticity = 0.f;
    float     diffusion = 0.f;
    glm::vec4 diffusionData{0.f};
};

/** @brief Owning Fluid3DGranularEmitterBlueprint values used to prepare a native 3D emitter. */
struct VolumeGranularEmitterBlueprint3D {
    unsigned capacity   = 1000;
    float    resolution = 1.f, restDensity = 1000.f;
    /** @brief Fluid3D radius-reduction percentage; runtime accepts the meaningful [0,100] range. */
    float randomness = 0.f;
};

/** @brief Evaluates Fluid3DEmitterBlueprintBase/Fluid3DEmitterBlueprint formulas in 3D.
 * @details resolution and restDensity must be finite and at least 0.001; smoothing must
 * be finite and at least 1. The pure setup-time call allocates nothing and mutates no solver.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<VolumeFluidEmitterBlueprintMetrics> evaluateVolumeFluidEmitterBlueprint3D(float resolution,
                                                                                               float restDensity,
                                                                                               float smoothing);

/** @brief Owned precomputed local emission sample, including shape-specific color. */
struct VolumeFluidDistributionPoint {
    glm::vec3 position{0.f};
    glm::vec4 color{1.f};
    /** @brief Local normalized emission direction; transformed by the nozzle pose. */
    glm::vec3 direction{0.f, 0.f, 1.f};
};

/** @brief Precomputes a bilinear alpha-masked image distribution on a spacing lattice.
 * @details Pixels are linear RGBA, bottom row first. No image pointers are retained.
 * Aspect ratio is preserved when clamping world dimensions to maximumSize.
 * At most 65536 lattice candidates and 4096 output points are allowed; excess
 * returns a diagnostic without partial output. Call during authoring/setup.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<std::vector<VolumeFluidDistributionPoint>> buildVolumeFluidImageDistribution(
    std::span<const glm::vec4> pixels, unsigned width, unsigned height, float pixelScale, float maximumSize,
    float spacing, float alphaThreshold);
/** @brief Voxelizes indexed triangles, returning surface and enclosed interior cell centers.
 * @details Setup-time only, owns its result and retains no mesh pointers. Scaled
 * bounds determine voxel size, at least spacing and at least longest extent/32.
 * Open meshes produce surface cells without assuming an enclosed interior.
 * Limits: 65536 triangles, 4M cell/triangle checks, 4096 output samples; exceeding
 * any budget returns a diagnostic rather than a partial distribution.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<std::vector<VolumeFluidDistributionPoint>> buildVolumeFluidMeshDistribution(
    std::span<const glm::vec3> vertices, std::span<const uint32_t> indices, glm::vec3 scale, float spacing);
/** @brief Builds an Fluid3D-compatible spherical surface or volume lattice during setup.
 * @details Radius and spacing must be finite and positive. Surface points emit along
 * their radial normals; volume points emit along local +Z. Candidate and output counts
 * are limited to 65536 and 4096 respectively; failure returns no partial distribution.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<std::vector<VolumeFluidDistributionPoint>> buildVolumeFluidSphereDistribution(float radius,
                                                                                                   float spacing,
                                                                                                   bool  surface);
/** @brief Builds an Fluid3D-compatible box surface or volume lattice during setup.
 * @details size is the full local box size. Surface points emit along the normalized
 * sum of their incident face normals; volume points emit along local +Z. Candidate and
 * output counts are limited to 65536 and 4096; failure returns no partial distribution.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<std::vector<VolumeFluidDistributionPoint>> buildVolumeFluidCubeDistribution(glm::vec3 size,
                                                                                                 float     spacing,
                                                                                                 bool      surface);
/** @brief Builds Fluid3D's edge lattice with per-sample radial velocity directions.
 * @details length is the full local X extent. Samples use spacing plus Fluid3D's
 * 0.01 metre separation bias; radialVelocityDegrees rotates local +Z around X
 * for each successive sample. Setup-time only, owning, and limited to 4096 points.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<std::vector<VolumeFluidDistributionPoint>> buildVolumeFluidEdgeDistribution(
    float length, float spacing, float radialVelocityDegrees);
/** @brief Builds Fluid3D's concentric disk or circumference-only distribution.
 * @details Radius/spacing must be finite and positive. Edge samples emit along
 * their radial normal; filled-disk samples emit along local +Z. Setup-time only,
 * owning, with 65536 candidate and 4096 output limits.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<std::vector<VolumeFluidDistributionPoint>> buildVolumeFluidDiskDistribution(float radius,
                                                                                                 float spacing,
                                                                                                 bool  edgeEmission);

/** @brief Value-owned emission description; +Z is mapped to direction. */
struct VolumeFluidEmission {
    VolumeFluidEmissionShape shape = VolumeFluidEmissionShape::Disk;
    glm::vec3                origin{0.f, 1.f, 0.f};
    glm::vec3                direction{0.f, -1.f, 0.f};
    /** @brief Half extents; disk/sphere use x as radius. */
    glm::vec3 extent{0.1f};
    /** @brief Nozzle speed and independent per-axis uniform native velocity jitter, m/s. */
    float speed = 1.f, jitter = 0.f;
    /** @brief Fluid3D randomVelocity blend between the shape direction and a seeded unit vector, in [0,1]. */
    float randomVelocity = 0.f;
    /** @brief Whether Distribution points replace prototype color, matching Fluid3DEmitter useShapeColor. */
    bool useShapeColor = true;
    /** @brief Maximum live particles owned by this emitter actor, matching Fluid3DEmitterBlueprintBase capacity. */
    unsigned actorCapacity = 1000;
    /** @brief Granular radius reduction percentage in [0,100]; zero preserves the prototype radius. */
    float granularRadiusRandomness = 0.f;
    /** @brief Material/color/data/lifetime copied to emitted particles; position/velocity are generated. */
    VolumeFluidParticle prototype;
    /** @brief Explicit named emission RNG stream seed; never touches a global RNG. */
    uint32_t seed = 1;
    /** @brief Precomputed shape points, used only by Distribution; maximum 4096, copied by value. */
    std::vector<VolumeFluidDistributionPoint> distribution;
};

/** @brief Builds one emitter distribution from an ordered set of Fluid3D emitter shapes.
 * @details `base` owns emitter-wide material, speed, lifetime, filter and RNG settings.
 * Each shape must be a Distribution description; its pose transforms its owned local
 * samples into solver space. An empty shape contributes its pose origin/direction, and
 * an empty shape list contributes the base pose, matching Fluid3DEmitter's default point behavior.
 * The setup-time operation accepts at most 64 shapes and 4096 total samples, publishes
 * no partial result, retains no input storage and adds no per-step allocation or GPU work.
 * Same ordered inputs produce the same ordered distribution on the same build.
 * Simulation-thread affinity applies when the returned value is later used by an emitter;
 * this pure composition call itself touches no solver and invokes no callbacks.
 * @param base Borrowed emitter-wide description, copied into the returned owning value.
 * @param shapes Borrowed ordered shape descriptions, valid only for this synchronous call.
 * @return Owning combined Distribution description, or a structured validation diagnostic.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<VolumeFluidEmission> composeVolumeFluidEmitterShapes(const VolumeFluidEmission&           base,
                                                                          std::span<const VolumeFluidEmission> shapes);

/** @brief Fully owned native setup prepared from one Fluid3D-emitter blueprint. */
struct VolumeFluidEmitterBlueprintApplication3D {
    VolumeFluidSettings                settings;
    VolumeFluidEmission                emission;
    VolumeFluidEmitterBlueprintMetrics metrics;
};

/** @brief Atomically maps every Fluid3DEmitterBlueprint field to native 3D setup values.
 * @details Pure setup-time operation. The returned solver settings and emission description own
 * all data; no solver, callback, render resource or GPU work is created. The default shared-solver
 * capacity equals the actor blueprint capacity and may be increased before solver construction.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<VolumeFluidEmitterBlueprintApplication3D> prepareVolumeFluidEmitterBlueprint3D(
    const VolumeFluidEmitterBlueprint3D& blueprint);

/** @brief Atomically maps an Fluid3DGranularEmitterBlueprint to owned native 3D setup values.
 * @details Pure setup-time operation. Radius variation uses the existing seeded emission stream;
 * no global RNG, solver, render resource or GPU work is created by this call.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<VolumeFluidEmitterBlueprintApplication3D> prepareVolumeGranularEmitterBlueprint3D(
    const VolumeGranularEmitterBlueprint3D& blueprint);

/** @brief Generates at most 4096 particles and admits the entire burst atomically.
 * @details Simulation-thread only. Retains no solver/description pointers, invokes no callbacks.
 * Same seed/count/input produces repeatable results on the same build; float parity is tolerance-based.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<unsigned> emitVolumeFluidBurst(VolumeFluid& solver, const VolumeFluidEmission& emission,
                                                    unsigned count);

/** @brief Version-1 owning state for rate (kind 0) or jet (kind 1) controllers. */
struct VolumeFluidEmitterSnapshot {
    std::string schema  = "eve.volume-fluid-emitter-state";
    unsigned    version = 1;
    unsigned    kind    = 0;
    /** @brief Decimal double text preserves phase across float32 scripting VMs. */
    std::string phase    = "0";
    uint32_t    sequence = 0;
    bool        emitting = false;
};

/** @brief Version-1 owning atomic checkpoint for a solver, emission description and controller. */
struct VolumeFluidEmitterCheckpoint {
    std::string                schema  = "eve.volume-fluid-emitter-checkpoint";
    unsigned                   version = 1;
    VolumeFluidSnapshot        solver;
    VolumeFluidEmission        emission;
    VolumeFluidEmitterSnapshot controller;
};

/** @brief Owning world-space nozzle pose; rotation is a unit quaternion in XYZW order. */
struct VolumeFluidNozzlePose {
    glm::vec3 position{0.f};
    glm::vec4 rotation{0.f, 0.f, 0.f, 1.f};
};

/** @brief Bounded runtime stream controller with explicit snapshot/restore.
 * @details Owns its fractional credit and RNG sequence. Caller injects dt and nozzle samples.
 * No external reference is retained. Simulation-thread affine; no callbacks or reentrancy.
 * Reconstructing this object resets the stream; replay supplies identical calls from construction.
 */
class EVENGINE_API_DOMAINS VolumeFluidEmitter final {
public:
    /** @brief Emits one particle with Fluid3DEmitter's normalized intra-step position offset.
     * @param offset Fraction of the current step's prescribed emission travel, in [0,1].
     * @param dt Current simulation-step duration in [0,1/30].
     * @details Returns one when admitted and zero when the actor/shared pool is full. The actor scan
     * and capacity checks precede bounded one-particle construction. Failures preserve controller/solver.
     */
    [[nodiscard]] Result<unsigned> emitParticle(VolumeFluid& solver, const VolumeFluidEmission& emission, float offset,
                                                float dt);
    /** @brief Advances emission with at most maxPerStep particles; excess whole-particle credit is discarded.
     * @param minimumPoolFraction A stopped stream restarts only above this actor pool's free-capacity fraction.
     * @details dt is in [0,1/30], rate in [0,1000000], maxPerStep in [1,4096].
     * An emission speed of zero is an explicit disabled controller state and clears fractional credit.
     * Failures preserve solver and controller state. Capacity exhaustion is successful zero emission.
     */
    [[nodiscard]] Result<unsigned> advance(VolumeFluid& solver, const VolumeFluidEmission& emission, float dt,
                                           float rate, unsigned maxPerStep = 256, float minimumPoolFraction = 0.5f);
    /** @brief Advances Fluid3D's BURST emission lifecycle for one actor group.
     * @param count Complete burst size in [1,4096].
     * @param minimumPoolFraction Emission waits until the actor pool's free capacity is above this fraction.
     * @details The prototype actorGroup identifies the emitter actor. A burst is emitted
     * atomically only when that group has no live particles; it becomes eligible again
     * after all particles in the group expire or are killed. Insufficient capacity is a
     * successful zero result. Linear actor scan plus the bounded existing burst generator;
     * no retained solver reference, callbacks, or GPU work.
     */
    [[nodiscard]] Result<unsigned> advanceBurst(VolumeFluid& solver, const VolumeFluidEmission& emission,
                                                unsigned count, float minimumPoolFraction = 0.5f);
    /** @brief Returns the current Fluid3DEmitter isEmitting state in O(1), without snapshot allocation. */
    [[nodiscard]] bool isEmitting() const noexcept { return emitting_; }
    /** @brief Copies controller state; caller checkpoints solver/configuration at the same step boundary. */
    [[nodiscard]] VolumeFluidEmitterSnapshot snapshot() const;
    /** @brief Validates schema, kind and phase before atomic replacement; invokes no callbacks. */
    [[nodiscard]] Result<void> restore(const VolumeFluidEmitterSnapshot& state);

private:
    double   credit_   = 0.0;
    uint32_t sequence_ = 0;
    bool     emitting_ = false;
};

/** @brief Spacing-driven planar/distribution jet controller with independent, owned stream phase.
 * @details Simulation-thread affine, no callbacks or retained external references.
 * Recreating it resets phase unless restored. Call once AFTER the solver step with
 * that step's dt. New layers are advected from their intra-step emission time at
 * prescribed nozzle velocity. Existing particles are advanced only by the solver.
 */
class EVENGINE_API_DOMAINS VolumeFluidJetEmitter final {
public:
    /** @brief Emits using interpolated step-boundary rigid poses instead of description origin/direction.
     * @details Call after the solver step. Translation is linear, rotation follows
     * the shortest quaternion arc; pose values are borrowed for this call only.
     * inheritVelocity in [0,1] blends linear and angular nozzle surface velocity.
     * New particles are advected for their fractional age at emission velocity;
     * external forces for that fractional age are not integrated here.
     */
    [[nodiscard]] Result<unsigned> advanceMoving(VolumeFluid& solver, const VolumeFluidEmission& emission,
                                                 const VolumeFluidNozzlePose& begin, const VolumeFluidNozzlePose& end,
                                                 float dt, unsigned maxPerStep = 256, float minimumPoolFraction = 0.5f,
                                                 float inheritVelocity = 0.f);
    /** @brief Emits whole nozzle layers spaced by solver.spacing().
     * @details Supports edge, square, disk and owned precomputed distributions;
     * rejects analytic volume shapes. Distribution point colors replace the prototype when enabled. The work cap
     * must fit one complete layer. Excess whole layers are dropped, never queued.
     * Admission failures preserve solver and stream phase. dt is in [0,1/30].
     * A speed of zero emits nothing and clears partial travel, so resume cannot catch up disabled time.
     */
    [[nodiscard]] Result<unsigned> advance(VolumeFluid& solver, const VolumeFluidEmission& emission, float dt,
                                           unsigned maxPerStep = 256, float minimumPoolFraction = 0.5f);
    /** @brief Returns whether the jet currently owns an active emission phase, without allocation. */
    [[nodiscard]] bool isEmitting() const noexcept { return emitting_; }
    /** @brief Copies controller state; caller checkpoints solver/configuration at the same step boundary. */
    [[nodiscard]] VolumeFluidEmitterSnapshot snapshot() const;
    /** @brief Validates schema, kind and phase before atomic replacement; invokes no callbacks. */
    [[nodiscard]] Result<void> restore(const VolumeFluidEmitterSnapshot& state);

private:
    /** @brief Reused analytic layer scratch; distribution descriptions are borrowed directly per call. */
    std::vector<VolumeFluidDistributionPoint> layerPoints_;
    double                                    distance_ = 0.0;
    uint32_t                                  sequence_ = 0;
    bool                                      emitting_ = false;
};

/** @brief Captures one completed-step rate-emitter checkpoint without retaining references. */
[[nodiscard]] EVENGINE_API_DOMAINS VolumeFluidEmitterCheckpoint captureVolumeFluidEmitterCheckpoint(const VolumeFluid&         solver,
                                                                               const VolumeFluidEmission& emission,
                                                                               const VolumeFluidEmitter&  controller);
/** @brief Captures one completed-step moving-jet checkpoint without retaining references. */
[[nodiscard]] EVENGINE_API_DOMAINS VolumeFluidEmitterCheckpoint captureVolumeFluidEmitterCheckpoint(const VolumeFluid&           solver,
                                                                               const VolumeFluidEmission&   emission,
                                                                               const VolumeFluidJetEmitter& controller);
/** @brief Atomically restores solver and rate controller after validating the complete owning candidate.
 * @details Simulation-thread only. Schema/version, solver state, emission description and
 * controller kind/state are preflighted before either destination changes. No callbacks,
 * external references or GPU work are involved. The emission description remains owned by
 * the checkpoint and is returned to script callers by the binding layer.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<void> restoreVolumeFluidEmitterCheckpoint(VolumeFluid& solver, VolumeFluidEmitter& controller,
                                                                               const VolumeFluidEmitterCheckpoint& checkpoint);
/** @brief Atomically restores solver and moving-jet controller under the same checkpoint contract. */
[[nodiscard]] EVENGINE_API_DOMAINS Result<void> restoreVolumeFluidEmitterCheckpoint(VolumeFluid& solver, VolumeFluidJetEmitter& controller,
                                                                               const VolumeFluidEmitterCheckpoint& checkpoint);

}  // namespace eve::fluids
