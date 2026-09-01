#pragma once

/**
 * @file ClimbingAnchorGraph.h
 * @brief Versioned body-local climbing topology, dynamic resolution, and deterministic occupancy.
 */

#include "climbing/ClimbingPrimitives.h"
#include "common/Result.h"
#include "common/SquirrelOwnership.h"
#include "common/StrongUint64.h"
#include "common/Value.h"
#include "physics/PhysicsHandles.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace eve::physics {
class World3D;
}

namespace eve::climbing {

/** @brief Authored semantic of one explicit climbing anchor. */
enum class ClimbingAnchorKind : std::uint8_t { Ledge, CornerInner, CornerOuter, LadderRung, Pole, Beam, Bar };

/** @brief Allowed authored transition between two explicit anchors. */
enum class ClimbingAnchorEdgeKind : std::uint8_t {
    Shimmy,
    Corner,
    Jump,
    Drop,
    Mount,
    Dismount,
    Climb,
    Balance,
    Swing
};

/** @brief One body-local anchor node with deterministic occupancy slots. */
struct ClimbingAnchorNodeDefinition {
    std::string              id;
    ClimbingAnchorKind       kind = ClimbingAnchorKind::Ledge;
    Vec3                     localPosition;
    Vec3                     localNormal{0.f, 0.f, -1.f};
    Vec3                     localTangent{1.f, 0.f, 0.f};
    Vec3                     leftHandSocket;
    Vec3                     rightHandSocket;
    Vec3                     feetSocket;
    std::uint32_t            occupancySlots = 1;
    std::vector<std::string> tags;
    /** @brief Unknown schema fields retained across decode and encode. */
    eve::Value::Object       extensionMetadata;
};

/** @brief Directed authored transition in a climbing anchor graph. */
struct ClimbingAnchorEdgeDefinition {
    std::string                from;
    std::string                to;
    ClimbingAnchorEdgeKind     kind = ClimbingAnchorEdgeKind::Shimmy;
    bool                       bidirectional = false;
    std::vector<std::string>   requiredTags;
    /** @brief Unknown schema fields retained across decode and encode. */
    eve::Value::Object         extensionMetadata;
};

/** @brief Versioned graph asset; topology is authoritative while collision remains owned by Physics. */
struct ClimbingAnchorGraphDefinition {
    /** @brief Canonical schema id. */
    static constexpr std::string_view SchemaId = "evengine.climbing-anchor-graph";
    /** @brief Current schema version. */
    static constexpr std::int64_t SchemaVersion = 2;

    std::string                               id;
    std::string                               sourceGeometryContentId;
    std::string                               buildSettingsHash;
    std::vector<ClimbingAnchorNodeDefinition> nodes;
    std::vector<ClimbingAnchorEdgeDefinition> edges;
    /** @brief Unknown schema fields retained across decode and encode. */
    eve::Value::Object                        extensionMetadata;
};

/** @brief Validates all graph identities, frames, slots, tags, and edge endpoints. */
[[nodiscard]] eve::Result<void> validateClimbingAnchorGraphDefinition(const ClimbingAnchorGraphDefinition& graph);
/** @brief Encodes a validated graph to its canonical owning Value representation. */
[[nodiscard]] eve::Result<eve::Value> encodeClimbingAnchorGraphDefinition(const ClimbingAnchorGraphDefinition& graph);
/** @brief Decodes and validates a complete owning graph candidate transactionally. */
[[nodiscard]] eve::Result<ClimbingAnchorGraphDefinition> decodeClimbingAnchorGraphDefinition(const eve::Value& value);

namespace detail {
struct ClimbingAnchorReservationIdTag {};
struct ClimbingAnchorAgentIdTag {};
}

/** @brief Stable non-zero identity of one live anchor-slot reservation. */
using ClimbingAnchorReservationId = eve::detail::StrongUint64<detail::ClimbingAnchorReservationIdTag>;
/** @brief Stable non-zero identity of the character/agent that owns graph occupancy. */
using ClimbingAnchorAgentId = eve::detail::StrongUint64<detail::ClimbingAnchorAgentIdTag>;

/** @brief Compound occupancy owner; execution ids are only unique inside one agent/runtime. */
struct ClimbingAnchorOccupant {
    ClimbingAnchorAgentId agentId = ClimbingAnchorAgentId::zero();
    ClimbingExecutionId   executionId = ClimbingExecutionId::zero();
    friend bool operator==(const ClimbingAnchorOccupant&, const ClimbingAnchorOccupant&) noexcept = default;
    friend auto operator<=>(const ClimbingAnchorOccupant&, const ClimbingAnchorOccupant&) noexcept = default;
};

/** @brief Generation-qualified reference to an authored anchor node. */
struct ClimbingAnchorNodeRef {
    std::string   graphId;
    std::string   nodeId;
    std::uint64_t graphGeneration = 0;
};

/** @brief Owning reservation credential; release validates every field. */
struct ClimbingAnchorReservation {
    ClimbingAnchorReservationId id = ClimbingAnchorReservationId::zero();
    std::uint64_t               graphGeneration = 0;
    std::uint64_t               claimGeneration = 0;
    std::string                 nodeId;
    std::uint32_t               slot = 0;
    ClimbingAnchorOccupant      occupant;
};

/** @brief Policy controlling whether deterministic route planning may cross fully occupied nodes. */
enum class ClimbingRouteOccupancyPolicy : std::uint8_t { Ignore, AvoidFull };

/** @brief Read-only route request over one current anchor-graph generation. */
struct ClimbingAnchorRouteRequest {
    ClimbingAnchorNodeRef          start;
    ClimbingAnchorNodeRef          goal;
    ClimbingRouteOccupancyPolicy   occupancyPolicy = ClimbingRouteOccupancyPolicy::AvoidFull;
    /** @brief Existing owner allowed to traverse its own current reservation; both ids must be zero or non-zero. */
    ClimbingAnchorOccupant         requester;
    /** @brief Empty means every authored edge kind is allowed. */
    std::vector<ClimbingAnchorEdgeKind> allowedEdgeKinds;
    /** @brief Stable capability tags available to the route consumer; every edge requirement must be present. */
    std::vector<std::string>            availableTags;
    /** @brief Deterministic safety bound; planning fails instead of returning a partial route. */
    std::uint32_t                  maxVisitedNodes = 4096;
};

/** @brief One owning directed edge in a planned anchor route. */
struct ClimbingAnchorRouteStep {
    ClimbingAnchorNodeRef      from;
    ClimbingAnchorNodeRef      to;
    ClimbingAnchorEdgeKind     kind = ClimbingAnchorEdgeKind::Shimmy;
};

/** @brief Shortest deterministic route pinned to one graph generation. */
struct ClimbingAnchorRoute {
    std::string                         graphId;
    std::uint64_t                       graphGeneration = 0;
    std::vector<ClimbingAnchorNodeRef>  nodes;
    std::vector<ClimbingAnchorRouteStep> steps;
};

/** @brief World-space projection of a body-local anchor at one synchronous resolve point. */
struct ResolvedClimbingAnchorNode {
    ClimbingAnchorNodeRef    reference;
    ClimbingAnchorKind       kind = ClimbingAnchorKind::Ledge;
    physics::PhysicsBodyHandle body = physics::PhysicsBodyHandle::invalid();
    Vec3                     position;
    Vec3                     normal;
    Vec3                     tangent;
    Vec3                     leftHandSocket;
    Vec3                     rightHandSocket;
    Vec3                     feetSocket;
    Vec3                     pointVelocity;
    std::vector<std::string> tags;
};

/** @brief Atomic graph-reload outcome and owners whose old-generation reservations were invalidated. */
struct ClimbingAnchorGraphReload {
    std::uint64_t                    oldGeneration = 0;
    std::uint64_t                    newGeneration = 0;
    std::vector<ClimbingAnchorOccupant> invalidatedOccupants;
};

/**
 * @brief Runtime instance binding one immutable graph generation to one Physics body.
 *
 * The instance owns topology and reservations, but stores only generation-checked Physics handles.
 * It never retains World3D or Body3D pointers across calls.
 */
class ClimbingAnchorGraphInstance {
public:
    /**
     * @brief Validates and binds a graph snapshot to an existing Physics body.
     * @param graph Owning definition candidate.
     * @param world Borrowed synchronously for link validation only.
     * @param body Generation-checked target body handle.
     */
    [[nodiscard]] static eve::Result<ClimbingAnchorGraphInstance> bind(ClimbingAnchorGraphDefinition graph,
                                                                       physics::World3D& world,
                                                                       physics::PhysicsBodyHandle body);
    /** @brief Returns a current-generation reference for a stable node id. */
    [[nodiscard]] eve::Result<ClimbingAnchorNodeRef> nodeRef(std::string_view nodeId) const;
    /** @brief Resolves a current node from body-local to world space after validating world/body generation. */
    [[nodiscard]] eve::Result<ResolvedClimbingAnchorNode> resolveNode(physics::World3D& world,
                                                                      const ClimbingAnchorNodeRef& reference) const;
    /**
     * @brief Resolves per-tick anchor transforms without copying cold tag metadata.
     * @remarks The returned tags are empty by contract; use resolveNode for authoring/transition validation.
     */
    [[nodiscard]] eve::Result<ResolvedClimbingAnchorNode> resolveNodeKinematics(
        physics::World3D& world, const ClimbingAnchorNodeRef& reference) const;
    /** @brief Returns deterministic outgoing edges for a current-generation node reference. */
    [[nodiscard]] eve::Result<std::vector<ClimbingAnchorEdgeDefinition>> edgesFrom(
        const ClimbingAnchorNodeRef& reference) const;
    /**
     * @brief Finds the shortest occupancy-aware route using stable edge-kind/node-id tie breaking.
     * @return An owning current-generation route; NotFound means no permitted complete route exists.
     * @thread Call on the graph owner thread; this method is read-only and invokes no callbacks.
     */
    [[nodiscard]] eve::Result<ClimbingAnchorRoute> planRoute(const ClimbingAnchorRouteRequest& request) const;
    /** @brief Reserves the lowest free slot on a node for one non-zero agent/execution identity. */
    [[nodiscard]] eve::Result<ClimbingAnchorReservation> reserve(const ClimbingAnchorNodeRef& reference,
                                                                  ClimbingAnchorOccupant occupant);
    /** @brief Validates that a reservation credential still names the exact live slot record. */
    [[nodiscard]] eve::Result<void> validateReservation(const ClimbingAnchorReservation& reservation) const;
    /**
     * @brief Atomically transfers a live slot claim to a newly issued credential.
     * @return A credential with an incremented claim generation; the input credential becomes stale.
     */
    [[nodiscard]] eve::Result<ClimbingAnchorReservation> transferReservation(
        const ClimbingAnchorReservation& reservation);
    /**
     * @brief Claims a snapshot reservation by transfer or exact-slot reacquisition.
     *
     * A still-live matching credential is transferred and invalidated. If its prior owner already
     * released it, the authored slot is reacquired only when still free. A conflicting live claim
     * is never displaced.
     */
    [[nodiscard]] eve::Result<ClimbingAnchorReservation> restoreReservation(
        const ClimbingAnchorReservation& reservation);
    /** @brief Releases exactly the live reservation described by the credential. */
    [[nodiscard]] eve::Result<void> release(const ClimbingAnchorReservation& reservation);
    /** @brief Releases all reservations owned by an agent/execution pair and returns the released count. */
    [[nodiscard]] eve::Result<std::uint32_t> releaseOccupant(ClimbingAnchorOccupant occupant);
    /**
     * @brief Atomically publishes a validated graph generation and invalidates old-generation reservations.
     * @return Sorted unique agent/execution identities that the caller must safely cancel or migrate.
     */
    [[nodiscard]] eve::Result<ClimbingAnchorGraphReload> reload(ClimbingAnchorGraphDefinition graph);
    /** @brief Current monotonically increasing topology generation. */
    [[nodiscard]] std::uint64_t generation() const noexcept { return generation_; }
    /** @brief Target Physics body handle; stale detection occurs during resolve. */
    [[nodiscard]] physics::PhysicsBodyHandle body() const noexcept { return body_; }
    /** @brief Number of live reservations in this graph instance. */
    [[nodiscard]] std::size_t reservationCount() const noexcept { return reservations_.size(); }

private:
    struct ReservationRecord {
        std::string         nodeId;
        std::uint32_t       slot = 0;
        ClimbingAnchorOccupant occupant;
        std::uint64_t       claimGeneration = 1;
    };

    /**
     * @brief Finds a node in the instance-owned immutable graph snapshot.
     * @return Borrowed pointer valid only until this instance is reloaded or destroyed; never exposed publicly.
     * @lifetime The pointee is owned by this instance and must not escape the synchronous private call.
     */
    const ClimbingAnchorNodeDefinition* findNode(std::string_view nodeId) const noexcept;

    ClimbingAnchorGraphDefinition                                      graph_;
    physics::PhysicsWorldHandle                                        world_ = physics::PhysicsWorldHandle::invalid();
    physics::PhysicsBodyHandle                                         body_ = physics::PhysicsBodyHandle::invalid();
    std::uint64_t                                                      generation_ = 1;
    ClimbingAnchorReservationId                                        nextReservationId_{1};
    std::unordered_map<ClimbingAnchorReservationId, ReservationRecord> reservations_;
};

/** @brief Handle domain for module-owned anchor graph runtime instances. */
struct ClimbingAnchorGraphHandleTag {};
/** @brief Generation- and module-epoch-qualified anchor graph instance reference. */
using ClimbingAnchorGraphHandleRef = eve::script::RuntimeHandleRef<ClimbingAnchorGraphHandleTag>;

}  // namespace eve::climbing
