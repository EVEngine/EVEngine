#pragma once

/**
 * @file Climbing.h
 * @brief Deterministic climbing/parkour planning and capsule-constrained execution.
 */

#include "climbing/ClimbingAnchorGraph.h"
#include "climbing/ClimbingAnchorAuthoring.h"
#include "climbing/ClimbingPrimitives.h"
#include "climbing/ClimbingTelemetry.h"
#include "common/Module.h"
#include "common/Result.h"
#include "common/SquirrelOwnership.h"
#include "common/StrongUint64.h"
#include "common/Time.h"
#include "common/Value.h"
#include "physics/OwnedQuery3D.h"

#include <cstddef>
#include <cstdint>
#include <array>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace eve::animation {
class AnimClip;
}

namespace eve::physics {
class World3D;
}

namespace eve::climbing {

class ClimbingSelectionSystem;
class ClimbingServiceSelectionSystem;

/** @brief Data-driven action family; stable action identity remains the definition id. */
enum class ClimbingActionKind : std::uint8_t {
    Vault,
    Mantle,
    LedgeGrab,
    ClimbUp,
    Shimmy,
    CornerInner,
    CornerOuter,
    LedgeJump,
    ClimbDown,
    LadderMount,
    LadderClimb,
    LadderDismount,
    WallRun,
    Slide,
    BeamBalance,
    PoleSwing,
    BarSwing
};

/** @brief Movement mode used by action source-mode hard validation. */
enum class ClimbingMovementMode : std::uint8_t { Grounded, Airborne, Climbing };

/** @brief Bit mask of movement modes from which an action may start. */
enum class ClimbingSourceMode : std::uint8_t {
    None     = 0,
    Grounded = 1u << 0u,
    Airborne = 1u << 1u,
    Climbing = 1u << 2u,
    Any      = 0x07
};

/** @brief Input edge required by an action definition; Any keeps legacy automatic selection. */
enum class ClimbingCommandRequirement : std::uint8_t { Any, Jump, Climb, Drop, Sprint, Crouch };

/** @brief Semantic query family used to produce action candidates. */
enum class ClimbingProbeRecipe : std::uint8_t { Automatic, Obstacle, Ledge, Wall, Ground, AnchorGraph };

/** @brief Semantic trajectory topology, independent of a particular animation clip. */
enum class ClimbingTrajectoryKind : std::uint8_t { Automatic, BallisticArc, SurfaceFollow, AnchorToAnchor };

/** @brief Policy applied when authored motion reaches its landing phase. */
enum class ClimbingLandingPolicy : std::uint8_t { PreserveMomentum, MatchGround, Stop };

/** @brief Policy applied to vertical velocity when an action terminates. */
enum class ClimbingTerminalVelocityPolicy : std::uint8_t { Preserve, ClampDownward, Zero };

/** @brief Optional stamina integration policy; climbing never owns stamina state. */
enum class ClimbingStaminaPolicy : std::uint8_t { Disabled, RequireProvider, UseWhenPresent };

/** @brief Input interpretation used by candidate assist and deterministic selection policies. */
enum class ClimbingInputMode : std::uint8_t { Flow, Precision };

/** @brief Stable animation asset and graph binding metadata. */
struct ClimbingAnimationBinding {
    std::string clipId;
    std::string graphNodeId;
    std::string rootBone = "Root";
    bool        mirrored = false;
};

/** @brief Normalized branch interval advertising one stable combo tag. */
struct ClimbingBranchWindow {
    float       start = 0.f;
    float       end   = 1.f;
    std::string comboTag;
};

/** @brief Contact target used by animation/IK adapters. */
enum class ClimbingContactTarget : std::uint8_t { LeftHand, RightHand, LeftFoot, RightFoot, Pelvis };

/** @brief Normalized contact interval and maximum constraint weight. */
struct ClimbingContactConstraint {
    ClimbingContactTarget target    = ClimbingContactTarget::LeftHand;
    float                 start     = 0.f;
    float                 end       = 1.f;
    float                 maxWeight = 1.f;
};

/** @brief Quantized selection-cost weights stored with a profile. */
struct ClimbingScoreWeights {
    std::int32_t direction       = 1000;
    std::int32_t approachSpeed   = 1000;
    std::int32_t height          = 1000;
    std::int32_t distance        = 1000;
    std::int32_t warpTranslation = 1000;
    std::int32_t warpRotation    = 1000;
    std::int32_t intentMismatch  = 1000;
};

/** @brief Evidence describing whether a hanging pose has foot support. */
enum class HangSupport : std::uint8_t { None, Braced, Free };

/** @brief Stable, strongly typed terminal cancellation reason. */
enum class ClimbingCancelReason : std::uint8_t {
    PlayerRequest,
    DropRequested,
    LinkStale,
    AnchorStale,
    MotionBlocked,
    WarpBudgetExceeded,
    DefinitionReloaded
};

/** @brief Normalized action interval selecting which motion-warp channels may correct authored motion. */
struct ClimbingWarpWindow {
    float start      = 0.f;
    float end        = 1.f;
    bool  horizontal = true;
    bool  vertical   = true;
    bool  facing     = true;
};

/** @brief Definition of one geometry-compatible climbing action. */
struct ClimbingActionDefinition {
    /** @brief Schema id emitted by the canonical action codec. */
    static constexpr std::string_view SchemaId = "evengine.climbing-action";
    /** @brief Current action schema version. */
    static constexpr std::int64_t SchemaVersion = 4;

    std::string                     id;
    float                           minHeight                 = 0.f;
    float                           maxHeight                 = 1.f;
    float                           minSpeed                  = 0.f;
    eve::Duration                   duration                  = eve::Duration::fromNanoseconds(500000000);
    float                           landingForward            = 0.35f;
    float                           apexHeight                = 0.15f;
    int                             selectionBias             = 0;
    ClimbingActionKind              kind                      = ClimbingActionKind::Mantle;
    float                           hangBodyOffset            = 0.35f;
    float                           hangFeetBelowLedge        = 1.55f;
    float                           handSpacing               = 0.42f;
    float                           cancelWindowStart         = 0.f;
    float                           cancelWindowEnd           = 1.f;
    float                           rootMotionScaleMin        = 0.75f;
    float                           rootMotionScaleMax        = 1.35f;
    float                           maxTranslationWarpPerTick = 0.15f;
    float                           maxYawWarpRadiansPerTick  = 0.12f;
    float                           horizontalWarpBudget      = 0.6f;
    float                           verticalWarpBudget        = 0.6f;
    float                           facingWarpBudgetRadians   = 0.8f;
    std::vector<ClimbingWarpWindow> warpWindows;
    std::vector<std::string>        requiredNotifies;
    std::vector<std::string>        tags;
    ClimbingSourceMode              sourceModes = ClimbingSourceMode::Any;
    ClimbingCommandRequirement      requiredCommand = ClimbingCommandRequirement::Any;
    ClimbingProbeRecipe             probeRecipe = ClimbingProbeRecipe::Automatic;
    float                           minDepth = 0.f;
    float                           maxDepth = 1000.f;
    float                           minDistance = 0.f;
    float                           maxDistance = 1000.f;
    float                           minSurfaceNormalY = -1.f;
    float                           maxSlopeRadians = 3.1415927f;
    float                           maxCurvature = 1000.f;
    std::vector<std::string>        requiredSupportTags;
    ClimbingTrajectoryKind          trajectory = ClimbingTrajectoryKind::Automatic;
    ClimbingAnimationBinding        animation;
    std::vector<ClimbingBranchWindow> branchWindows;
    std::vector<ClimbingContactConstraint> contactConstraints;
    ClimbingLandingPolicy           landingPolicy = ClimbingLandingPolicy::MatchGround;
    ClimbingTerminalVelocityPolicy  terminalVelocityPolicy = ClimbingTerminalVelocityPolicy::Preserve;
    std::vector<std::string>        comboTags;
    std::int32_t                    repetitionPenalty = 0;
    std::vector<std::string>        requiredConditionTags;
    float                           staminaCost = 0.f;
    std::string                     cameraCue;
    std::vector<std::string>        eventMetadata;
    /** @brief Unknown schema fields retained across decode and encode. */
    eve::Value::Object extensionMetadata;
};

/** @brief Runtime profile controlling capsule geometry and probe limits. */
struct ClimbingProfileDefinition {
    /** @brief Schema id emitted by the canonical profile codec. */
    static constexpr std::string_view SchemaId = "evengine.climbing-profile";
    /** @brief Current profile schema version. */
    static constexpr std::int64_t SchemaVersion = 4;

    float                                 capsuleRadius          = 0.3f;
    float                                 capsuleHeight          = 1.8f;
    float                                 compactCapsuleHeight   = 1.2f;
    float                                 skin                   = 0.03f;
    float                                 maxProbeDistance       = 1.2f;
    float                                 maxObstacleHeight      = 1.8f;
    float                                 minTopNormalY          = 0.65f;
    float                                 maxWarpResidual        = 0.2f;
    float                                 maxPlatformSpeed       = 8.f;
    float                                 dropInitialSpeed       = 1.5f;
    std::uint64_t                         inputBufferTicks       = 8;
    std::uint64_t                         coyoteTicks            = 6;
    std::uint32_t                         pathValidationSegments = 12;
    float                                 maxPelvisDeviation     = 0.35f;
    float                                 groundAcceleration     = 28.f;
    float                                 groundBraking          = 32.f;
    float                                 airControl             = 0.45f;
    float                                 gravity                = 24.f;
    float                                 jumpSpeed              = 7.f;
    std::uint32_t                         probeSectors           = 8;
    std::uint32_t                         maxCandidates          = 8;
    float                                 autoAssistStrength     = 0.65f;
    ClimbingScoreWeights                  scoreWeights;
    float                                 maxTotalWarpBudget     = 1.2f;
    std::vector<std::string>              defaultActionIds;
    std::vector<std::string>              allowedActionTags;
    std::vector<std::string>              deniedActionTags;
    ClimbingStaminaPolicy                 staminaPolicy = ClimbingStaminaPolicy::Disabled;
    std::string                           staminaAdapter;
    std::string                           cameraCueProfile;
    physics::QueryFilter3D                queryFilter{};
    std::vector<ClimbingActionDefinition> actions;
    /** @brief Unknown schema fields retained across decode and encode. */
    eve::Value::Object extensionMetadata;
};

/** @brief Small owning projection of profile fields consumed by ordinary ground/air locomotion. */
struct ClimbingLocomotionPolicy {
    float                  groundAcceleration = 28.f;
    float                  groundBraking      = 32.f;
    float                  airControl          = 0.45f;
    float                  gravity             = 24.f;
    float                  jumpSpeed           = 7.f;
    std::uint64_t          coyoteTicks         = 6;
    physics::QueryFilter3D queryFilter{};
};

/** @brief Compatibility-only spelling; ClimbingProfileDefinition is canonical. */
using ClimbingProfile = ClimbingProfileDefinition;

/** @brief Input pose used for one deterministic candidate probe. */
struct ClimbingPose {
    Vec3  feet;
    Vec3  forward{0.f, 0.f, 1.f};
    float speed         = 0.f;
    int   ignoredBodyId = -1;
    float verticalSpeed = 0.f;
    bool  grounded      = true;
    /** @brief Camera-relative movement intent; Flow mode prioritizes this direction. */
    Vec3  moveIntent;
    /** @brief Camera/look intent; Precision mode prioritizes this direction. */
    Vec3  lookIntent{0.f, 0.f, 1.f};
    ClimbingInputMode inputMode = ClimbingInputMode::Flow;
};

/** @brief Owning, replayable climbing candidate; contains no borrowed query pointers. */
struct ClimbingCandidate {
    std::string                 actionId;
    /** @brief Definition generation used to produce this candidate. */
    std::uint64_t               definitionGeneration = 1;
    physics::PhysicsWorldHandle world           = physics::PhysicsWorldHandle::invalid();
    physics::PhysicsBodyHandle  obstacleBody    = physics::PhysicsBodyHandle::invalid();
    physics::PhysicsShapeHandle obstacleShape   = physics::PhysicsShapeHandle::invalid();
    int                         obstacleBodyId  = -1;
    int                         obstacleShapeId = -1;
    int                         ignoredBodyId   = -1;
    Vec3                        frontPoint;
    Vec3                        topPoint;
    Vec3                        landingFeet;
    Vec3                        surfaceNormal;
    Vec3                        surfaceTangent;
    Vec3                        leftHandAnchor;
    Vec3                        rightHandAnchor;
    Vec3                        bodyLocalTop;
    Vec3                        bodyLocalLanding;
    float                       obstacleHeight = 0.f;
    float                       obstacleDepth  = 0.f;
    float                       gapDistance    = 0.f;
    float                       clearanceHeight = 0.f;
    float                       slopeRadians   = 0.f;
    float                       curvature      = 0.f;
    int                         supportShapeTag = 0;
    int                         supportMaterialId = 0;
    ClimbingProbeRecipe         probeRecipe = ClimbingProbeRecipe::Automatic;
    std::int64_t                score          = 0;
    ClimbingActionKind          kind           = ClimbingActionKind::Mantle;
    HangSupport                 support        = HangSupport::None;
};

/**
 * @brief Fixed-capacity owning candidate set used by the production probe hot path.
 *
 * `consider` retains the deterministic best eight candidates without heap growth in the container. String
 * storage is reused when the same set is passed repeatedly to `probeInto`.
 */
class ClimbingCandidateSet {
public:
    static constexpr std::size_t Capacity = 8;

    /** @brief Remove all candidates while retaining each slot's owned string capacity. */
    void clear() noexcept { size_ = 0; }
    /** @brief Consider one candidate and retain it only when it belongs to the best fixed-capacity set. */
    void consider(ClimbingCandidate candidate);
    /**
     * @brief Consider a candidate while reusing slot-owned action-id storage on the production probe path.
     * @param actionId Stable definition id borrowed only for this synchronous call.
     */
    void considerWithActionId(ClimbingCandidate candidate, std::string_view actionId);
    /** @brief Sort by canonical deterministic selection key and clamp to a validated limit. */
    void sortAndLimit(std::size_t limit);
    /** @brief Exchange complete owning slot storage without allocating. */
    void swap(ClimbingCandidateSet& other) noexcept;
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] const ClimbingCandidate& front() const noexcept { return values_[0]; }
    /** @brief Move the best candidate out of a non-empty set. */
    [[nodiscard]] ClimbingCandidate takeFront() noexcept { return std::move(values_[0]); }
    [[nodiscard]] const ClimbingCandidate& operator[](std::size_t index) const noexcept { return values_[index]; }
    /**
     * @brief Returns the first candidate for synchronous read-only iteration.
     * @return A borrowed pointer valid only until this candidate set is modified or destroyed; do not retain it.
     * @lifetime The candidate set owns the storage; the caller must not retain the returned pointer.
     */
    [[nodiscard]] const ClimbingCandidate* begin() const noexcept { return values_.data(); }
    /**
     * @brief Returns the sentinel following the last candidate for synchronous read-only iteration.
     * @return A borrowed pointer valid only until this candidate set is modified or destroyed; do not retain it.
     * @lifetime The candidate set owns the storage; the caller must not retain the returned pointer.
     */
    [[nodiscard]] const ClimbingCandidate* end() const noexcept { return values_.data() + size_; }
    [[nodiscard]] std::span<const ClimbingCandidate> values() const noexcept { return {values_.data(), size_}; }

private:
    std::array<ClimbingCandidate, Capacity> values_{};
    std::size_t                             size_ = 0;
};

namespace detail {
struct ClimbingPredictionSequenceTag {};
}

/** @brief Non-zero client command sequence used to pair one prediction decision. */
using ClimbingPredictionSequence = eve::detail::StrongUint64<detail::ClimbingPredictionSequenceTag>;

/**
 * @brief Opaque deterministic identity of one sorted candidate; contains no client-authored position.
 *
 * The fingerprint is computed from the complete authoritative candidate evidence. Network peers must use
 * replicated stable obstacle identities when they expect exact acceptance across processes; any mismatch is
 * safely corrected because the server always executes its own re-probed candidate.
 */
struct ClimbingCandidateKey {
    std::string             actionId;
    ClimbingActionKind      kind = ClimbingActionKind::Mantle;
    std::uint64_t           definitionGeneration = 0;
    std::uint32_t           sortedRank = 0;
    std::uint64_t           fingerprint = 0;
    friend bool operator==(const ClimbingCandidateKey&, const ClimbingCandidateKey&) noexcept = default;
};

/** @brief Creates the opaque key for one candidate at its deterministic sorted rank. */
[[nodiscard]] ClimbingCandidateKey makeClimbingCandidateKey(const ClimbingCandidate& candidate,
                                                             std::uint32_t sortedRank) noexcept;

/** @brief Client-to-server request containing selection identity only, never a world-space target. */
struct ClimbingPredictionRequest {
    /** @brief Canonical cross-process schema id. */
    static constexpr std::string_view SchemaId = "evengine.climbing-prediction-request";
    /** @brief Current request schema version. */
    static constexpr std::int64_t SchemaVersion = 1;

    ClimbingPredictionSequence sequence = ClimbingPredictionSequence::zero();
    eve::SimulationTick        clientTick = eve::SimulationTick::zero();
    ClimbingCandidateKey       candidate;
    /** @brief Unknown schema fields retained across decode and encode. */
    eve::Value::Object         extensionMetadata;
};

/** @brief Named server-authoritative outcome of one prediction request. */
enum class ClimbingPredictionDisposition : std::uint8_t { Accepted, Corrected, Rejected };

/** @brief Stable reason for a non-accepted prediction decision. */
enum class ClimbingPredictionReason : std::uint8_t {
    None,
    CandidateMismatch,
    NoCandidate,
    TickTooFarAhead,
    RuntimeBusy,
    InvalidRequest
};

/** @brief Server-to-client decision carrying the authoritative runtime snapshot for reconciliation. */
struct ClimbingPredictionDecision {
    /** @brief Canonical cross-process schema id. */
    static constexpr std::string_view SchemaId = "evengine.climbing-prediction-decision";
    /** @brief Current decision schema version. */
    static constexpr std::int64_t SchemaVersion = 1;

    ClimbingPredictionSequence    sequence = ClimbingPredictionSequence::zero();
    eve::SimulationTick           clientTick = eve::SimulationTick::zero();
    eve::SimulationTick           serverTick = eve::SimulationTick::zero();
    ClimbingPredictionDisposition disposition = ClimbingPredictionDisposition::Rejected;
    ClimbingPredictionReason      reason = ClimbingPredictionReason::InvalidRequest;
    /** @brief Server-selected key; fingerprint zero means no authoritative candidate was available. */
    ClimbingCandidateKey          authoritativeCandidate;
    /** @brief Canonical server runtime state after the decision, used for rollback/replay reconciliation. */
    eve::Value                    authoritativeSnapshot;
    /** @brief Unknown schema fields retained across decode and encode. */
    eve::Value::Object            extensionMetadata;
};

/** @brief Encodes a validated prediction request to its canonical owning Value representation. */
[[nodiscard]] eve::Result<eve::Value> encodeClimbingPredictionRequest(const ClimbingPredictionRequest& request);
/** @brief Decodes a complete request and rejects unknown schema versions transactionally. */
[[nodiscard]] eve::Result<ClimbingPredictionRequest> decodeClimbingPredictionRequest(const eve::Value& value);
/** @brief Encodes a validated authoritative decision to its canonical owning Value representation. */
[[nodiscard]] eve::Result<eve::Value> encodeClimbingPredictionDecision(const ClimbingPredictionDecision& decision);
/** @brief Decodes a complete authoritative decision transactionally. */
[[nodiscard]] eve::Result<ClimbingPredictionDecision> decodeClimbingPredictionDecision(const eve::Value& value);

/** @brief Owning evidence returned by one committed climbing selection transaction. */
struct ClimbingStart {
    ClimbingExecutionId executionId = ClimbingExecutionId::zero();
    ClimbingCandidate   candidate;
};

/** @brief Execution lifecycle visible to gameplay and animation adapters. */
enum class ClimbingPhase : std::uint8_t {
    Idle,
    Requested,
    Aligning,
    Launching,
    Climbing,
    Landing,
    Recovering,
    Hanging,
    Dropping,
    Completed,
    Cancelled,
    Failed,
    Balanced,
    Swinging
};

/** @brief Stable semantic event emitted by the authoritative climbing lifecycle. */
enum class ClimbingEventKind : std::uint8_t {
    Started,
    AnchorTransitionStarted,
    AnchorReached,
    ContactLeftHand,
    ContactRightHand,
    Landed,
    Hanging,
    Dropped,
    Completed,
    Cancelled,
    Failed
};

/** @brief Stable animation notify semantics accepted from a validated climbing clip. */
enum class ClimbingNotifyKind : std::uint8_t {
    ContactLeftHand,
    ContactRightHand,
    CollisionCompact,
    BranchOpen,
    BranchClose,
    Land
};

/** @brief Owning event record; dispatch to scripts only after the simulation phase. */
struct ClimbingEvent {
    ClimbingEventKind   kind = ClimbingEventKind::Started;
    std::string         actionId;
    eve::SimulationTick tick        = eve::SimulationTick::zero();
    ClimbingExecutionId executionId = ClimbingExecutionId::zero();
    /** @brief Owning immutable action metadata copied when the event is enqueued. */
    std::vector<std::string> metadata;
};

/** @brief Per-tick authored animation motion supplied before climbing warp and collision. */
struct ClimbingMotionInput {
    Vec3  rootTranslation;
    Vec3  facing{0.f, 0.f, 1.f};
    Vec3  pelvisOffset;
    float rootYawRadians = 0.f;
    bool  hasRootMotion  = false;
    /** @brief Owning semantic notifies crossed by the animation player during this exact simulation step. */
    std::vector<ClimbingNotifyKind> notifies;
};

/** @brief Owning output of one execution tick. */
struct ClimbingAdvance {
    ClimbingPhase              phase = ClimbingPhase::Idle;
    std::string                actionId;
    Vec3                       feet;
    Vec3                       desiredDelta;
    Vec3                       actualDelta;
    Vec3                       warpResidual;
    float                      normalizedTime = 0.f;
    bool                       constrained    = false;
    bool                       grounded       = false;
    HangSupport                support        = HangSupport::None;
    Vec3                       leftHandAnchor;
    Vec3                       rightHandAnchor;
    float                      contactWeight = 0.f;
    float                      leftHandWeight = 0.f;
    float                      rightHandWeight = 0.f;
    bool                       compactCollisionRequested = false;
    bool                       compactCollisionActive = false;
    bool                       branchWindowOpen = false;
    /** @brief Active authored combo tag; empty for notify-only or terminal branch availability. */
    std::string                branchComboTag;
    ClimbingExecutionId        executionId   = ClimbingExecutionId::zero();
    Vec3                       appliedWarp;
    float                      desiredYawDelta = 0.f;
    /** @brief Owning presentation hint; consumers may ignore it without affecting simulation. */
    std::string                cameraCueProfile;
    /** @brief Action-local camera cue resolved from the immutable execution definition. */
    std::string                cameraCue;
    /** @brief Stable animation clip id resolved from the immutable action definition. */
    std::string                animationClipId;
    /** @brief Stable external animation-graph node id; the game adapter resolves it to its graph handle. */
    std::string                animationGraphNodeId;
    /** @brief Whether the selected action requests the mirrored presentation variant. */
    bool                       animationMirrored = false;
    /** @brief Left-foot constraint weight resolved from the immutable action contact windows. */
    float                      leftFootWeight = 0.f;
    /** @brief Right-foot constraint weight resolved from the immutable action contact windows. */
    float                      rightFootWeight = 0.f;
    /** @brief Pelvis constraint weight resolved from the immutable action contact windows. */
    float                      pelvisWeight = 0.f;
    /** @brief Authoritative velocity policy output for the receiving locomotion owner. */
    Vec3                       terminalVelocity;
    /** @brief Whether terminalVelocity must replace velocity derived from this tick's displacement. */
    bool                       hasTerminalVelocity = false;
};

/** @brief Runtime policy controlling optional owning diagnostic capture. */
enum class ClimbingDebugCapture : std::uint8_t { Disabled, Enabled };

/** @brief Geometry-query evidence suitable for an engine debug-draw adapter. */
struct ClimbingDebugQuery {
    std::string actionId;
    std::string code;
    Vec3        start;
    Vec3        end;
    float       radius = 0.f;
    float       height = 0.f;
};

/** @brief Quantized selection cost and hard-decision evidence for one action definition. */
struct ClimbingCandidateEvidence {
    std::string  actionId;
    std::string  code;
    std::int64_t biasCost     = 0;
    std::int64_t heightCost   = 0;
    std::int64_t distanceCost = 0;
    std::int64_t totalCost    = 0;
};

/** @brief One planned-versus-actual capsule sample from an authoritative execution tick. */
struct ClimbingMotionEvidence {
    eve::SimulationTick tick = eve::SimulationTick::zero();
    Vec3                plannedFeet;
    Vec3                actualFeet;
    Vec3                residual;
    float               capsuleHeight = 0.f;
    bool                constrained = false;
};

/** @brief Bounded owning debug snapshot, independent of Physics query caches. */
struct ClimbingDebugSnapshot {
    ClimbingPhase                  phase = ClimbingPhase::Idle;
    std::vector<ClimbingCandidate> candidates;
    Vec3                           accumulatedWarpResidual;
    std::uint32_t                  broadPhaseQueryCount = 0;
    std::uint32_t                  broadPhaseHitCount = 0;
    std::uint32_t                  queryCount = 0;
    std::string                    terminalCode;
    ClimbingExecutionId            executionId = ClimbingExecutionId::zero();
    std::vector<ClimbingDebugQuery> queries;
    std::vector<ClimbingCandidateEvidence> evidence;
    std::vector<ClimbingMotionEvidence> motion;
};

/**
 * @brief Owner-thread climbing planner and executor for one character.
 *
 * The runtime owns definitions and execution state, but never retains a World3D pointer. Every
 * physics call accepts a borrowed world for that synchronous call and validates its runtime handle.
 */
class ClimbingRuntime {
public:
    /** @brief Stable schema id for owning runtime snapshots. */
    static constexpr std::string_view SnapshotSchemaId = "evengine.climbing-runtime";
    /** @brief Current runtime snapshot version; v3 is N-1 and historical v2/v1/v0 remain migratable. */
    static constexpr std::int64_t SnapshotSchemaVersion = 4;
    /** @brief Hard bound for undelivered post-simulation events owned by one runtime. */
    static constexpr std::size_t PendingEventCapacity = 64;

    ClimbingRuntime() = default;
    ClimbingRuntime(const ClimbingRuntime&) = delete;
    ClimbingRuntime& operator=(const ClimbingRuntime&) = delete;
    ClimbingRuntime(ClimbingRuntime&&) noexcept = default;
    ClimbingRuntime& operator=(ClimbingRuntime&&) noexcept = default;
    /** @brief Releases any live anchor reservation before runtime storage is destroyed. */
    ~ClimbingRuntime() noexcept;

    /** @brief Replaces the runtime profile after validating all dimensions and action definitions. */
    [[nodiscard]] eve::Result<void> setProfile(ClimbingProfile profile);
    /**
     * @brief Atomically publishes a validated profile while pinning any active execution to its old snapshot.
     * @return Applied on publication; failure leaves both the active execution and current profile unchanged.
     */
    [[nodiscard]] eve::Result<void> reloadProfile(ClimbingProfile profile);
    /** @brief Parses and installs a canonical profile JSON document transactionally. */
    [[nodiscard]] eve::Result<void> setProfileJson(std::string_view json);
    /** @brief Parses and hot-reloads a canonical profile JSON document transactionally. */
    [[nodiscard]] eve::Result<void> reloadProfileJson(std::string_view json);
    /** @brief Adds or replaces one action definition by stable id. */
    [[nodiscard]] eve::Result<void> upsertAction(ClimbingActionDefinition action);
    /**
     * @brief Validates and records an action's clip notify contract without retaining the clip.
     * @param actionId Registered stable definition id.
     * @param clip Borrowed synchronously; the runtime stores only validation evidence.
     */
    [[nodiscard]] eve::Result<void> validateAnimationBinding(std::string_view           actionId,
                                                             const animation::AnimClip& clip);
    /**
     * @brief Probes, validates, and deterministically sorts all matching candidates.
     * @param world Borrowed for this synchronous owner-thread call only; never retained.
     * @return Owning candidate vector sorted by score and stable action id.
     * @thread Call on the runtime and physics world's shared simulation thread.
     * @reentrancy Does not invoke callbacks or scripts.
     */
    [[nodiscard]] eve::Result<ClimbingCandidateSet> probe(physics::World3D& world,
                                                          const ClimbingPose& pose) const;
    /**
     * @brief Probe into caller-retained fixed storage; successful calls atomically replace output.
     * @remarks Reusing output avoids candidate-container and action-id allocation after warmup.
     */
    [[nodiscard]] eve::Result<void> probeInto(physics::World3D& world, const ClimbingPose& pose,
                                               ClimbingCandidateSet& output,
                                               eve::SimulationTick tick = eve::SimulationTick::zero()) const;
    /** @brief Begins the best current candidate atomically, or returns NotFound without mutation. */
    [[nodiscard]] eve::Result<ClimbingCandidate> tryBegin(physics::World3D& world, const ClimbingPose& pose,
                                                          eve::SimulationTick tick);
    /**
     * @brief Re-probes authoritative state and commits only the exact server-matched predicted candidate.
     * @param pose Server-owned character pose; no pose or target position is read from the request.
     * @param serverTick Authoritative execution tick used for commit and snapshot.
     * @param maxClientTickLead Maximum accepted positive client tick lead.
     * @return A protocol decision with an authoritative snapshot; transport/probe/serialization faults fail Result.
     */
    [[nodiscard]] eve::Result<ClimbingPredictionDecision> tryBeginPredicted(
        physics::World3D& world, const ClimbingPose& pose, const ClimbingPredictionRequest& request,
        eve::SimulationTick serverTick, std::uint64_t maxClientTickLead);
    /**
     * @brief Begins an authored graph anchor using the same authoritative execution lifecycle.
     * @param graph Module-owned generation-qualified graph instance.
     * @param node Current-generation anchor node reference.
     * @param agentId Stable non-zero character identity used for occupancy arbitration.
     * @param actionId Registered ledge-grab or ladder-mount definition used to approach the anchor.
     */
    [[nodiscard]] eve::Result<ClimbingCandidate> tryBeginAnchor(
        physics::World3D& world, ClimbingAnchorGraphHandleRef graph, const ClimbingAnchorNodeRef& node,
        ClimbingAnchorAgentId agentId, std::string_view actionId, const ClimbingPose& pose,
        eve::SimulationTick tick);
    /**
     * @brief Atomically moves a hanging graph execution to a reserved adjacent node.
     * @param world Borrowed synchronously for target resolution and capsule clearance.
     * @param target Current-generation destination reference from the same graph.
     * @param edgeKind Required authored edge semantic.
     * @param actionId Registered graph-only action definition matching the edge.
     * @param tick New simulation tick for the branch commit.
     */
    [[nodiscard]] eve::Result<void> transitionAnchor(physics::World3D& world,
                                                      const ClimbingAnchorNodeRef& target,
                                                      ClimbingAnchorEdgeKind edgeKind,
                                                      std::string_view actionId,
                                                      eve::SimulationTick tick);
    /**
     * @brief Advances the active trajectory through the world's capsule mover.
     * @param world Borrowed for this synchronous owner-thread call only; never retained.
     * @param step Injected deterministic simulation step; duplicate/non-increasing ticks are rejected.
     */
    [[nodiscard]] eve::Result<ClimbingAdvance> advance(physics::World3D& world, eve::SimulationStep step);
    /** @brief Advances with an authored root-motion delta before warp and capsule collision. */
    [[nodiscard]] eve::Result<ClimbingAdvance> advance(physics::World3D& world, eve::SimulationStep step,
                                                       const ClimbingMotionInput& motion);
    /** @brief Enters an explicit physics-constrained drop from a hanging pose. */
    [[nodiscard]] eve::Result<void> drop(eve::SimulationTick tick);
    /** @brief Branches from hanging into the best compatible mantle/climb-up definition. */
    [[nodiscard]] eve::Result<void> climbUp(eve::SimulationTick tick);
    /**
     * @brief Cancels an active execution with an injected tick and stable, strongly typed reason.
     * @param reason Authoritative terminal reason.
     * @param tick Simulation tick recorded on the queued cancellation event.
     */
    [[nodiscard]] eve::Result<void> cancel(ClimbingCancelReason reason, eve::SimulationTick tick);
    /**
     * @brief Atomically takes all events queued by simulation operations in stable production order.
     * @return Owning event batch; callbacks and scripts may be invoked only after this method returns.
     * @thread Call on the runtime owner thread after the simulation phase.
     * @reentrancy The returned batch is detached before user code can run; events produced reentrantly remain queued.
     */
    [[nodiscard]] eve::Result<std::vector<ClimbingEvent>> drainEvents();
    /** @brief Number of undelivered events currently owned by this runtime. */
    [[nodiscard]] std::size_t pendingEventCount() const noexcept { return pendingEvents_.size(); }
    /** @brief Captures the latest bounded owning diagnostics without querying Physics. */
    [[nodiscard]] ClimbingDebugSnapshot inspect() const;
    /** @brief Return allocation-free counters for the latest probe or active execution workload. */
    [[nodiscard]] ClimbingRuntimeCounters counters() const noexcept { return lastCounters_; }
    /** @brief Return the runtime-owned fixed profiler ring. */
    [[nodiscard]] const ClimbingTelemetryBuffer& telemetry() const noexcept { return telemetry_; }
    /** @brief Return an owning ordinary-locomotion policy snapshot safe across later profile reloads. */
    [[nodiscard]] ClimbingLocomotionPolicy locomotionPolicy() const noexcept;
    /** @brief Clear profiler samples without affecting simulation state or diagnostic evidence. */
    void clearTelemetry() noexcept { telemetry_.clear(); }
    /** @brief Record one ordinary locomotion tick and its bounded Physics mover work. */
    void recordOrdinaryTick(eve::SimulationTick tick, std::uint32_t queryCount = 0,
                            std::uint32_t moverIterations = 0,
                            std::uint64_t elapsedNanoseconds = 0) noexcept;
    /** @brief Enables or disables bounded diagnostic capture; disabling immediately releases captured data. */
    void setDebugCapture(ClimbingDebugCapture capture) noexcept;
    /** @brief Captures deterministic runtime state; debug candidates and final bone poses are excluded. */
    [[nodiscard]] eve::Result<eve::Value> snapshot() const;
    /** @brief Serializes the canonical runtime snapshot as JSON. */
    [[nodiscard]] eve::Result<std::string> snapshotJson() const;
    /**
     * @brief Restores a fully parsed candidate atomically after revalidating all process-local Physics links.
     * @param value Owning schema v4, N-1 v3, or migratable historical v2/v1/v0 snapshot payload.
     * @param world Borrowed synchronously for link validation and never retained.
     */
    [[nodiscard]] eve::Result<void> restore(const eve::Value& value, physics::World3D& world);
    /** @brief Parses and atomically restores a canonical runtime snapshot JSON document. */
    [[nodiscard]] eve::Result<void> restoreJson(std::string_view json, physics::World3D& world);
    /** @brief Current lifecycle phase. */
    [[nodiscard]] ClimbingPhase phase() const noexcept { return phase_; }
    /** @brief Current execution identity, or zero when no execution is active or retained. */
    [[nodiscard]] ClimbingExecutionId executionId() const noexcept {
        return execution_ ? execution_->executionId : ClimbingExecutionId::zero();
    }
    /** @brief Current monotonically increasing profile/action definition generation. */
    [[nodiscard]] std::uint64_t definitionGeneration() const noexcept { return definitionGeneration_; }
    /** @brief Returns the current graph node reference, or NotFound when execution is probe-only. */
    [[nodiscard]] eve::Result<ClimbingAnchorNodeRef> currentAnchor() const;

private:
    friend class ClimbingSelectionSystem;
    friend class ClimbingServiceSelectionSystem;

    struct PreparedBegin {
        ClimbingExecutionId      executionId = ClimbingExecutionId::zero();
        ClimbingCandidate        candidate;
        ClimbingActionDefinition action;
        Vec3                     startFeet;
        eve::SimulationTick      tick = eve::SimulationTick::zero();
        bool                     conditionsSatisfied = false;
    };

    struct Execution {
        ClimbingExecutionId executionId = ClimbingExecutionId::zero();
        std::uint64_t       definitionGeneration = 1;
        ClimbingCandidate   candidate;
        /** @brief Immutable action snapshot pinned when this execution or branch begins. */
        ClimbingActionDefinition action;
        Vec3                     startFeet;
        Vec3                     currentFeet;
        Vec3                     lastPlannedFeet;
        eve::Duration            elapsed  = eve::Duration::zero();
        eve::Duration            duration = eve::Duration::zero();
        eve::SimulationTick      lastTick = eve::SimulationTick::zero();
        Vec3                     velocity;
        Vec3                     accumulatedResidual;
        float                    horizontalWarpUsed  = 0.f;
        float                    verticalWarpUsed    = 0.f;
        float                    facingWarpUsed      = 0.f;
        bool                     leftContactEmitted  = false;
        bool                     rightContactEmitted = false;
        bool                     landContactReleased = false;
        bool                     compactCollisionActive = false;
        bool                     branchWindowOpen = false;
        ClimbingAnchorGraphHandleRef anchorGraph;
        ClimbingAnchorNodeRef        anchorNode;
        ClimbingAnchorReservation    anchorReservation;
    };

    [[nodiscard]] eve::Result<PreparedBegin> prepareBegin(physics::World3D& world, const ClimbingPose& pose,
                                                          eve::SimulationTick tick);
    [[nodiscard]] eve::Result<PreparedBegin> prepareBeginCandidate(physics::World3D& world,
                                                                   const ClimbingPose& pose,
                                                                   eve::SimulationTick tick,
                                                                   ClimbingCandidate candidate);
    [[nodiscard]] eve::Result<ClimbingStart> commitBegin(PreparedBegin prepared);
    [[nodiscard]] eve::Result<void> publishProfile(ClimbingProfile profile, bool allowActiveExecution);
    [[nodiscard]] eve::Result<void> requireEventCapacity(std::size_t count, eve::SimulationTick tick) const;
    [[nodiscard]] eve::Result<void> releaseAnchorReservation();
    void enqueueEvent(ClimbingEvent event);

    ClimbingProfile                        profile_;
    ClimbingPhase                          phase_ = ClimbingPhase::Idle;
    std::optional<Execution>               execution_;
    mutable std::vector<ClimbingCandidate> lastCandidates_;
    mutable ClimbingCandidateSet            probeScratch_;
    mutable std::uint32_t                  lastQueryCount_ = 0;
    mutable ClimbingRuntimeCounters         lastCounters_;
    mutable ClimbingTelemetryBuffer         telemetry_;
    ClimbingDebugCapture                   debugCapture_ = ClimbingDebugCapture::Enabled;
    mutable std::vector<ClimbingDebugQuery> lastDebugQueries_;
    mutable std::vector<ClimbingCandidateEvidence> lastEvidence_;
    std::vector<ClimbingMotionEvidence>     motionEvidence_;
    std::string                            terminalCode_;
    std::unordered_set<std::string>        validatedAnimationActions_;
    std::string                            previousActionId_;
    std::uint64_t                          nextExecutionId_ = 1;
    std::uint64_t                          definitionGeneration_ = 1;
    std::vector<ClimbingEvent>             pendingEvents_;
};

/** @brief Handle domain for module-owned climbing runtimes. */
struct ClimbingRuntimeHandleTag {};
/** @brief Generation- and module-epoch-qualified climbing runtime reference. */
using ClimbingRuntimeHandleRef = eve::script::RuntimeHandleRef<ClimbingRuntimeHandleTag>;

/** @brief Script-facing factory and owner for independent climbing runtimes. */
class Climbing : public Module {
public:
    Module_REG(Climbing);
    /** @brief Creates a module-owned runtime and returns its generation-qualified reference. */
    [[nodiscard]] static eve::Result<ClimbingRuntimeHandleRef> newRuntime();
    /** @brief Resolves a live runtime as a non-owning observation. */
    [[nodiscard]] static eve::script::Borrowed<ClimbingRuntime> resolve(ClimbingRuntimeHandleRef reference) noexcept;
    /** @brief Releases a module-owned runtime and invalidates its reference. */
    [[nodiscard]] static eve::Result<void> release(ClimbingRuntimeHandleRef reference);
    /** @brief Reports whether a runtime reference is stale. */
    [[nodiscard]] static bool isStale(ClimbingRuntimeHandleRef reference) noexcept;
    /** @brief Creates a module-owned graph instance bound to one live Physics body. */
    [[nodiscard]] static eve::Result<ClimbingAnchorGraphHandleRef> newAnchorGraph(
        ClimbingAnchorGraphDefinition graph, physics::World3D& world, physics::PhysicsBodyHandle body);
    /** @brief Resolves a live graph instance as a synchronous non-owning observation. */
    [[nodiscard]] static eve::script::Borrowed<ClimbingAnchorGraphInstance> resolveAnchorGraph(
        ClimbingAnchorGraphHandleRef reference) noexcept;
    /** @brief Releases a module-owned graph instance and all of its reservations. */
    [[nodiscard]] static eve::Result<void> releaseAnchorGraph(ClimbingAnchorGraphHandleRef reference);
    /** @brief Reports whether a graph instance reference is stale. */
    [[nodiscard]] static bool isAnchorGraphStale(ClimbingAnchorGraphHandleRef reference) noexcept;

private:
    eve::script::RuntimeObjectRegistry<ClimbingAnchorGraphInstance, ClimbingAnchorGraphHandleTag> anchorGraphs_;
    eve::script::RuntimeObjectRegistry<ClimbingRuntime, ClimbingRuntimeHandleTag> runtimes_;
};

}  // namespace eve::climbing
