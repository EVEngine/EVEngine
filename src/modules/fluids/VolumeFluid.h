#pragma once

#include "common/Result.h"
#include "fluids/FluidSdf.h"

#include <array>
#include <cstdint>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace eve::fluids {

/** @brief Constitutive phase; solids are world-fixed or follow their owned collider attachment. */
enum class VolumeFluidPhase : uint8_t { Liquid, Gas, Granular, Solid };

/** @brief Fluid3D collision-material coefficient combination, with package integer values. */
enum class VolumeFluidMaterialCombineMode : uint8_t { Average = 0, Minimum = 1, Multiply = 2, Maximum = 3 };

/** @brief Per-particle material, copied at emission; densities enable multiphase buoyancy. */
struct VolumeFluidMaterial {
    /** @brief Rest density in kg/m3, in [0.1,100000]. */
    float density = 1000.f;
    /** @brief SPH support-radius multiplier over solver spacing, in [1,4]. */
    float smoothing = 2.f;
    /** @brief Viscous relaxation per second, in [0,100]. */
    float viscosity = 2.f;
    /** @brief Cohesion acceleration, in [0,10]. */
    float cohesion = 0.2f;
    /** @brief Gravity multiplier; negative values model rising gas. */
    float buoyancy = 1.f;
    /** @brief Linear air drag per second. */
    float drag = 0.f;
    /** @brief Ambient pressure acceleration along the inward SPH surface normal, in [0,1000]. */
    float atmosphericPressure = 0.f;
    /** @brief Color and user-data diffusion per second. */
    float diffusion = 0.f;
    /** @brief Bingham yield rate; zero disables yield viscosity. */
    float yield = 0.f;
    /** @brief Vorticity confinement strength. */
    float vorticity = 0.f;
    /** @brief Dynamic particle-contact friction coefficient in [0,1]. */
    float dynamicFriction = 0.2f;
    /** @brief Static particle-contact friction coefficient in [0,1]. */
    float staticFriction = 0.2f;
    /** @brief Rolling-friction coefficient in [0,1]. */
    float rollingFriction = 0.f;
    /** @brief Enables angular contact velocity and rolling response for granular particles. */
    bool rollingContacts = false;
    /** @brief Adhesion strength in [0,1000]. */
    float stickiness = 0.f;
    /** @brief Adhesion reach beyond contact in metres, in [0,10]. */
    float stickDistance = 0.f;
    /** @brief Combination mode for static, dynamic and rolling friction. */
    VolumeFluidMaterialCombineMode frictionCombine = VolumeFluidMaterialCombineMode::Average;
    /** @brief Combination mode for stickiness. */
    VolumeFluidMaterialCombineMode stickinessCombine = VolumeFluidMaterialCombineMode::Average;
    /** @brief Constitutive phase. */
    VolumeFluidPhase phase = VolumeFluidPhase::Liquid;
};

/** @brief Transient linear RGBA gradient key at an absolute material viscosity. */
struct VolumeFluidViscosityColorKey {
    /** @brief Viscosity coordinate in [0,100]; keys must be strictly increasing. */
    float viscosity = 0.f;
    /** @brief Linear RGBA components in [0,1]. */
    glm::vec4 color{1.f};
};

/** @brief One strictly ordered [0,1] coordinate and linear RGBA gradient color. */
struct VolumeFluidColorKey {
    float     coordinate = 0.f;
    glm::vec4 color{1.f};
};

/** @brief Meter-space (+Y up) volume-fluid material and solver policy. */
struct VolumeFluidSettings {
    /** @brief Maximum live particles; admission fails atomically at capacity. */
    unsigned capacity = 8192;
    /** @brief Rest lattice separation in meters. */
    float spacing = 0.1f;
    /** @brief Applied acceleration, in meters per second squared. */
    glm::vec3 gravity{0.f, -9.81f, 0.f};
    /** @brief Inner container corners. Particle centers retain half-spacing clearance. */
    glm::vec3 minimum{-2.f, 0.f, -1.f}, maximum{2.f, 3.f, 1.f};
    /** @brief Density projection iterations per substep, in [1,20]. */
    unsigned iterations = 5;
};

/** @brief Owning particle value with world position and velocity in SI units. */
struct VolumeFluidParticle {
    glm::vec3 position{0.f};
    glm::vec3 velocity{0.f};
    /** @brief World angular velocity in radians/second; integrated for granular particles. */
    glm::vec3 angularVelocity{0.f};
    /** @brief Principal collision/query radii in meters; all zero resolves to spacing/2 on emission. */
    glm::vec3 radii{0.f};
    /** @brief Unit particle-shape orientation in XYZW order. */
    glm::vec4 orientation{0.f, 0.f, 0.f, 1.f};
    /** @brief Owned material parameters. */
    VolumeFluidMaterial material;
    /** @brief Linear RGBA color and four diffusing application channels. */
    glm::vec4 color{0.1f, 0.5f, 0.9f, 1.f}, data{0.f};
    /** @brief Remaining lifetime in seconds; zero means unlimited. */
    float life = 0.f;
    /** @brief Query collision filter: low 16 bits category, high 16 bits mask. */
    unsigned collisionFilter = 0xffff0001u;
    /** @brief Fluid3D-compatible actor group in [0, 0x00ffffff]; equal groups use selfCollide instead of filters. */
    unsigned actorGroup = 0;
    /** @brief Allows interaction with particles in the same actor group. */
    bool selfCollide = true;
};

/** @brief Shape used by a transient Fluid3D-style wind force zone. */
enum class VolumeFluidWindZoneType { Ambient, Spherical };

/** @brief Owning description of one transient ambient or spherical wind zone. */
struct VolumeFluidWindZone {
    VolumeFluidWindZoneType type = VolumeFluidWindZoneType::Ambient;
    glm::vec3               center{0.f};
    glm::vec3               direction{0.f, 0.f, 1.f};
    float                   intensity           = 0.f;
    float                   turbulence          = 0.f;
    float                   turbulenceFrequency = 1.f;
    float                   turbulenceSeed      = 0.f;
    float                   radius              = 5.f;
    bool                    radial              = true;
};

/** @brief Analytic obstacle geometry. */
enum class VolumeFluidColliderShape { Sphere, Box, Capsule };

/** @brief Owned obstacle sample; the caller updates moving obstacles before stepping. */
struct VolumeFluidCollider {
    /** @brief Application label echoed by contacts; not a runtime object handle. */
    unsigned label = 0;
    /** @brief Analytic primitive. */
    VolumeFluidColliderShape shape = VolumeFluidColliderShape::Sphere;
    /** @brief World center and box half extents; capsule halfExtent.y is half its total height. */
    glm::vec3 center{0.f, 1.f, 0.f}, halfExtent{0.25f};
    /** @brief Sphere or capsule radius. */
    float radius = 0.25f;
    /** @brief Surface velocity at the sampled center. */
    glm::vec3 velocity{0.f};
    /** @brief Coulomb friction in [0,1]. */
    float friction = 0.2f;
    /** @brief Static Coulomb friction in [0,1]; friction is the dynamic coefficient. */
    float staticFriction = 0.2f;
    /** @brief Rolling-friction coefficient in [0,1]. */
    float rollingFriction = 0.f;
    /** @brief Adhesion strength in [0,1000]. */
    float stickiness = 0.f;
    /** @brief Maximum adhesive surface separation in meters, in [0,10]. */
    float stickDistance = 0.f;
    /** @brief Fluid3D-compatible priority combine mode for all friction coefficients. */
    VolumeFluidMaterialCombineMode frictionCombine = VolumeFluidMaterialCombineMode::Average;
    /** @brief Fluid3D-compatible priority combine mode for stickiness. */
    VolumeFluidMaterialCombineMode stickinessCombine = VolumeFluidMaterialCombineMode::Average;
    /** @brief Enables oriented rolling contact when either side requests it. */
    bool rollingContacts = false;
    /** @brief Freezes contacts and enables one neighbor-contact wave per step from its attached solids. */
    bool solidify = false;
    /** @brief Unit quaternion XYZW; box extents and the capsule's Y axis use this local frame. */
    glm::vec4 rotation{0.f, 0.f, 0.f, 1.f};
    /** @brief World angular velocity in radians/second, sampled at the collider center. */
    glm::vec3 angularVelocity{0.f};
    /** @brief Collision filter: low 16 bits category, high 16 bits mask. */
    unsigned collisionFilter = 0xffff0001u;
    /** @brief Generates contacts without applying projection, friction or material transfer. */
    bool isTrigger = false;
};

/** @brief Owned sampled mesh/SDF collider with a uniform world transform. */
struct VolumeFluidSdfCollider {
    unsigned label = 0;
    /** @brief Local signed field; negative values represent mesh solid. */
    MeshSdf   sdf;
    glm::vec3 position{0.f};
    glm::vec4 rotation{0.f, 0.f, 0.f, 1.f};
    float     scale = 1.f;
    /** @brief When true, positive mesh exterior is solid and particles remain inside. */
    bool                           inverted = false;
    glm::vec3                      velocity{0.f};
    glm::vec3                      angularVelocity{0.f};
    float                          friction          = 0.2f;
    float                          staticFriction    = 0.2f;
    float                          rollingFriction   = 0.f;
    float                          stickiness        = 0.f;
    float                          stickDistance     = 0.f;
    VolumeFluidMaterialCombineMode frictionCombine   = VolumeFluidMaterialCombineMode::Average;
    VolumeFluidMaterialCombineMode stickinessCombine = VolumeFluidMaterialCombineMode::Average;
    bool                           rollingContacts   = false;
    unsigned                       collisionFilter   = 0xffff0001u;
    /** @brief Generates contacts without applying projection, friction or material transfer. */
    bool isTrigger = false;
};

/** @brief Owned regular-grid terrain collider matching Fluid3D height-field sample layout. */
struct VolumeFluidHeightFieldCollider {
    unsigned label = 0;
    /** @brief Row-major samples indexed as z * resolution.x + x, in normalized [0,1] height units. */
    std::vector<float> heights;
    /** @brief Grid vertex counts along local X and Z. */
    glm::ivec2 resolution{2, 2};
    /** @brief Local terrain size; sample height is multiplied by size.y. */
    glm::vec3 size{1.f};
    glm::vec3 position{0.f};
    /** @brief Unit local-to-world quaternion in XYZW order. */
    glm::vec4                      rotation{0.f, 0.f, 0.f, 1.f};
    glm::vec3                      velocity{0.f};
    glm::vec3                      angularVelocity{0.f};
    float                          friction          = 0.2f;
    float                          staticFriction    = 0.2f;
    float                          rollingFriction   = 0.f;
    float                          stickiness        = 0.f;
    float                          stickDistance     = 0.f;
    VolumeFluidMaterialCombineMode frictionCombine   = VolumeFluidMaterialCombineMode::Average;
    VolumeFluidMaterialCombineMode stickinessCombine = VolumeFluidMaterialCombineMode::Average;
    bool                           rollingContacts   = false;
    unsigned                       collisionFilter   = 0xffff0001u;
    /** @brief Generates contacts without applying projection, friction or material transfer. */
    bool isTrigger = false;
};

/** @brief Transient transform update for one owned SDF collider, identified by stable label. */
struct VolumeFluidSdfPose {
    unsigned  label = 0;
    glm::vec3 position{0.f};
    glm::vec4 rotation{0.f, 0.f, 0.f, 1.f};
    float     scale = 1.f;
    glm::vec3 velocity{0.f};
    glm::vec3 angularVelocity{0.f};
};

/** @brief Local axis normal used by an explicit SDF diagnostic slice. */
enum class VolumeFluidSdfSliceAxis { X = 0, Y = 1, Z = 2 };

/** @brief Owning normalized SDF slice plus its world-space sample lattice. */
struct VolumeFluidSdfSlice {
    unsigned width = 0, height = 0;
    /** @brief World position of sample (0,0). */
    glm::vec3 origin{0.f};
    /** @brief World displacement from one sample to the next along image X/Y. */
    glm::vec3 stepX{0.f}, stepY{0.f};
    /** @brief Row-major values remapped from [-maxDistance,+maxDistance] to [0,1]. */
    std::vector<float> values;
};

/** @brief Owned contact event; opposite impulse can be applied to the caller's rigid body. */
struct VolumeFluidContact {
    unsigned  colliderLabel = 0;
    glm::vec3 point{0.f}, normal{0.f}, impulse{0.f};
    /** @brief Current dense particle index; stale after any particle mutation. */
    unsigned particleIndex = 0;
    /** @brief Stable actor group copied from the contacting particle. */
    unsigned actorGroup = 0;
    /** @brief Signed pre-projection surface separation in meters. */
    float distance = 0.f;
};

/** @brief Contact lifecycle classification matching Fluid3DContactEventDispatcher. */
enum class VolumeFluidContactEventType : uint8_t { Enter, Stay, Exit };

/** @brief Owning deduplicated actor/collider contact lifecycle event. */
struct VolumeFluidContactEvent {
    VolumeFluidContactEventType type = VolumeFluidContactEventType::Enter;
    VolumeFluidContact          contact;
};

/** @brief Explicit bounded contact lifecycle tracker; owns no solver reference. */
class VolumeFluidContactTracker final {
public:
    /** @brief Diffs latest contacts against the preceding successful call.
     * @param contacts Borrowed latest-step contacts; at most 65536.
     * @param distanceThreshold Finite accepted separation in [0,1] meters.
     * @return Owning Enter/Stay/Exit events sorted by collider label then actor group.
     * @details Contacts are deduplicated by collider/actor like Fluid3D. Invalid input
     * leaves prior state unchanged. No callbacks, locks or GPU work are performed.
     */
    [[nodiscard]] Result<std::vector<VolumeFluidContactEvent>> advance(std::span<const VolumeFluidContact> contacts,
                                                                       float distanceThreshold = .01f);
    /** @brief Clears prior keys so the next contacts enter again. */
    void reset();

private:
    std::vector<VolumeFluidContact> previous_;
    std::vector<VolumeFluidContact> scratch_;
};

/** @brief Opposite impulse produced by a prescribed solid-particle attachment constraint. */
struct VolumeFluidAttachmentReaction {
    /** @brief Collider identity that owns the attachment. */
    unsigned colliderLabel = 0;
    /** @brief World-space attachment point at the completed pose. */
    glm::vec3 point{0.f};
    /** @brief World-space impulse to apply to the owning rigid body. */
    glm::vec3 impulse{0.f};
    /** @brief World-space pure angular impulse from prescribed particle rotation. */
    glm::vec3 angularImpulse{0.f};
};

/** @brief One-step contact transfer into user channels x/y; negative rate heats, positive cools. */
struct VolumeFluidThermalRule {
    unsigned colliderLabel    = 0;
    float    rate             = 0.f;
    float    minimumViscosity = 0.05f, maximumViscosity = 10.f;
    float    minimumCohesion = 0.5f, maximumCohesion = 2.f;
};

/** @brief Owned liquid/gas field sample for diffuse-particle emission and advection. */
struct VolumeFluidFieldSample {
    /** @brief Poly6-weighted velocity in meters/second; zero without support. */
    glm::vec3 velocity{0.f};
    /** @brief Curl of the normalized poly6 velocity interpolation, in inverse seconds. */
    glm::vec3 vorticity{0.f};
    /** @brief SPH mass density in kg/m3; no container boundary-support correction. */
    float density = 0.f;
    /** @brief Liquid/gas samples strictly within twice the particle spacing. */
    unsigned neighborCount = 0;
};

/** @brief Owning occupied-cell sample for Fluid3DParticleGridDebugger-style visualization. */
struct VolumeFluidGridCell {
    /** @brief World-space center of the solver's uniform hash cell. */
    glm::vec3 center{0.f};
    /** @brief World-space full size of the cell. */
    glm::vec3 size{0.f};
    /** @brief Number of current particles hashed into this cell. */
    unsigned particleCount = 0;
};

/** @brief Owning local-axis sample for DebugParticleFrames-style visualization. */
struct VolumeFluidParticleFrame {
    /** @brief Current dense particle index; invalidated by particle-pool mutation. */
    unsigned particleIndex = 0;
    /** @brief World-space ray origin. */
    glm::vec3 origin{0.f};
    /** @brief World-space endpoints for the red, green and blue local-axis rays. */
    glm::vec3 x{0.f}, y{0.f}, z{0.f};
};

/** @brief One owning mesh-instance transform/color row for Fluid3DInstancedParticleRenderer-style presentation. */
struct VolumeFluidParticleInstance {
    /** @brief Current dense particle index; invalidated by particle-pool mutation. */
    unsigned  particleIndex = 0;
    glm::vec3 position{0.f};
    /** @brief Unit particle orientation in XYZW order. */
    glm::vec4 orientation{0.f, 0.f, 0.f, 1.f};
    /** @brief Principal particle radii multiplied component-wise by the requested instance scale. */
    glm::vec3 scale{1.f};
    glm::vec4 color{1.f};
};

/** @brief Lifecycle kind matching Fluid3DEmitter OnEmitParticle/OnKillParticle. */
enum class VolumeFluidParticleEventType : uint8_t { Emitted, Killed };

/** @brief Owning particle lifecycle event captured before the dense pool changes again. */
struct VolumeFluidParticleEvent {
    VolumeFluidParticleEventType type = VolumeFluidParticleEventType::Emitted;
    /** @brief Dense index at event time; stale after any later particle mutation. */
    unsigned particleIndex = 0;
    /** @brief Stable emitter/actor group copied from the particle. */
    unsigned actorGroup = 0;
    /** @brief Owning particle value at emission or immediately before removal. */
    VolumeFluidParticle particle;
};

/** @brief Owning result of one explicit particle lifecycle-event drain. */
struct VolumeFluidParticleEventBatch {
    std::vector<VolumeFluidParticleEvent> events;
    /** @brief Events discarded since the preceding drain because the configured queue was full. */
    uint64_t dropped = 0;
};

/** @brief Owning mass summary for one Fluid3D-compatible actor group. */
struct VolumeFluidMassProperties {
    /** @brief Sum of particle rest masses in kilograms. */
    float mass = 0.f;
    /** @brief Rest-mass-weighted world-space center. */
    glm::vec3 centerOfMass{0.f};
    /** @brief Number of particles contributing to the summary. */
    unsigned particleCount = 0;
};

/** @brief Owning ray hit against one native oriented-ellipsoid volume-fluid particle. */
struct VolumeFluidRayHit {
    /** @brief Current pool index; valid only until the next particle mutation. */
    unsigned particleIndex = 0;
    /** @brief World-space distance along the normalized ray, in meters. */
    float     distance = 0.f;
    glm::vec3 point{0.f};
    glm::vec3 normal{0.f, 1.f, 0.f};
    /** @brief Owning particle value captured at query time. */
    VolumeFluidParticle particle;
};

/** @brief Owning signed surface-distance result for a native query shape. */
struct VolumeFluidDistanceHit {
    /** @brief Current pool index; valid only until the next particle mutation. */
    unsigned particleIndex = 0;
    /** @brief Signed separation; negative values mean surface overlap. */
    float distance = 0.f;
    /** @brief Nearest point on the query surface including its contact offset. */
    glm::vec3 queryPoint{0.f};
    /** @brief Direction from the query shape toward the particle center. */
    glm::vec3 normal{0.f, 1.f, 0.f};
    /** @brief Owning particle value captured at query time. */
    VolumeFluidParticle particle;
};

/** @brief Shape kind for one bounded batched particle query. */
enum class VolumeFluidQueryType : unsigned { Sphere = 0, Box = 1, Ray = 2 };

/** @brief Owning transient query description compatible with Fluid3D QueryShape semantics. */
struct VolumeFluidQueryShape {
    VolumeFluidQueryType type = VolumeFluidQueryType::Sphere;
    /** @brief Sphere/box center or ray segment start. */
    glm::vec3 center{0.f};
    /** @brief Sphere radius in x, full box size, or ray segment end. */
    glm::vec3 size{0.f};
    /** @brief Unit XYZW box rotation; ignored by sphere and ray queries. */
    glm::vec4 rotation{0.f, 0.f, 0.f, 1.f};
    float     contactOffset = 0.f;
    float     maxDistance   = 0.f;
    unsigned  phaseMask     = 0x0f;
    /** @brief Low 16 bits category and high 16 bits mask; both sides must match. */
    unsigned collisionFilter = 0xffff0001u;
};

/** @brief Owning result from one entry in a mixed query batch. */
struct VolumeFluidQueryHit {
    unsigned            queryIndex    = 0;
    unsigned            particleIndex = 0;
    float               distance      = 0.f;
    glm::vec3           queryPoint{0.f};
    glm::vec3           normal{0.f, 1.f, 0.f};
    VolumeFluidParticle particle;
};

/** @brief A point, edge or triangle over snapshot-local particle indices. */
struct VolumeFluidSimplex {
    /** @brief First size entries are used; size must be in [1,3] and entries must be distinct. */
    std::array<unsigned, 3> particleIndices{0, 0, 0};
    unsigned                size = 1;
};

/** @brief Owning Fluid3D-compatible nearest result for one simplex and query pair. */
struct VolumeFluidSimplexHit {
    glm::vec4 simplexBary{1.f, 0.f, 0.f, 0.f};
    glm::vec3 queryPoint{0.f};
    glm::vec3 normal{0.f, 1.f, 0.f};
    float     distance = 0.f;
    /** @brief Current topology index; stale after topology or particle mutation. */
    unsigned simplexIndex = 0;
    unsigned queryIndex   = 0;
};

/** @brief Owned local anchor for a contact-frozen particle; indices refer to its snapshot particle array. */
struct VolumeFluidAttachment {
    /** @brief Snapshot-local particle index; remapped on lifetime removal, never a persistent entity ID. */
    unsigned particleIndex = 0;
    /** @brief Application sample identity; removal detaches permanently, including after label reuse. */
    unsigned colliderLabel = 0;
    /** @brief Point in the collider's rigid local frame; no external pointer is retained. */
    glm::vec3 localPosition{0.f};
    /** @brief Particle-shape orientation in the collider's local frame, XYZW. */
    glm::vec4 localOrientation{0.f, 0.f, 0.f, 1.f};
    /** @brief Whether the target rotation also drives the particle orientation. */
    bool constrainOrientation = true;
    /** @brief Dynamic attachments use a compliant pin instead of a fixed inverse weight. */
    bool dynamic = false;
    /** @brief XPBD compliance in inverse stiffness units; zero is rigid. */
    float compliance = 0.f;
    /** @brief Approximate constraint-force threshold in newtons; finite and positive. */
    float breakThreshold = 1e12f;
};

/** @brief One Fluid3DStitcher-compatible zero-distance particle-pair constraint. */
struct VolumeFluidStitch {
    /** @brief Snapshot-local distinct particle indices. */
    unsigned particleIndex1 = 0, particleIndex2 = 0;
    /** @brief XPBD compliance in inverse stiffness units; zero is rigid. */
    float compliance = 0.f;
};

/** @brief Canonical version-20 owning state; older codecs migrate collider, attachment, stitch, filter, group,
 * material, shape, topology, height-field, trigger and collision-material fields. */
struct VolumeFluidSnapshot {
    std::string                                 schema  = "eve.volume-fluid";
    unsigned                                    version = 20;
    VolumeFluidSettings                         settings;
    std::vector<VolumeFluidParticle>            particles;
    std::vector<VolumeFluidCollider>            colliders;
    std::vector<VolumeFluidSdfCollider>         sdfColliders;
    std::vector<VolumeFluidHeightFieldCollider> heightFieldColliders;
    std::vector<VolumeFluidAttachment>          attachments;
    std::vector<VolumeFluidStitch>              stitches;
    /** @brief Explicit point/edge/triangle topology; empty means one implicit point simplex per particle. */
    std::vector<VolumeFluidSimplex> simplexes;
};

/**
 * @brief CPU position-based free-volume fluid with a bounded spatial grid.
 * @ownership Owns all particle state; no external objects or callbacks are retained.
 * @thread All methods are simulation-thread affine; concurrent access is forbidden.
 * @reentrancy No callbacks are invoked. Renderers consume copied particle positions.
 * @details Fixed input ordering and timesteps are repeatable on the same build;
 * cross-platform float results are tolerance-bounded, not bit-exact. No RNG is used.
 * Liquid/gas sampling uses a half-spacing separation regularizer to resolve
 * coincident zero-gradient particles; granular contacts use full spacing.
 */
class VolumeFluid final {
public:
    /** @brief Validates settings before allocating an owning solver; errors publish no state. */
    [[nodiscard]] static Result<std::unique_ptr<VolumeFluid>> create(const VolumeFluidSettings& settings);
    ~VolumeFluid();
    VolumeFluid(const VolumeFluid&)            = delete;
    VolumeFluid& operator=(const VolumeFluid&) = delete;

    /** @brief Atomically admits particles; rejects nonfinite, out-of-container or over-capacity input. */
    [[nodiscard]] Result<void> emit(std::span<const VolumeFluidParticle> particles);
    /** @brief Enables a bounded Fluid3DEmitter lifecycle-event queue or disables it with zero capacity.
     * @param capacity Maximum pending events in [0,65536].
     * @details Simulation-thread only. Configuration clears pending events and the
     * overflow counter, and reserves storage before publishing the new capacity.
     * Disabled emission/removal adds no allocations, callbacks, locks or GPU work.
     */
    [[nodiscard]] Result<void> configureParticleEvents(unsigned capacity);
    /** @brief Drains owning emission/removal snapshots and the overflow count.
     * @details Simulation-thread only. The explicit drain allocates its returned owning
     * vector; the configured hot path retains its reservation and never invokes scripts.
     */
    [[nodiscard]] VolumeFluidParticleEventBatch drainParticleEvents();
    /**
     * @brief Advances by an injected duration in (0,1/30] seconds with 1..32 substeps.
     * @details Rejects invalid input before mutation. Each substep must be <=1/120 s.
     * Numerical failure restores the complete pre-step particle state.
     * Analytic/SDF colliders use their sampled linear and angular velocities to
     * back-extrapolate each substep pose without copying SDF samples. Attached solids
     * follow the latest collider pose with linear substep interpolation. Out-of-bounds
     * targets, linear speeds above 100 m/s or angular speeds above 1000 rad/s fail atomically.
     * After substeps, attached solids on solidifying colliders propagate to touching
     * movable particles within 1.001 times spacing. A wave's new solids do not seed
     * until the next call; competing sources choose the smallest collider label.
     */
    [[nodiscard]] Result<void> step(float seconds, unsigned substeps = 4);
    /** @brief Replaces one-step per-particle wind velocities used by atmospheric drag.
     * @details Simulation-thread only. A nonempty borrowed span must match the current
     * particle count and contain finite velocities of at most 1000 m/s. Values are
     * copied, survive a failed step, and clear after the next successful whole step.
     * An empty span clears pending wind. Wind is transient and absent from snapshots.
     */
    [[nodiscard]] Result<void> setParticleWinds(std::span<const glm::vec3> winds);
    /** @brief Replaces one-step world-space forces in newtons, one per current particle.
     * @details A nonempty span must match the dense pool and contain finite vectors of at most
     * 1000 N. Forces survive failed steps, clear after a successful whole step, and are absent
     * from snapshots. The inactive path is compile-time specialized out of particle integration.
     */
    [[nodiscard]] Result<void> setParticleExternalForces(std::span<const glm::vec3> forces);
    /** @brief Accumulates a bounded batch of Fluid3D-style force zones into pending wind.
     * @param zones Borrowed ambient/spherical zones; at most 64, validated before mutation.
     * @param fixedTimeSeconds Injected non-negative fixed simulation time for turbulence.
     * @details Simulation-thread only. Ambient forces are precombined and spherical zones
     * share one particle traversal, capped at four million particle-zone candidates.
     * Turbulence multiplies deterministic coherent noise in [0,1]. Results add to
     * pending setParticleWinds values, survive failure, and clear after a successful step.
     * Empty input is a no-op. No zone or callback is retained and snapshots omit the result.
     */
    [[nodiscard]] Result<void> accumulateWindZones(std::span<const VolumeFluidWindZone> zones, float fixedTimeSeconds);
    /** @brief Accumulates Fluid3D external-force zones into the pending one-step force array.
     * @details Geometry, turbulence and work budgets match accumulateWindZones(), while values
     * are interpreted as newtons and converted by each particle's inverse rest mass during step.
     */
    [[nodiscard]] Result<void> accumulateExternalForceZones(std::span<const VolumeFluidWindZone> zones,
                                                            float                                fixedTimeSeconds);
    /** @brief Advances and applies bounded contact transfer to user channels, once per particle/rule.
     * @details Simulation-thread only; at most 1024 uniquely labelled rules referring to
     * current colliders. Rates must be finite in [-1000000,1000000]. Bounds lie within
     * viscosity [0,100] and cohesion [0,10]. Rules are borrowed only for this call;
     * no callbacks or persistent bindings are created. Transfer precedes lifetime removal.
     * Call applyMaterialChannels explicitly to update materials for the next step.
     * @return Invalid rules/dt fail before mutation; numerical failure restores state.
     */
    [[nodiscard]] Result<void> stepWithThermalContacts(float seconds, unsigned substeps,
                                                       std::span<const VolumeFluidThermalRule> rules);
    /** @brief Atomically updates collider samples and advances; failed steps restore prior samples and particles. */
    [[nodiscard]] Result<void> stepWithColliders(float seconds, unsigned substeps,
                                                 std::span<const VolumeFluidCollider>    colliders,
                                                 std::span<const VolumeFluidThermalRule> rules = {});
    /** @brief Returns an owning snapshot, independent of subsequent mutation or destruction. */
    [[nodiscard]] std::vector<VolumeFluidParticle> particles() const;
    /** @brief Borrows the current dense particle pool without copying it.
     * @return Read-only contiguous view in current pool order.
     * @ownership The solver owns every element; callers must not retain the view.
     * @lifetime Valid only until the next non-const solver call or solver destruction.
     * @thread Simulation-thread only; no concurrent mutation or callbacks.
     * @details Constant-time and allocation-free. Intended for bounded native hot-path scans;
     * scripting continues to receive owning snapshots.
     */
    [[nodiscard]] std::span<const VolumeFluidParticle> particleView() const;
    /** @brief Returns the live count without allocating or copying particle state. */
    [[nodiscard]] size_t particleCount() const;
    /** @brief Returns unused pool capacity in constant time without allocating. */
    [[nodiscard]] size_t availableCapacity() const;
    /** @brief Returns the rest lattice spacing without copying state. */
    [[nodiscard]] float spacing() const;
    /** @brief Replaces world-space gravity for subsequent fixed steps.
     * @param gravity Finite acceleration with magnitude at most 1000 m/s^2.
     * @return Success, or invalid argument without changing solver settings.
     * @details Native particles already use world-space coordinates, so this is the
     * package WorldSpaceGravity behavior without retaining a Transform. Constant-time,
     * simulation-thread affine, and adds no particle traversal, callback or GPU work.
     */
    [[nodiscard]] Result<void> setGravity(glm::vec3 gravity);
    /** @brief Returns current world-space gravity in constant time. */
    [[nodiscard]] glm::vec3 gravity() const;
    /** @brief Copies positions/colors into caller-owned reusable buffers and returns rest spacing.
     * @details Simulation-thread only; retains no references to either output buffer.
     */
    [[nodiscard]] float copyRenderData(std::vector<glm::vec3>& positions, std::vector<glm::vec4>& colors) const;
    /** @brief Copies presentation positions interpolated between the last successful fixed-step endpoints.
     * @param alpha Finite fraction in [0,1]; zero selects the step start and one the current state.
     * @param positions Destination resized to the live count while retaining existing capacity.
     * @return Success, or invalid argument without changing the destination.
     * @details Previous positions are transient derived state. Emission and restore initialize them,
     * removal compacts them with particles, teleport resets them, and a failed step preserves them.
     * Simulation-thread affine; no callbacks or GPU work.
     */
    [[nodiscard]] Result<void> copyInterpolatedPositions(float alpha, std::vector<glm::vec3>& positions) const;
    /** @brief Fuses interpolated positions and colors into one allocation-reusing renderer pass. */
    [[nodiscard]] Result<float> copyInterpolatedRenderData(float alpha, std::vector<glm::vec3>& positions,
                                                           std::vector<glm::vec4>& colors) const;
    /**
     * @brief Copies position, color and oriented-ellipsoid shape data in one renderer-facing pass.
     * @param positions Owning destination resized to the current particle count.
     * @param colors Owning destination resized to the current particle count.
     * @param radii Principal world-space ellipsoid radii, parallel to positions.
     * @param orientations Unit particle orientations in XYZW order, parallel to positions.
     * @return Solver spacing in metres.
     * @details Read-only and allocation-reusing: existing destination capacity is retained. The
     * four arrays are a coherent snapshot from this call and no simulator pointer is retained.
     * Render-thread affinity is inherited from the caller; do not race this method with step/emit.
     */
    [[nodiscard]] float copySurfaceRenderData(std::vector<glm::vec3>& positions, std::vector<glm::vec4>& colors,
                                              std::vector<glm::vec3>& radii,
                                              std::vector<glm::vec4>& orientations) const;
    /** @brief Fuses interpolated positions, colors and ellipsoid shape into one renderer pass.
     * @param alpha Finite fixed-step interpolation fraction in [0,1].
     * @return Solver spacing, or invalid argument before changing any destination.
     * @details All outputs retain capacity; no state, callback or GPU work is involved.
     */
    [[nodiscard]] Result<float> copyInterpolatedSurfaceRenderData(float alpha, std::vector<glm::vec3>& positions,
                                                                  std::vector<glm::vec4>& colors,
                                                                  std::vector<glm::vec3>& radii,
                                                                  std::vector<glm::vec4>& orientations) const;
    /**
     * @brief Copies a bounded prefix of one material phase for render-only consumers.
     * @param phase Material phase to select.
     * @param positions Replaced with up to maxParticles selected positions.
     * @param colors Replaced with the corresponding particle colors.
     * @param maxParticles Maximum number of particles copied.
     * @return Total selected particle count, including particles beyond maxParticles.
     * @details Read-only and simulation-thread affine. Output vectors are owned by the caller.
     */
    [[nodiscard]] size_t copyPhaseRenderData(VolumeFluidPhase phase, std::vector<glm::vec3>& positions,
                                             std::vector<glm::vec4>& colors, size_t maxParticles) const;
    /** @brief Atomically replaces owned obstacle samples; rejects invalid geometry and duplicate labels.
     * @details Collider labels identify successive poses. Removing a label detaches its
     * solids at their current position; later label reuse does not reattach them.
     * Changing a pose takes effect on the next step. Turning solidify off affects
     * future contacts only; paint to a non-solid phase explicitly melts and detaches.
     * At most four million live particle/collider candidates are accepted.
     */
    [[nodiscard]] Result<void> setColliders(std::span<const VolumeFluidCollider> colliders);
    /** @brief Atomically replaces at most 16 owned mesh/SDF colliders; total samples are bounded to 4M. */
    [[nodiscard]] Result<void> setSdfColliders(std::span<const VolumeFluidSdfCollider> colliders);
    /**
     * @brief Atomically replaces owned Fluid3D-compatible regular height-field colliders.
     * @param colliders Borrowed descriptions copied before publication; each grid is row-major X-fastest.
     * @return Success, or a structured validation error without changing current colliders.
     * @details At most 16 grids and four million total samples are accepted. Each particle samples only
     * its local cell's two triangles, so work is linear in particle-count times height-field-count.
     * Simulation-thread only; no callback or external pointer is retained.
     */
    [[nodiscard]] Result<void> setHeightFieldColliders(std::span<const VolumeFluidHeightFieldCollider> colliders);
    /**
     * @brief Binds a particle group to an existing analytic collider in its current local frame.
     * @param particleIndices Borrowed current pool indices; copied into sparse owned anchors.
     * @param colliderLabel Existing analytic collider label used as the attachment target.
     * @param constrainOrientation Also drive particle orientation from the target rotation.
     * @return Number of installed anchors, or failure before any mutation.
     * @details Simulation-thread only. Indices must be unique and currently unattached. The target
     * position is enforced without changing particle phase or material. No callback or external
     * pointer is retained; removal of the collider detaches the group.
     */
    [[nodiscard]] Result<unsigned> bindStaticParticles(std::span<const unsigned> particleIndices,
                                                       unsigned colliderLabel, bool constrainOrientation = false);
    /**
     * @brief Installs Fluid3D-style compliant dynamic pins for a particle group.
     * @param compliance Finite compliance in [0,1000000]; zero creates a rigid dynamic pin.
     * @param breakThreshold Finite force threshold in (0,1e12] newtons.
     * @details Dynamic particles retain mass and participate in the solver. Constraint work is
     * linear in anchor count and reactions are reported through attachmentReactions().
     */
    [[nodiscard]] Result<unsigned> bindDynamicParticles(std::span<const unsigned> particleIndices,
                                                        unsigned colliderLabel, float compliance, float breakThreshold,
                                                        bool constrainOrientation = false);
    /** @brief Removes anchors for the supplied unique current indices, returning the number removed atomically. */
    [[nodiscard]] Result<unsigned> unbindStaticParticles(std::span<const unsigned> particleIndices);
    /**
     * @brief Atomically replaces Fluid3DStitcher-compatible zero-distance constraints.
     * @param stitches Borrowed pairs copied after complete validation; at most 65536.
     * @details Indices in each pair must be distinct and no unordered pair may repeat. Compliance
     * is finite in [0,1000000]. Constraints respect particle mass and fixed attachment/grabber
     * state, persist in snapshots and remap on particle removal. Empty input clears all stitches.
     */
    [[nodiscard]] Result<void> setStitches(std::span<const VolumeFluidStitch> stitches);
    /**
     * @brief Atomically updates transforms of existing owned SDF colliders without copying distance samples.
     * @details Simulation-thread only. Each stable label must exist exactly once in the input.
     * Position, unit XYZW rotation, uniform scale and surface velocities are validated before
     * mutation. Inverted fields must still cover solver bounds after the update. Omitted labels
     * retain their current pose. No callbacks or borrowed input are retained.
     */
    [[nodiscard]] Result<void> updateSdfColliderPoses(std::span<const VolumeFluidSdfPose> poses);
    /** @brief Returns owning events from the last completed step; no borrowed world pointers are stored. */
    [[nodiscard]] std::vector<VolumeFluidContact> contacts() const;
    /** @brief Borrows attachment-constraint reactions from the last completed step.
     * @details Simulation-thread only. The span is invalidated by the next mutating solver call
     * or solver destruction. Reactions include prescribed linear inertia and the attached
     * particle's gravity load, ellipsoid angular inertia, and neighbor pressure,
     * cohesion and viscosity transfer;
     * contact impulses remain available separately through contacts().
     */
    [[nodiscard]] std::span<const VolumeFluidAttachmentReaction> attachmentReactions() const;
    /** @brief Queries a sphere, returning particle values; invalid query input returns a diagnostic. */
    [[nodiscard]] Result<std::vector<VolumeFluidParticle>> overlap(glm::vec3 center, float radius) const;
    /** @brief Queries particle centers inside an oriented box, including its boundary.
     * @param center World-space center in meters.
     * @param halfExtent Nonnegative half sizes, each at most 10000 meters.
     * @param rotation Unit quaternion in XYZW order; norm-squared tolerance is 0.001.
     * @return Owning particle values in pool order, or an invalid-argument diagnostic.
     * @details Simulation-thread only; O(particle count), no retained references or callbacks.
     */
    [[nodiscard]] Result<std::vector<VolumeFluidParticle>> overlapBox(glm::vec3 center, glm::vec3 halfExtent,
                                                                      glm::vec4 rotation) const;
    /** @brief Raycasts native oriented-ellipsoid particles and returns nearest hits first.
     * @param origin Finite world-space ray origin.
     * @param direction Finite nonzero direction; normalized internally.
     * @param maxDistance Finite segment length in [0,10000] meters.
     * @param maxHits Output bound in [1,4096]. The nearest hits are retained.
     * @param phaseMask Bits 0..3 select Liquid, Gas, Granular and Solid; other bits fail.
     * @return Owning hits, or failure without simulation mutation.
     * @details Transforms the ray into each selected particle's unit-sphere frame.
     * An origin inside a particle reports distance zero. At most 65536 live
     * particles are accepted per query to bound O(N log maxHits) work. Ties use
     * pool index for repeatability. Simulation-thread affine; no callbacks or
     * retained references. Indices become stale after any particle mutation.
     */
    [[nodiscard]] Result<std::vector<VolumeFluidRayHit>> raycast(glm::vec3 origin, glm::vec3 direction,
                                                                 float maxDistance, unsigned maxHits = 64,
                                                                 unsigned phaseMask = 0x0f) const;
    /** @brief Returns nearest signed distances between a sphere and particle surfaces.
     * @param center Finite world-space query center.
     * @param radius Query radius in [0,10000] meters.
     * @param contactOffset Nonnegative expansion in [0,10000] meters.
     * @param maxDistance Maximum positive separation in [0,10000]; overlaps always qualify.
     * @param maxHits Output bound in [1,4096]; smallest signed distances are retained.
     * @param phaseMask Bits 0..3 select Liquid, Gas, Granular and Solid; other bits fail.
     * @details Particles use their oriented ellipsoid radii. At most 65536 live particles are
     * accepted, bounding O(N log maxHits) work. Ties use current pool index.
     * Simulation-thread affine; owns outputs, invokes no callbacks, retains no
     * references and does not mutate simulation state. Indices become stale on mutation.
     */
    [[nodiscard]] Result<std::vector<VolumeFluidDistanceHit>> querySphere(glm::vec3 center, float radius,
                                                                          float contactOffset, float maxDistance,
                                                                          unsigned maxHits         = 64,
                                                                          unsigned phaseMask       = 0x0f,
                                                                          unsigned collisionFilter = 0xffffffffu) const;
    /** @brief Returns nearest signed distances between an oriented box and particle surfaces.
     * @param center World-space box center.
     * @param halfExtent Finite nonnegative local half sizes, each at most 10000 meters.
     * @param rotation Unit quaternion in XYZW order; norm-squared tolerance is 0.001.
     * @param contactOffset Nonnegative outward expansion in [0,10000] meters.
     * @param maxDistance Maximum positive separation in [0,10000]; overlaps always qualify.
     * @param maxHits Output bound in [1,4096]; smallest signed distances are retained.
     * @param phaseMask Bits 0..3 select Liquid, Gas, Granular and Solid; other bits fail.
     * @details Query points lie on the contact-offset-expanded box surface.
     * Inside points choose the nearest face with deterministic X/Y/Z tie order.
     * Particles use their oriented ellipsoid radius along the contact normal. At most 65536 live particles are
     * accepted, bounding O(N log maxHits) work. Simulation-thread affine; owns outputs, invokes no callbacks, retains
     * no references and does not mutate state.
     */
    [[nodiscard]] Result<std::vector<VolumeFluidDistanceHit>> queryBox(glm::vec3 center, glm::vec3 halfExtent,
                                                                       glm::vec4 rotation, float contactOffset,
                                                                       float maxDistance, unsigned maxHits = 64,
                                                                       unsigned phaseMask       = 0x0f,
                                                                       unsigned collisionFilter = 0xffffffffu) const;
    /** @brief Evaluates a bounded mixed batch of sphere, box and ray queries.
     * @param queries Borrowed descriptions in result-group order; at most 256.
     * @param maxHitsPerQuery Per-query nearest-hit bound in [1,4096].
     * @return Owning hits grouped by query index, then distance and particle index.
     * @details Uses Fluid3D QueryShape meanings: sphere radius is size.x, box size is
     * full local extent, and ray center/size are segment endpoints. The entire
     * batch is validated before scanning. At most 65536 particles and four million
     * query-particle candidate checks are accepted. Failure returns no partial
     * output and does not mutate simulation state. Simulation-thread affine;
     * invokes no callbacks and retains no references. Indices become stale after
     * particle mutation. Particle hits use owned oriented ellipsoid radii.
     */
    [[nodiscard]] Result<std::vector<VolumeFluidQueryHit>> queryBatch(std::span<const VolumeFluidQueryShape> queries,
                                                                      unsigned maxHitsPerQuery = 64) const;
    /**
     * @brief Applies an Fluid3D OverlapTest-style base color and per-query overlap colors atomically.
     * @param queries Borrowed sphere, box or ray descriptions in priority order; at most 256.
     * @param overlapColors One finite linear RGBA color per query. Later queries win on overlap.
     * @param baseColor Finite linear RGBA assigned to particles outside every retained overlap.
     * @param maxHitsPerQuery Per-query overlap/result bound in [1,4096].
     * @return Owning overlap counts in query order, or failure without changing any particle color.
     * @details Only hits with negative signed distance are triggers. Query validation, reciprocal
     * filtering and the four-million candidate budget are shared with queryBatch. Returned particle
     * copies are discarded before mutation. Simulation-thread affine; no callbacks or GPU work.
     */
    [[nodiscard]] Result<std::vector<unsigned>> applyQueryColors(std::span<const VolumeFluidQueryShape> queries,
                                                                 std::span<const glm::vec4>             overlapColors,
                                                                 glm::vec4 baseColor, unsigned maxHitsPerQuery = 4096);
    /**
     * @brief Applies per-query colors to overlapping particles while preserving all other colors.
     * @param queries Borrowed sphere, box or ray descriptions in priority order; at most 256.
     * @param overlapColors One finite linear RGBA color per query. Later queries win on overlap.
     * @param maxHitsPerQuery Per-query overlap/result bound in [1,4096].
     * @return Owning overlap counts in query order, or failure without changing any particle color.
     * @details This is the persistent-color variant used by contact/trigger colorizers. It shares
     * queryBatch validation and budgets, mutates only negative-distance hits after the full query
     * succeeds, retains no references, invokes no callbacks and performs no GPU work.
     */
    [[nodiscard]] Result<std::vector<unsigned>> applyQueryColorsPreservingOutside(
        std::span<const VolumeFluidQueryShape> queries, std::span<const glm::vec4> overlapColors,
        unsigned maxHitsPerQuery = 4096);
    /**
     * @brief Queries point/edge/triangle simplexes and returns Fluid3D-compatible barycentric results.
     * @param queries Borrowed query descriptions, at most 256.
     * @param maxHitsPerQuery Per-query output bound in [1,4096].
     * @details Empty explicit topology means one point simplex per particle. Work is rejected
     * above four million query-simplex candidates. Results own their values; indices become
     * stale after particle or topology mutation. Simulation-thread affine; no callbacks.
     */
    [[nodiscard]] Result<std::vector<VolumeFluidSimplexHit>> querySimplexes(
        std::span<const VolumeFluidQueryShape> queries, unsigned maxHitsPerQuery = 64) const;
    /**
     * @brief Atomically replaces point/edge/triangle topology over current particle indices.
     * @details At most 65536 simplexes. Empty restores implicit point topology. New emissions
     * remain addressable as implicit points only when topology is empty; explicit topology is
     * otherwise unchanged. Lifetime removal remaps surviving indices and drops affected simplexes.
     */
    [[nodiscard]] Result<void> setSimplexes(std::span<const VolumeFluidSimplex> simplexes);
    /** @brief Atomically changes the material of particles in a sphere (supports melting/solidifying). */
    [[nodiscard]] Result<void> paint(glm::vec3 center, float radius, const VolumeFluidMaterial& material);
    /** @brief Atomically maps user data x/y to material viscosity/cohesion for all particles.
     * @return Failure without mutation if any x is outside [0,100] or y outside [0,10].
     * @details Explicit simulation-thread operation; data remains unchanged. Retains no
     * references, invokes no callbacks, and does not create an automatic binding.
     * Optional 2..32 strictly ordered color keys are borrowed for this call only.
     * Empty keys preserve colors. Otherwise colors use the pre-update viscosity,
     * linearly interpolate RGBA and clamp outside the key range, then channels
     * update material parameters. This matches the reference sample's update order.
     * Invalid keys or channels leave all particles unchanged. No allocation,
     * retained bindings, callbacks or simulation-time advancement occurs.
     */
    [[nodiscard]] Result<void> applyMaterialChannels(std::span<const VolumeFluidViscosityColorKey> colorKeys = {});
    /** @brief Assigns a linear RGBA color to all current solid particles in place.
     * @param color Finite components in [0,1]; invalid input fails before mutation.
     * @details Explicit simulation-thread operation, O(particle count), no allocation
     * or callbacks. Includes attached and world-fixed solids; other phases and all
     * non-color state are unchanged. No style binding is retained. Call after a step
     * to color newly frozen particles; melting does not restore a previous color.
     * The resulting colors are owned by particles and included in existing snapshots.
     */
    [[nodiscard]] Result<void> applySolidColor(glm::vec4 color);
    /** @brief Applies Fluid3D ColorFromVelocity RGB mapping to all particles in place.
     * @param sensibility Positive velocity component magnitude mapped to either color extreme.
     * @details Each RGB channel is clamp(velocity/sensibility,-1,1)*0.5+0.5;
     * alpha becomes one. Explicit linear-time, allocation-free, simulation-thread affine.
     * Invalid sensitivity fails before mutation; no callbacks, GPU work or retained binding.
     */
    [[nodiscard]] Result<void> applyVelocityColors(float sensibility);
    /** @brief Applies Fluid3D's 26-color alphabet by actorGroup modulo 26.
     * @details Explicit linear-time, allocation-free and simulation-thread affine.
     * Only owned particle colors change; no callbacks, GPU work or retained binding.
     */
    [[nodiscard]] Result<void> applyActorGroupColors();
    /** @brief Colors particles by one normalized application-data channel.
     * @param channel data component index in [0,3].
     * @param gradient Borrowed 2..32 key gradient with strictly increasing [0,1] coordinates.
     * @details Values clamp to endpoint colors. Validation completes before the allocation-free
     * linear-time mutation. Simulation-thread affine; no callbacks or GPU work.
     */
    [[nodiscard]] Result<void> applyDataColors(unsigned channel, std::span<const VolumeFluidColorKey> gradient);
    /** @brief Applies a deterministic Fluid3D ColorRandomizer-style gradient sample to every particle.
     * @param gradient Borrowed validated 2..32 key gradient.
     * @param seed Named deterministic random stream seed.
     * @details Explicit allocation-free linear-time mutation; no global RNG or retained state.
     */
    [[nodiscard]] Result<void> applyRandomColors(std::span<const VolumeFluidColorKey> gradient, uint32_t seed);
    /** @brief Applies one deterministic random velocity impulse to the whole actor.
     * @param intensity Velocity-change magnitude in [0,100] metres per second.
     * @param seed Named deterministic random stream seed; no global RNG is touched.
     * @details Matches the package AddRandomVelocity behavior: all particles receive
     * the same unit-sphere direction times intensity. Validation and the resulting
     * 100 m/s particle-speed bound are checked before mutation. Linear-time,
     * allocation-free, simulation-thread affine; no callbacks or GPU work.
     */
    [[nodiscard]] Result<void> addRandomVelocity(float intensity, uint32_t seed);
    /** @brief Teleports the whole fluid actor between explicit world poses.
     * @param currentPosition Current actor origin in world space.
     * @param currentRotation Current unit actor rotation in XYZW order.
     * @param targetPosition Target actor origin in world space.
     * @param targetRotation Target unit actor rotation in XYZW order.
     * @details Applies targetRotation*inverse(currentRotation) to particle positions
     * and orientations, then clears linear/angular velocity like Fluid3DActor::Teleport.
     * All transformed particles are checked against solver bounds before mutation.
     * Attachments and simplex indices remain owned and valid; derived contacts and
     * reaction output are cleared. Allocation-free linear-time simulation operation.
     */
    [[nodiscard]] Result<void> teleportActor(glm::vec3 currentPosition, glm::vec4 currentRotation,
                                             glm::vec3 targetPosition, glm::vec4 targetRotation);
    /** @brief Computes mass and world-space center for one actor group.
     * @param actorGroup Fluid3D-compatible actor group in [0, 0x00ffffff].
     * @return Owning summary, or NotFound when the group has no live particles.
     * @details This is the data used by the package ActorCOMTransform helper. It is
     * an explicit allocation-free O(particle count) query and does not run during
     * fixed stepping or rendering. Particle rest mass is spacing cubed times density.
     */
    [[nodiscard]] Result<VolumeFluidMassProperties> actorMassProperties(unsigned actorGroup) const;
    /** @brief Applies Fluid3D SetFilterCategory to all particles in one actor group.
     * @param actorGroup Stable actor group in [0,0x00ffffff].
     * @param category Collision category index in [0,15].
     * @details Replaces each matching filter's low 16-bit category with one bit
     * while preserving its high 16-bit mask. Explicit allocation-free linear scan;
     * missing groups and invalid input fail without mutation or GPU work.
     */
    [[nodiscard]] Result<void> setActorFilterCategory(unsigned actorGroup, unsigned category);
    /** @brief Applies Fluid3DEmitter.Filter to every live particle in one actor group.
     * @param actorGroup Stable actor group in [0,0x00ffffff].
     * @param collisionFilter Packed category/mask value; both 16-bit halves must be nonzero.
     * @details Replaces the complete packed filter for matching particles. This is
     * an explicit allocation-free linear scan; missing groups and invalid input
     * fail before mutation and the operation performs no solver or GPU work.
     */
    [[nodiscard]] Result<void> setActorCollisionFilter(unsigned actorGroup, unsigned collisionFilter);
    /** @brief Applies Fluid3DEmitter::UpdateParticleMaterial to one live actor group.
     * @param actorGroup Stable actor group in [0,0x00ffffff].
     * @param prototype Borrowed emitter prototype supplying material, data, radii,
     * collision filter and self-collision state; motion, pose, color and life are ignored.
     * @details The complete prototype subset and actor existence are validated before
     * one allocation-free linear mutation. Automatic zero radii resolve to spacing/2.
     * No callback, fixed-step traversal or GPU work is installed.
     */
    [[nodiscard]] Result<void> updateActorMaterial(unsigned actorGroup, const VolumeFluidParticle& prototype);
    /** @brief Applies Fluid3DEmitter::SetSelfCollisions to all live particles in one actor group.
     * @param actorGroup Stable actor group in [0,0x00ffffff].
     * @param enabled Whether same-group pairs may generate particle contacts.
     * @details Actor existence is checked before one allocation-free linear mutation;
     * missing groups fail without state changes or GPU work.
     */
    [[nodiscard]] Result<void> setActorSelfCollisions(unsigned actorGroup, bool enabled);
    /** @brief Implements Fluid3DEmitter::KillAll for one actor in a shared solver.
     * @param actorGroup Stable actor group in [0,0x00ffffff].
     * @return Number of particles returned to the pool; NotFound if none are live.
     * @details Compacts particles once, remaps surviving attachments/stitches/simplexes,
     * and clears derived contacts/reactions. Reuses solver index scratch, invokes no
     * callbacks and performs no GPU work. Invalid or missing groups do not mutate state.
     */
    [[nodiscard]] Result<unsigned> killActorParticles(unsigned actorGroup);
    /** @brief Counts current live particles for one Fluid3D-compatible actor group.
     * @param actorGroup Stable actor group in [0,0x00ffffff].
     * @return Live count, including zero when the actor currently has no particles.
     * @details Simulation-thread only. Performs one allocation-free read-only scan and
     * does not compute mass properties, invoke callbacks or perform GPU work.
     */
    [[nodiscard]] Result<unsigned> actorParticleCount(unsigned actorGroup) const;
    /** @brief Removes one particle by its actor-local active index, matching Fluid3DEmitter::KillParticle.
     * @details Simulation-thread only. Performs one allocation-free dense scan, then uses the
     * canonical atomic compaction/remapping path. Invalid groups or stale actor indices preserve state.
     */
    [[nodiscard]] Result<void> killActorParticle(unsigned actorGroup, unsigned actorParticleIndex);
    /** @brief Applies one fixed-step Fluid3DParticleDragger spring response.
     * @param particleIndex Current dense particle index, usually selected by raycast().
     * @param targetPosition Finite world-space drag target.
     * @param stiffness Spring acceleration coefficient in [0,10000].
     * @param damping Velocity damping coefficient in [0,10000].
     * @param seconds Injected fixed-step duration in (0,1/30].
     * @details Updates one velocity using semi-implicit spring acceleration. The
     * operation is allocation-free O(1), invokes no callback, performs no GPU work,
     * and fails before mutation if the resulting speed would exceed 1000 m/s.
     */
    [[nodiscard]] Result<void> applyParticleDrag(unsigned particleIndex, glm::vec3 targetPosition, float stiffness,
                                                 float damping, float seconds);
    /** @brief Freezes current contacts for one Fluid3DContactGrabber-compatible collider.
     * @param colliderLabel Existing analytic, SDF or height-field collider label.
     * @param position Current grabber world position.
     * @param rotation Current unit grabber world rotation in XYZW order.
     * @param distanceThreshold Maximum signed contact distance in [0,0.1] metres.
     * @return Number of particles captured from the latest successful contact set.
     * @details Replaces the label's previous capture atomically. Captured particles
     * retain local coordinates, contribute as zero-inverse-mass neighbors, and do
     * not integrate until released. One particle may belong to only one grabber.
     * Simulation-thread affine; bounded by current contacts and particle count.
     */
    [[nodiscard]] Result<unsigned> grabContactParticles(unsigned colliderLabel, glm::vec3 position, glm::vec4 rotation,
                                                        float distanceThreshold = .01f);
    /** @brief Moves all particles owned by one contact grabber to a new world pose.
     * @return Updated particle count; out-of-bounds poses fail before mutation.
     * @details Allocation-free after first capture and performs no solver step,
     * callback or GPU work. The caller supplies the authoritative fixed-step pose.
     */
    [[nodiscard]] Result<unsigned> updateGrabbedParticles(unsigned colliderLabel, glm::vec3 position,
                                                          glm::vec4 rotation);
    /** @brief Releases one grabber label and restores normal particle dynamics.
     * @return Released particle count. Missing labels are a successful zero count.
     */
    [[nodiscard]] Result<unsigned> releaseGrabbedParticles(unsigned colliderLabel);
    /** @brief Samples the current liquid/gas field at up to 65536 finite world positions.
     * @details Simulation-thread only; positions are borrowed for this call, output
     * owns one sample per input in the same order. No callbacks or time advancement.
     * Rebuilds derived spatial scratch to account for emission/lifetime/restore.
     * Empty support returns zero fields. Solids and granular particles do not contribute.
     * @return Complete samples, or failure without observable mutation for invalid input
     * or more than 4000000 candidate particle visits. No partial batch is returned.
     */
    [[nodiscard]] Result<std::vector<VolumeFluidFieldSample>> sampleField(std::span<const glm::vec3> positions) const;
    /** @brief Returns occupied particle-grid cells for explicit debug visualization.
     * @param maxCells Maximum complete result size in [1,1000000].
     * @return Owning cells in stable linear-grid order, or failure without partial output.
     * @details The query sorts one capacity-reserved index per live particle and
     * does not scan empty grid cells. It runs only when called and adds no work,
     * allocation, renderer state or GPU operation to normal simulation frames.
     */
    [[nodiscard]] Result<std::vector<VolumeFluidGridCell>> debugParticleGrid(unsigned maxCells = 65536) const;
    /**
     * @brief Returns particle-local XYZ rays for one actor group.
     * @param actorGroup Fluid3D-compatible actor group in [0,0x00ffffff].
     * @param size Finite ray length in (0,100] world units.
     * @param maxParticles Positive result budget capped at one million.
     * @details Explicit simulation-thread query. It scans current particles once, allocates only
     * the bounded owning result and performs no fixed-step, GPU, upload or rendering work.
     */
    [[nodiscard]] Result<std::vector<VolumeFluidParticleFrame>> debugParticleFrames(
        unsigned actorGroup, float size = 1.f, unsigned maxParticles = 65536) const;
    /**
     * @brief Copies one actor group's interpolated mesh instances for batched particle rendering.
     * @param actorGroup Fluid3D-compatible actor group in [0,0x00ffffff].
     * @param instanceScale Positive finite component-wise scale multiplier.
     * @param alpha Finite fixed-step position interpolation fraction in [0,1].
     * @param maxInstances Positive complete-output budget capped at one million.
     * @return Owning stable-order instance rows, or failure without partial output.
     * @details Explicit render-thread preparation. It scans particles twice only when called,
     * allocates one bounded output and performs no draw, GPU dispatch or texture upload.
     */
    [[nodiscard]] Result<std::vector<VolumeFluidParticleInstance>> particleInstances(
        unsigned actorGroup, glm::vec3 instanceScale = glm::vec3(1.f), float alpha = 1.f,
        unsigned maxInstances = 65536) const;
    /**
     * @brief Copies one actor group's Fluid3DParticleRenderer-style anisotropic impostor instances.
     * @param actorGroup Fluid3D-compatible actor group in [0,0x00ffffff].
     * @param radiusScale Positive finite scalar applied to every principal particle radius.
     * @param tint Finite linear RGBA multiplier in [0,1], multiplied with each particle color.
     * @param alpha Finite fixed-step position interpolation fraction in [0,1].
     * @param maxInstances Positive complete-output budget capped at one million.
     * @return Owning stable-order instance rows, or failure without partial output.
     * @details Explicit render-thread preparation. It preserves the solver's anisotropy and orientation,
     * scans particles twice only when called, and emits one row per particle for caller-owned batched
     * drawing. It does not expand particles into CPU quad vertices or submit GPU/render work.
     */
    [[nodiscard]] Result<std::vector<VolumeFluidParticleInstance>> particleImpostors(
        unsigned actorGroup, float radiusScale, glm::vec4 tint = glm::vec4(1.f), float alpha = 1.f,
        unsigned maxInstances = 65536) const;
    /**
     * @brief Writes Fluid3DParticleRenderer-style instances into caller-owned reusable storage.
     * @param actorGroup Fluid3D-compatible actor group in [0,0x00ffffff].
     * @param radiusScale Positive finite scalar applied to every principal particle radius.
     * @param tint Finite linear RGBA multiplier in [0,1].
     * @param alpha Finite fixed-step position interpolation fraction in [0,1].
     * @param maxInstances Positive complete-output budget capped at one million.
     * @param instances Destination replaced only after validation and complete-size budgeting.
     * @return Success, or failure leaving instances unchanged.
     * @details Existing destination capacity is retained, so a warmed render loop performs no
     * allocation. Simulation-thread affine; the solver and destination are borrowed for this call only.
     */
    [[nodiscard]] Result<void> copyParticleImpostors(unsigned actorGroup, float radiusScale, glm::vec4 tint,
                                                     float alpha, unsigned maxInstances,
                                                     std::vector<VolumeFluidParticleInstance>& instances) const;
    /**
     * @brief Samples one owned SDF collider slice for Fluid3DDistanceFieldRenderer-style diagnostics.
     * @param colliderLabel Stable SDF collider label.
     * @param axis Local slice-normal axis.
     * @param slice Finite normalized location in [0,1] across the selected local axis.
     * @param maxDistance Positive symmetric distance range mapped to [0,1].
     * @param maxSamples Positive complete-output budget capped at one million.
     * @return Owning row-major values and a world-space sample lattice, or failure without partial output.
     * @details Explicit simulation-thread query. It allocates and samples only when called and adds no
     * fixed-step traversal, callback, GPU dispatch, upload or surface reconstruction work.
     */
    [[nodiscard]] Result<VolumeFluidSdfSlice> debugSdfSlice(unsigned colliderLabel, VolumeFluidSdfSliceAxis axis,
                                                            float slice = .25f, float maxDistance = .5f,
                                                            unsigned maxSamples = 65536) const;
    /** @brief Returns complete owning state; scratch grids and last-step contacts are derived and omitted. */
    [[nodiscard]] VolumeFluidSnapshot snapshot() const;
    /** @brief Validates a complete candidate before replacing state; failures preserve the current solver. */
    [[nodiscard]] Result<void> restore(const VolumeFluidSnapshot& snapshot);
    /** @brief Removes all live particles; settings remain unchanged. */
    void clear();
    /** @brief Removes one live particle and returns its slot to the bounded pool.
     * @param particleIndex Current dense particle index.
     * @return Success, or invalid argument without mutation when the index is stale.
     * @details Simulation-thread only. Later indices shift down by one; attachments
     * and unaffected simplex indices are remapped, while simplexes containing the
     * removed particle are dropped. Pending per-particle winds remain aligned.
     * Last-step contacts and attachment reactions are invalidated. No callbacks.
     */
    [[nodiscard]] Result<void> killParticle(unsigned particleIndex);

private:
    friend class VolumeFluidFoam;
    friend class VolumeFluidDiffuse;
    struct Impl;
    [[nodiscard]] Result<unsigned> bindParticles(std::span<const unsigned> particleIndices, unsigned colliderLabel,
                                                 bool constrainOrientation, bool dynamic, float compliance,
                                                 float breakThreshold);
    [[nodiscard]] Result<void> accumulateForceZones(std::span<const VolumeFluidWindZone> zones, float fixedTimeSeconds,
                                                    bool asWind);
    [[nodiscard]] glm::vec3    interpolatedPositionUnchecked(size_t particleIndex, float alpha) const;
    [[nodiscard]] Result<void> sampleFieldInto(std::span<const glm::vec3>           positions,
                                               std::vector<VolumeFluidFieldSample>& samples) const;
    explicit VolumeFluid(const VolumeFluidSettings& settings);
    std::unique_ptr<Impl> impl_;
};

}  // namespace eve::fluids
