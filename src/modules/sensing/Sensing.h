#pragma once

#include "common/BorrowedRef.h"
#include "common/Module.h"
#include "common/Result.h"
#include "common/SensingQuery.h"
#include "common/SquirrelOwnership.h"
#include "spatial/SpatialHash2D.h"

#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace eve::sensing {

/** @brief Mirrored gameplay facts used by candidate queries. */
struct Subject {
    std::string           id;
    float                 x = 0, y = 0;
    std::string           faction;
    std::set<std::string> tags;
    std::set<std::string> visibleTo;
    std::set<std::string> zones; /**< Logical zone ids (LogicalId text). */
};

/** @brief A deterministic candidate returned by a spatial query. */
struct Candidate {
    std::string id;
    float       x = 0, y = 0, distance = 0;
};

/** @brief How candidate-count overflow relative to maxCount is handled. */
enum class CountPolicy : std::uint8_t {
    /** @brief Fail the query when the filtered set is outside [minCount, maxCount]. */
    FailIfOutOfRange,
    /** @brief Keep the best maxCount candidates after sorting (legacy circle/box limit). */
    TruncateToMax,
};

/** @brief Ordering applied to ranked query results. */
enum class SortKey : std::uint8_t {
    None,
    DistanceAscending,
};

/** @brief Circle shape for QuerySpec (World2D). */
struct QueryCircle {
    float x      = 0.f;
    float y      = 0.f;
    float radius = 0.f;
};

/** @brief Inclusive axis-aligned box shape for QuerySpec (World2D). */
struct QueryBox {
    float minX = 0.f;
    float minY = 0.f;
    float maxX = 0.f;
    float maxY = 0.f;
};

/**
 * @brief 2D cone/sector shape for QuerySpec (World2D).
 *
 * Apex is `(x,y)`. `(dirX,dirY)` is the forward axis (normalized on use).
 * `halfAngle` is radians in `[0, pi]`; `range` is non-negative distance from the apex.
 */
struct QueryCone {
    float x         = 0.f;
    float y         = 0.f;
    float dirX      = 1.f;
    float dirY      = 0.f;
    float halfAngle = 0.f;
    float range     = 0.f;
};

/** @brief Optional broad-phase shape; monostate means range-only around the origin. */
using QueryShape = std::variant<std::monostate, QueryCircle, QueryBox, QueryCone>;

/**
 * @brief Configurable candidate query against SensingWorld.
 *
 * This is the single filtering/sorting implementation surface. Script circle/box
 * helpers and Targeting adapters compose a QuerySpec rather than reimplementing
 * acceptance rules. Domain/LOS/zone stay outside this World2D fact query; adapters
 * that need them must inject projection/LOS capabilities explicitly.
 */
struct QuerySpec {
    QueryShape               shape{};
    float                    minRange = 0.f;
    float                    maxRange = std::numeric_limits<float>::infinity();
    std::vector<std::string> requiredTags;
    std::vector<std::string> excludedTags;
    std::vector<std::string> includeFactions;
    std::vector<std::string> excludeFactions;
    std::optional<std::string> visibleTo;
    std::uint32_t            minCount    = 0;
    std::uint32_t            maxCount    = std::numeric_limits<std::uint32_t>::max();
    CountPolicy              countPolicy = CountPolicy::TruncateToMax;
    SortKey                  sortKey     = SortKey::DistanceAscending;

    /** @brief Validates ranges, counts, tags and shape invariants. */
    [[nodiscard]] eve::Result<void> validate() const;
};

/** @brief World2D origin used for range tests and distance sorting. */
struct QueryOrigin {
    float                x = 0.f;
    float                y = 0.f;
    std::optional<std::string> subjectId; /**< Optional self id to exclude from results. */
};

/** @brief One ranked candidate produced by SensingWorld::query. */
struct RankedCandidate {
    std::string id;
    float       x            = 0.f;
    float       y            = 0.f;
    float       distance     = 0.f;
    float       score        = 0.f; /**< Higher is better; Phase 1 uses -distance. */
    std::string scoreReason;        /**< Stable diagnostic token, e.g. "distance". */
};

/** @brief Owning ranked result; does not assign a primary target. */
class CandidateQueryResult {
public:
    CandidateQueryResult() = default;
    explicit CandidateQueryResult(std::vector<RankedCandidate> ranked) : ranked_(std::move(ranked)) {}

    [[nodiscard]] std::span<const RankedCandidate> ranked() const noexcept { return ranked_; }
    [[nodiscard]] std::size_t size() const noexcept { return ranked_.size(); }

private:
    std::vector<RankedCandidate> ranked_;
};

/** @brief Gameplay-facing 2D candidate query service; it never values or selects targets. */
class SensingWorld {
public:
    /** @brief Inserts or replaces mirrored facts. CSV fields contain comma-separated stable keys. */
    [[nodiscard]] eve::Result<void> upsert(std::string_view id, float x, float y, std::string_view faction,
                                           std::string_view tagsCsv, std::string_view visibleToCsv);
    /** @brief Removes mirrored facts, or returns NotFound when the id is absent. */
    [[nodiscard]] eve::Result<void> remove(std::string_view id);
    /**
     * @brief Replaces logical zone membership for an existing subject.
     * @param id Subject id that must already be registered.
     * @param zonesCsv Comma-separated LogicalId texts (e.g. "arena:central"); empty clears membership.
     * @return Applied on success, NotFound when absent, or InvalidArgument for empty/invalid zone ids.
     * @thread Call on the sensing world's owning simulation thread.
     */
    [[nodiscard]] eve::Result<void> setZones(std::string_view id, std::string_view zonesCsv);

    /**
     * @brief Enables or disables an optional SpatialHash2D broadphase (off by default).
     * @param enabled When true, rebuilds the index from current subjects.
     * @param cellSize Uniform grid cell size; must be finite and > 0 when enabling.
     * @return Applied on success, or InvalidArgument when cellSize is invalid.
     * @remarks Filtering/sorting remain QuerySpec-owned; the hash only culls candidates.
     * @thread Call on the sensing world's owning simulation thread.
     */
    [[nodiscard]] eve::Result<void> setSpatialIndexEnabled(bool enabled, float cellSize = 64.f);
    /** @brief Reports whether the optional spatial broadphase is active. */
    [[nodiscard]] bool spatialIndexEnabled() const noexcept;

    /**
     * @brief Runs a configurable candidate query.
     * @return Owning ranked candidates, or a structured failure.
     * @remarks Also refreshes the resultAt() cache to match ranked() order.
     * @thread Call on the sensing world's owning simulation thread.
     */
    [[nodiscard]] eve::Result<CandidateQueryResult> query(const QueryOrigin& origin, const QuerySpec& spec);

    /** @brief Queries a circle. Filters are CSV; empty fields disable that filter. */
    [[nodiscard]] eve::Result<int> circle(float x, float y, float radius, std::string_view requireTagsCsv,
                                          std::string_view excludeTagsCsv, std::string_view includeFactionsCsv,
                                          std::string_view excludeFactionsCsv, std::string_view visibleTo, int limit);
    /** @brief Queries an axis-aligned box with the same filters as circle(). */
    [[nodiscard]] eve::Result<int> box(float minX, float minY, float maxX, float maxY, std::string_view requireTagsCsv,
                                       std::string_view excludeTagsCsv, std::string_view includeFactionsCsv,
                                       std::string_view excludeFactionsCsv, std::string_view visibleTo, int limit);
    /**
     * @brief Runs a registered TargetingPreset against this world and refreshes resultAt().
     * @param presetId Stable preset id (e.g. "sensing.builtin.coneSelect").
     * @param originX Origin X used as QueryOrigin and cone apex.
     * @param originY Origin Y used as QueryOrigin and cone apex.
     * @param dirX Forward X for cone filter tasks (normalized on use).
     * @param dirY Forward Y for cone filter tasks (normalized on use).
     * @return Number of ranked candidates, or a structured failure.
     * @thread Call on the sensing world's owning simulation thread.
     */
    [[nodiscard]] eve::Result<int> executePreset(std::string_view presetId, float originX, float originY, float dirX,
                                                 float dirY);
    /**
     * @brief Returns a candidate from the most recent query, or null for an invalid index.
     * @return Borrowed nullable candidate owned by the query result cache.
     * @ownership SensingWorld owns the candidate cache; callers must not delete or mutate it.
     * @lifetime Valid until the next query/circle/box, restore, or world destruction; copy it for later use.
     * @thread Call on the sensing world's owning simulation thread.
     * @reentrancy Do not retain across a callback or another query.
     */
    [[nodiscard]] eve::OptionalRef<const Candidate> resultAt(int index) const;
    /**
     * @brief Deterministic JSON dump of the last successful query for MCP/debug overlays.
     * @return Schema `eve.sensing.lastQuery` with origin, shape, spatial stats, and ranked scores.
     * @remarks Empty ranked list when no query has succeeded yet. Does not assign primary.
     */
    [[nodiscard]] std::string debugLastQueryJson() const;
    /** @brief Exports deterministic JSON. */
    std::string snapshotJson() const;
    /** @brief Restores a snapshot transactionally. */
    [[nodiscard]] eve::Result<void> restoreJson(const std::string& json);

    /** @brief Returns a borrowed view of stored subjects for adapters; valid until mutation. */
    [[nodiscard]] const std::map<std::string, Subject>& subjects() const noexcept { return subjects_; }

    SensingWorld() = default;
    ~SensingWorld() = default;
    SensingWorld(const SensingWorld&) = delete;
    SensingWorld& operator=(const SensingWorld&) = delete;
    SensingWorld(SensingWorld&&) noexcept = default;
    SensingWorld& operator=(SensingWorld&&) noexcept = default;

private:
    [[nodiscard]] bool accepts(const Subject& subject, const QuerySpec& spec) const;
    void               publishResults(std::vector<RankedCandidate> ranked);
    void               clearSpatialIndex();
    void               rebuildSpatialIndex();
    void               indexSubject(const Subject& subject);
    void               unindexSubject(const std::string& id);
    /** @brief Returns true when the spatial hash produced a broadphase candidate set. */
    [[nodiscard]] bool trySpatialBroadphase(const QueryOrigin& origin, const QuerySpec& spec,
                                            std::vector<const Subject*>& out) const;

    std::map<std::string, Subject> subjects_;
    std::vector<Candidate>         results_;

    std::unique_ptr<eve::spatial::SpatialHash2D> spatialIndex_;
    std::unordered_map<std::string, int>         subjectSpatialIds_;
    std::unordered_map<int, std::string>         spatialIdSubjects_;
    int                                          nextSpatialId_ = 1;

    struct LastQueryDebug {
        bool                         usedSpatial = false;
        std::uint32_t                scanned     = 0;
        std::uint32_t                accepted    = 0;
        float                        originX     = 0.f;
        float                        originY     = 0.f;
        std::string                  shapeKind   = "none";
        std::vector<RankedCandidate> ranked;
    };
    LastQueryDebug lastQuery_;
};

/** @brief Handle domain for module-owned sensing worlds. */
struct SensingWorldHandleTag {};
/** @brief Generation- and module-epoch-qualified sensing-world reference. */
using SensingWorldHandleRef = eve::script::RuntimeHandleRef<SensingWorldHandleTag>;

/** @brief Script factory for independent sensing worlds. */
class Sensing : public Module, public eve::ISensingQuery {
public:
    Module_REG(Sensing);
    /** @brief Registers the read-only sensing capability for automation hosts. */
    Sensing();
    ~Sensing() override;
    /**
     * @brief Script factory for independent sensing worlds.
     * @return A generation-qualified reference to a module-owned world.
     * @ownership Sensing retains sole ownership; callers retain the reference and release it explicitly.
     * @lifetime The reference becomes stale after release, module unload, or registry replacement.
     * @thread Call on the Sensing module's owning thread.
     * @reentrancy The factory invokes no callbacks.
     */
    [[nodiscard]] static eve::Result<SensingWorldHandleRef> newWorld();
    /** @brief Resolves a live sensing world as a non-owning observation. */
    [[nodiscard]] static eve::script::Borrowed<SensingWorld> resolve(SensingWorldHandleRef reference) noexcept;
    /** @brief Releases a module-owned sensing world and invalidates its handle. */
    [[nodiscard]] static eve::Result<void> release(SensingWorldHandleRef reference);
    /** @brief Reports whether a world reference is stale. */
    [[nodiscard]] static bool isStale(SensingWorldHandleRef reference) noexcept;

    /** @copydoc eve::ISensingQuery::worldCount */
    [[nodiscard]] int worldCount() const override;
    /** @copydoc eve::ISensingQuery::lastQueries */
    [[nodiscard]] std::vector<eve::SensingWorldQuery> lastQueries() const override;

private:
    eve::script::RuntimeObjectRegistry<SensingWorld, SensingWorldHandleTag> worlds_;
};

/**
 * @brief Neutral perception fact projected from a ranked sensing candidate.
 *
 * npc_ai / gameplay adapters copy these into their own memory types. Sensing
 * does not depend on npc_ai and never assigns threat or primary target semantics.
 */
struct PerceptionFact {
    std::string subjectId;
    float       x            = 0.f;
    float       y            = 0.f;
    float       distance     = 0.f;
    float       score        = 0.f;
    std::string scoreReason;
};

/**
 * @brief Projects ranked query results into adapter-neutral perception facts.
 * @param result Borrowed ranked candidates from SensingWorld::query / executePreset.
 * @return Owning facts in the same order as `result.ranked()`.
 */
[[nodiscard]] inline std::vector<PerceptionFact> perceptionFactsFrom(const CandidateQueryResult& result) {
    std::vector<PerceptionFact> facts;
    facts.reserve(result.size());
    for (const auto& candidate : result.ranked()) {
        PerceptionFact fact;
        fact.subjectId   = candidate.id;
        fact.x           = candidate.x;
        fact.y           = candidate.y;
        fact.distance    = candidate.distance;
        fact.score       = candidate.score;
        fact.scoreReason = candidate.scoreReason;
        facts.push_back(std::move(fact));
    }
    return facts;
}

}  // namespace eve::sensing
