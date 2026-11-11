#pragma once

#include "common/Result.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace eve {

/** @brief Owning read-only scene projection; host-local id is qualified by snapshot and host. */
struct SceneSearchRecord {
    std::string              id, persistentId, name, space, text;
    std::vector<std::string> tags, kinds;
    std::array<double, 3>    position{};
    /** @brief World-space orthonormal axes: right, up, forward (+Z local); zero axes mean degenerate pose. */
    std::array<std::array<double, 3>, 3> axes{};
    /** @brief World AABB projected from all eight local corners; absent means no authored bounds. */
    std::optional<std::array<double, 6>> bounds;
    bool                                 visible = true;
    int                                  layer   = 0;
};

/** @brief Frozen source evidence. Tokens expire after preparation, source mutation or host destruction. */
struct SceneSearchSnapshot {
    std::string token, host;
    /** @brief Published embedding identity; empty before index publication. */
    std::string                    model;
    std::vector<SceneSearchRecord> records;
};

/** @brief Exact geometric relation; directional predicates compare node origins, not visual occlusion. */
enum class SceneSpatialRelation { Left, Right, Above, Below, Front, Behind, Near, Inside };
/** @brief World axes, reference node axes, or a separate observer node's axes. */
enum class SceneSpatialFrame { World, Reference, Observer };
/** @brief Static protocol spellings, in SceneSpatialRelation order. */
inline constexpr std::array<std::string_view, 8> sceneSpatialRelationNames{
    "left-of", "right-of", "above", "below", "in-front-of", "behind", "near", "inside"};
/** @brief Static protocol spellings, in SceneSpatialFrame order. */
inline constexpr std::array<std::string_view, 3> sceneSpatialFrameNames{"world", "reference", "observer"};
/** @brief One AND constraint. References are host-local IDs resolved in the current authoritative projection. */
struct SceneSpatialConstraint {
    SceneSpatialRelation relation = SceneSpatialRelation::Near;
    SceneSpatialFrame    frame    = SceneSpatialFrame::World;
    std::string          reference, observer;
    double               distance = 5, margin = 0.01;
};
/** @brief Measured evidence in the selected coordinate basis; no model-estimated spatial facts. */
struct SceneSpatialEvidence {
    std::string           reference, observer;
    SceneSpatialRelation  relation = SceneSpatialRelation::Near;
    SceneSpatialFrame     frame    = SceneSpatialFrame::World;
    std::array<double, 3> offset{};
    double                distance = 0;
};

/** @brief One embedding space; revision must identify immutable model weights and encoding configuration. */
struct SceneEmbeddingBatch {
    std::string                      snapshot, model;
    std::vector<std::vector<double>> vectors;
};

/** @brief Exact structured filters, optional HNSW cosine retrieval and bounded pagination. */
struct SceneSearchQuery {
    std::string                          snapshot, model, kind, space, pageToken;
    std::vector<std::string>             tags;
    std::optional<bool>                  visible;
    std::optional<int>                   layer;
    std::optional<std::array<double, 3>> center;
    double                               radius = 0;
    std::vector<double>                  embedding;
    std::vector<SceneSpatialConstraint>  spatial;
    /** @brief Maximum HNSW candidate window; pagination stays within this explicitly bounded window. */
    std::size_t candidates = 128;
    std::size_t limit = 20, scanBudget = 4096;
};

/** @brief Candidate plus cosine evidence (zero for structured-only queries). */
struct SceneSearchHit {
    SceneSearchRecord                 record;
    double                            score = 0;
    std::vector<SceneSpatialEvidence> spatial;
};

/** @brief Owning results, snapshot/index watermarks and continuation bound to the complete query. */
struct SceneSearchResult {
    std::string                 snapshot, indexSnapshot, model, pageToken;
    std::vector<SceneSearchHit> hits;
    std::size_t                 scanned = 0, matched = 0;
    /** @brief HNSW is approximate; candidateTruncated indicates a window smaller than eligible objects. */
    bool approximate = false, candidateTruncated = false;
};

/**
 * @brief Optional scene-owned search capability; never owns or mutates scene entities.
 * @ownership Process-lifetime provider; all inputs borrowed only during the call, outputs own data.
 * @thread Engine thread at a safe point; no concurrent scene mutation.
 * @reentrancy Calls no scripts, link callbacks or embedding services. Not reentrant.
 */
class ISceneSearch {
public:
    static constexpr const char* capabilityName = "ISceneSearch";
    /** @brief Destroy the provider and its derived projection. */
    virtual ~ISceneSearch() = default;
    /**
     * @brief Prepare one bounded immutable projection, replacing the previous search session.
     * @param host Explicit existing host name.
     * @return Snapshot token and canonical embedding texts, or NotFound / budget failure.
     * @cost Linear in nodes and ancestor transforms per preparation; amortize model work by text caching.
     */
    [[nodiscard]] virtual Result<SceneSearchSnapshot> prepare(std::string_view host) = 0;
    /**
     * @brief Read the current session and live geometry without rebuilding its embedding/HNSW index.
     * @return Owning snapshot, or unavailable/stale semantic source. Engine thread; no callbacks.
     * @cost Linear scene projection per status request; use at tool frequency, not per frame.
     */
    [[nodiscard]] virtual Result<SceneSearchSnapshot> status() = 0;
    /**
     * @brief Atomically publish embeddings in snapshot record order after source validation.
     * @return Failure on stale source, invalid/nonfinite vectors or mixed dimensions; old index stays intact.
     * @cost Scene-wide vector normalization and HNSW graph construction; amortize over many queries.
     */
    [[nodiscard]] virtual Result<void> publish(const SceneEmbeddingBatch& batch) = 0;
    /**
     * @brief Filter live geometry and retrieve a bounded approximate HNSW cosine candidate window.
     * @return Owned candidates; stale tokens, absent vectors and exceeded budgets fail explicitly.
     * @cost Linear scene verification plus data-dependent graph traversal; use for low-frequency tools.
     */
    [[nodiscard]] virtual Result<SceneSearchResult> query(const SceneSearchQuery& query) = 0;
};
}  // namespace eve
