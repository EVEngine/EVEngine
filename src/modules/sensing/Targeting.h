#pragma once

/**
 * @file Targeting.h
 * @brief Coordinate-safe targeting protocol shared by sensing and gameplay adapters.
 *
 * This header deliberately contains no physics, scene, UI, or rendering types.
 * Candidate discovery and line-of-sight are consumer-owned capabilities; a
 * domain-specific skill, weapon, card, or RTS command remains responsible for
 * choosing a primary target from the returned candidate set.
 */

#include "common/Capability.h"
#include "common/Identity.h"
#include "common/Result.h"
#include "common/SubjectRef.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace eve::sensing {

/** @brief Coordinate spaces are not interchangeable, even when dimensions match. */
enum class CoordinateSpace : std::uint8_t {
    World2D,
    World3D,
    Grid2D,
    Grid3D,
};

/** @brief Returns the stable protocol name for a coordinate space. */
[[nodiscard]] std::string_view coordinateSpaceName(CoordinateSpace space) noexcept;

/** @brief Writes the stable coordinate-space protocol name. */
inline std::ostream& operator<<(std::ostream& stream, CoordinateSpace space) {
    return stream << coordinateSpaceName(space);
}

/** @brief Stable relationship class used by generic domain constraints. */
enum class TargetDomain : std::uint8_t {
    Any,
    Self,
    Ally,
    Enemy,
    Neutral,
};

/** @brief Whether a candidate must be visible from the query origin. */
enum class LineOfSightMode : std::uint8_t {
    NotRequired,
    Required,
};

/**
 * @brief Strong reference to a runtime subject.
 *
 * This is a persistent identity, not an ECS entity handle. The nil value is
 * rejected by all targeting operations. There is no implicit conversion to or
 * from PersistentId, which prevents an AssetId/SubjectRef mix at call sites.
 */
/** @brief Compatibility spelling for the common subject reference. */
using SubjectRef = ::eve::SubjectRef;

/**
 * @brief Strong reference to a named spatial zone.
 *
 * ZoneRef names a logical zone; its shape and membership are supplied by a
 * candidate provider. It is intentionally distinct from SubjectRef.
 */
class ZoneRef {
public:
    /** @brief Constructs an invalid zone reference. */
    ZoneRef() = default;

    /**
     * @brief Creates a zone reference from a valid logical ID.
     * @param id Scoped logical zone name.
     * @return The reference, or empty when `id` is invalid.
     */
    [[nodiscard]] static std::optional<ZoneRef> fromLogicalId(LogicalId id);

    /** @brief Returns whether the reference contains a valid logical ID. */
    [[nodiscard]] bool isValid() const noexcept { return id_.isValid(); }

    /** @brief Returns the wrapped logical ID. */
    [[nodiscard]] const LogicalId& logicalId() const noexcept { return id_; }

    friend bool operator==(const ZoneRef&, const ZoneRef&) noexcept = default;

private:
    explicit ZoneRef(LogicalId id) : id_(std::move(id)) {}

    LogicalId id_;
};

/**
 * @brief Finite world-space point tagged as either World2D or World3D.
 *
 * Grid coordinates use GridPoint and cannot be passed as a WorldPoint. The
 * constructors validate finite values and preserve the dimension tag.
 */
class WorldPoint {
public:
    /** @brief Constructs an invalid default point; use world2D/world3D for values. */
    WorldPoint() = default;

    /**
     * @brief Creates a finite 2D world point.
     * @return A checked point or InvalidArgument.
     */
    [[nodiscard]] static Result<WorldPoint> world2D(float x, float y);

    /**
     * @brief Creates a finite 3D world point.
     * @return A checked point or InvalidArgument.
     */
    [[nodiscard]] static Result<WorldPoint> world3D(float x, float y, float z);

    /** @brief Returns the point coordinate space. */
    [[nodiscard]] CoordinateSpace space() const noexcept { return space_; }
    /** @brief Returns the X coordinate in the tagged world space. */
    [[nodiscard]] float x() const noexcept { return x_; }
    /** @brief Returns the Y coordinate in the tagged world space. */
    [[nodiscard]] float y() const noexcept { return y_; }
    /** @brief Returns the Z coordinate; it is zero for World2D. */
    [[nodiscard]] float z() const noexcept { return z_; }
    /** @brief Returns whether the point was constructed with finite coordinates. */
    [[nodiscard]] bool isValid() const noexcept { return valid_; }

    friend bool operator==(const WorldPoint&, const WorldPoint&) noexcept = default;

private:
    WorldPoint(CoordinateSpace space, float x, float y, float z) noexcept
        : space_(space), x_(x), y_(y), z_(z), valid_(true) {}

    CoordinateSpace space_ = CoordinateSpace::World2D;
    float           x_     = 0.f;
    float           y_     = 0.f;
    float           z_     = 0.f;
    bool            valid_ = false;
};

/**
 * @brief Integer grid point tagged as Grid2D or Grid3D.
 *
 * Grid units are cells, not world meters. No implicit conversion to
 * WorldPoint exists; a project-specific map adapter must perform any mapping.
 */
class GridPoint {
public:
    /** @brief Constructs an invalid default grid point. */
    GridPoint() = default;

    /** @brief Creates a 2D integer grid point. */
    [[nodiscard]] static GridPoint grid2D(std::int32_t x, std::int32_t y) noexcept;
    /** @brief Creates a 3D integer grid point. */
    [[nodiscard]] static GridPoint grid3D(std::int32_t x, std::int32_t y, std::int32_t z) noexcept;

    /** @brief Returns the point coordinate space. */
    [[nodiscard]] CoordinateSpace space() const noexcept { return space_; }
    /** @brief Returns the X cell coordinate. */
    [[nodiscard]] std::int32_t x() const noexcept { return x_; }
    /** @brief Returns the Y cell coordinate. */
    [[nodiscard]] std::int32_t y() const noexcept { return y_; }
    /** @brief Returns the Z cell coordinate; it is zero for Grid2D. */
    [[nodiscard]] std::int32_t z() const noexcept { return z_; }
    /** @brief Returns whether the point was constructed by a grid factory. */
    [[nodiscard]] bool isValid() const noexcept { return valid_; }

    friend bool operator==(const GridPoint&, const GridPoint&) noexcept = default;

private:
    GridPoint(CoordinateSpace space, std::int32_t x, std::int32_t y, std::int32_t z) noexcept
        : space_(space), x_(x), y_(y), z_(z), valid_(true) {}

    CoordinateSpace space_ = CoordinateSpace::Grid2D;
    std::int32_t    x_     = 0;
    std::int32_t    y_     = 0;
    std::int32_t    z_     = 0;
    bool            valid_ = false;
};

/** @brief Either a world point or a grid point, with the tag retained. */
using TargetLocation = std::variant<WorldPoint, GridPoint>;

/**
 * @brief A world-space area constraint.
 *
 * Circle/box are 2D and sphere/box3D are 3D. Factory methods reject a point
 * from the wrong coordinate space, so a 2D area cannot accidentally consume a
 * 3D point or a grid point.
 */
class WorldArea {
public:
    enum class Shape : std::uint8_t { Circle2D, Box2D, Sphere3D, Box3D };

    /** @brief Constructs an invalid default area. */
    WorldArea() = default;
    /** @brief Creates a finite non-negative 2D world circle. */
    [[nodiscard]] static Result<WorldArea> circle2D(WorldPoint center, float radius);
    /** @brief Creates an axis-aligned 2D world box from inclusive corners. */
    [[nodiscard]] static Result<WorldArea> box2D(WorldPoint minimum, WorldPoint maximum);
    /** @brief Creates a finite non-negative 3D world sphere. */
    [[nodiscard]] static Result<WorldArea> sphere3D(WorldPoint center, float radius);
    /** @brief Creates an axis-aligned 3D world box from inclusive corners. */
    [[nodiscard]] static Result<WorldArea> box3D(WorldPoint minimum, WorldPoint maximum);

    /** @brief Returns the area shape. */
    [[nodiscard]] Shape shape() const noexcept { return shape_; }
    /** @brief Returns the area coordinate space. */
    [[nodiscard]] CoordinateSpace space() const noexcept { return space_; }
    /** @brief Returns whether this area is valid. */
    [[nodiscard]] bool isValid() const noexcept { return valid_; }
    /** @brief Tests a world point without converting coordinate spaces. */
    [[nodiscard]] bool contains(WorldPoint point) const noexcept;

private:
    WorldArea(Shape shape, WorldPoint first, WorldPoint second, float radius) noexcept
        : shape_(shape), space_(first.space()), first_(first), second_(second), radius_(radius), valid_(true) {}

    Shape           shape_ = Shape::Circle2D;
    CoordinateSpace space_ = CoordinateSpace::World2D;
    WorldPoint      first_;
    WorldPoint      second_;
    float           radius_ = 0.f;
    bool            valid_  = false;
};

/**
 * @brief An integer grid area constraint, kept separate from WorldArea.
 *
 * Grid distances and world distances have different units. A grid area must
 * be authored and queried in the same Grid2D or Grid3D space.
 */
class GridArea {
public:
    enum class Shape : std::uint8_t { Box2D, Box3D };

    /** @brief Constructs an invalid default grid area. */
    GridArea() = default;
    /** @brief Creates an inclusive 2D grid rectangle. */
    [[nodiscard]] static Result<GridArea> box2D(GridPoint minimum, GridPoint maximum);
    /** @brief Creates an inclusive 3D grid box. */
    [[nodiscard]] static Result<GridArea> box3D(GridPoint minimum, GridPoint maximum);

    /** @brief Returns the area shape. */
    [[nodiscard]] Shape shape() const noexcept { return shape_; }
    /** @brief Returns the area coordinate space. */
    [[nodiscard]] CoordinateSpace space() const noexcept { return space_; }
    /** @brief Returns whether this area is valid. */
    [[nodiscard]] bool isValid() const noexcept { return valid_; }
    /** @brief Tests a grid point without converting coordinate spaces. */
    [[nodiscard]] bool contains(GridPoint point) const noexcept;

private:
    GridArea(Shape shape, GridPoint minimum, GridPoint maximum) noexcept
        : shape_(shape), space_(minimum.space()), minimum_(minimum), maximum_(maximum), valid_(true) {}

    Shape           shape_ = Shape::Box2D;
    CoordinateSpace space_ = CoordinateSpace::Grid2D;
    GridPoint       minimum_;
    GridPoint       maximum_;
    bool            valid_ = false;
};

/** @brief A checked target-count and spatial constraint description. */
struct TargetingSpec {
    /** @brief Required coordinate space for origin, candidates and area. */
    CoordinateSpace space = CoordinateSpace::World2D;
    /** @brief Relationship class accepted from candidate facts. */
    TargetDomain domain = TargetDomain::Any;
    /** @brief Minimum number of candidates required in the resolved set. */
    std::uint32_t minCount = 0;
    /** @brief Maximum number of candidates accepted; no selection is performed to enforce it. */
    std::uint32_t maxCount = std::numeric_limits<std::uint32_t>::max();
    /** @brief Minimum distance, in world units or grid cells according to `space`. */
    float minRange = 0.f;
    /** @brief Maximum distance, in world units or grid cells; positive infinity means unbounded. */
    float maxRange = std::numeric_limits<float>::infinity();
    /** @brief All tags that each candidate must contain. */
    std::vector<std::string> requiredTags;
    /** @brief Tags that exclude a candidate when present. */
    std::vector<std::string> excludedTags;
    /** @brief Optional logical zone membership required from candidates. */
    std::optional<ZoneRef> zone;
    /** @brief Optional World2D/World3D area constraint. */
    std::optional<WorldArea> worldArea;
    /** @brief Optional Grid2D/Grid3D area constraint. */
    std::optional<GridArea> gridArea;
    /** @brief Whether an injected LOS provider must confirm each candidate. */
    LineOfSightMode lineOfSight = LineOfSightMode::NotRequired;

    /**
     * @brief Validates count, range, tags, zone and coordinate-space invariants.
     * @return Success or a structured Rejected result.
     */
    [[nodiscard]] Result<void> validate() const;
};

/** @brief A query passed from a domain targeting algorithm to sensing. */
struct TargetingQuery {
    /** @brief Subject issuing the query; it is not implicitly selected as a result. */
    SubjectRef origin;
    /** @brief Origin location in exactly `spec.space`. */
    TargetLocation originLocation;
    /** @brief Generic constraints to apply to candidate facts. */
    TargetingSpec spec;

    /** @brief Validates origin identity, location/spec space and spec constraints. */
    [[nodiscard]] Result<void> validate() const;
};

/**
 * @brief Candidate facts returned by sensing.
 *
 * This is deliberately not a selected target. The provider supplies facts;
 * skills, weapons, cards and RTS commands retain their own selection policy.
 */
struct TargetCandidate {
    /** @brief Stable subject identity. */
    SubjectRef subject;
    /** @brief Candidate location in one of the explicitly tagged spaces. */
    TargetLocation location;
    /** @brief Relationship class supplied by the sensing/domain adapter. */
    TargetDomain domain = TargetDomain::Neutral;
    /** @brief Stable gameplay tags used by required/excluded tag queries. */
    std::vector<std::string> tags;
    /** @brief Logical zones containing this candidate. */
    std::vector<ZoneRef> zones;
};

/**
 * @brief Owning target result containing primary-independent subjects and optional geometry.
 */
class TargetSet {
public:
    /** @brief Adds a valid subject once; duplicate subjects are a NoOp success. */
    [[nodiscard]] Result<void> addSubject(SubjectRef subject);
    /** @brief Assigns an existing subject as primary without choosing one implicitly. */
    [[nodiscard]] Result<void> setPrimary(SubjectRef subject);
    /** @brief Stores a point projection after preserving its coordinate tag. */
    [[nodiscard]] Result<void> setPoint(TargetLocation point);
    /** @brief Stores an area projection after preserving its coordinate tag. */
    [[nodiscard]] Result<void> setArea(WorldArea area);
    /** @brief Stores a grid area projection after preserving its coordinate tag. */
    [[nodiscard]] Result<void> setArea(GridArea area);

    /** @brief Returns borrowed subjects, valid until this TargetSet is mutated or destroyed. */
    [[nodiscard]] std::span<const SubjectRef> subjects() const noexcept { return subjects_; }
    /** @brief Returns the explicitly assigned primary subject, if any. */
    [[nodiscard]] const std::optional<SubjectRef>& primary() const noexcept { return primary_; }
    /** @brief Returns the optional point projection. */
    [[nodiscard]] const std::optional<TargetLocation>& point() const noexcept { return point_; }
    /** @brief Returns the optional world-area projection. */
    [[nodiscard]] const std::optional<WorldArea>& worldArea() const noexcept { return worldArea_; }
    /** @brief Returns the optional grid-area projection. */
    [[nodiscard]] const std::optional<GridArea>& gridArea() const noexcept { return gridArea_; }

private:
    std::vector<SubjectRef>       subjects_;
    std::optional<SubjectRef>     primary_;
    std::optional<TargetLocation> point_;
    std::optional<WorldArea>      worldArea_;
    std::optional<GridArea>       gridArea_;
};

/** @brief Result of one injected line-of-sight query. */
struct LineOfSightResult {
    /** @brief True when the complete segment is visible. */
    bool visible = false;
    /** @brief Optional stable blocker identity when the provider can identify it. */
    std::optional<SubjectRef> blocker;
};

/**
 * @brief Consumer-owned line-of-sight capability.
 *
 * Providers may support only selected coordinate spaces. A present provider
 * must return Unsupported for a space it cannot interpret; it must not convert
 * grid cells to world units implicitly.
 */
class ILineOfSightQuery {
public:
    static constexpr const char* capabilityName = "eve.sensing.ILineOfSightQuery";
    virtual ~ILineOfSightQuery()                = default;

    /**
     * @brief Tests the segment between two same-space locations.
     * @param from Borrowed value location of the observer.
     * @param to Borrowed value location of the candidate.
     * @return Visibility or Unsupported/Rejected with structured diagnostics.
     * @remarks Synchronous; callers must invoke on the provider's documented simulation thread.
     */
    [[nodiscard]] virtual Result<LineOfSightResult> query(const TargetLocation& from,
                                                          const TargetLocation& to) const = 0;
};

/**
 * @brief Candidate discovery capability consumed by TargetingResolver.
 *
 * Providers may use spatial indexes internally, but return owning candidate
 * values and never retain the query's temporary references.
 */
class ISensingCandidateProvider {
public:
    static constexpr const char* capabilityName = "eve.sensing.ISensingCandidateProvider";
    virtual ~ISensingCandidateProvider()        = default;

    /**
     * @brief Returns broad-phase candidates satisfying the coordinate and spatial constraints.
     * @return Owning candidates; no primary target is selected.
     * @remarks Synchronous and read-only. Thread affinity is provider-defined and must be
     * documented by each implementation.
     */
    [[nodiscard]] virtual Result<std::vector<TargetCandidate>> query(const TargetingQuery& query) const = 0;
};

/**
 * @brief Pure CPU sensing candidate provider with deterministic map-backed storage.
 *
 * It is useful as a minimal runtime provider and as a contract-test fixture.
 * The implementation has no physics, scene, or UI dependency; a production
 * provider may replace its linear scan with a spatial index.
 */
class SensingCandidateProvider final : public ISensingCandidateProvider {
public:
    /** @brief Creates an empty provider; registration with capability is explicit. */
    SensingCandidateProvider()           = default;
    ~SensingCandidateProvider() override = default;

    /** @brief Adds or replaces one candidate using its SubjectRef as the key. */
    [[nodiscard]] Result<void> upsert(TargetCandidate candidate);
    /** @brief Removes one candidate; NotFound is returned when absent. */
    [[nodiscard]] Result<void> remove(SubjectRef subject);
    /** @brief Returns the number of stored candidate facts. */
    [[nodiscard]] std::size_t size() const noexcept { return candidates_.size(); }

    /** @copydoc ISensingCandidateProvider::query */
    [[nodiscard]] Result<std::vector<TargetCandidate>> query(const TargetingQuery& query) const override;

private:
    std::map<std::string, TargetCandidate> candidates_;
};

/**
 * @brief Generic constraint evaluator that composes candidate and LOS capabilities.
 *
 * It filters by domain, count, range, tags, zone, area and LOS, but never
 * selects a primary target. Missing capabilities are observable Unsupported
 * failures rather than empty successful sets.
 */
class TargetingResolver {
public:
    /**
     * @brief Resolves a constrained candidate set through registered capabilities.
     * @return An owning TargetSet or a structured failure.
     * @remarks Synchronous; it does not mutate ECS, physics, scene, or UI state.
     */
    [[nodiscard]] Result<TargetSet> resolve(const TargetingQuery& query) const;
};

/**
 * @brief Stable spelling for diagnostics and telemetry.
 * @return Borrowed non-null pointer to immutable process-lifetime storage.
 * @ownership The returned static text is not caller-owned and must not be freed.
 * @lifetime Valid for the lifetime of the process; copy it when storing it.
 * @thread Thread-safe because the characters are immutable.
 * @reentrancy Does not invoke callbacks.
 */

}  // namespace eve::sensing
