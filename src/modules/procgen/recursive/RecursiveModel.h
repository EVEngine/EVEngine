#pragma once

#include "common/Result.h"
#include "common/Revision.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace eve::procgen::recursive {

/** @brief Binary path identity: root is 1; lower/upper children append a 0/1 bit. */
struct NodeKey {
    std::uint64_t path = 1;
    /** @brief Compare stable definition-local paths. */
    bool operator==(const NodeKey&) const = default;
};

/** @brief Capacity either partitions fixed volume or grows the root at fixed mean leaf volume. */
enum class CapacityMode { FixedVolume, FixedMeanLeafVolume };
/** @brief Independent deterministic random streams; values are part of rule version 1. */
enum class RandomStream : std::uint64_t { Geometry = 1, Material = 2, Decoration = 3 };

/** @brief Finite mantissa times 2^exponent; permits physical mass outside binary64's absolute range. */
struct ScaledMass {
    double mantissa = 0.0;
    int    exponent = 0;
};

/**
 * @brief Versioned uniform-density partition definition; leaf geometry is supplied by a future adapter.
 * @remarks Coordinates below are root-local. Physical length = local length * 2^lengthExponent.
 * Density is mass per physical unit cubed; mass exponent therefore includes 3*lengthExponent.
 * Capacity controls structural leaf count, independently of geometric scale.
 */
struct ModelDefinition {
    std::uint32_t         ruleVersion = 1;
    std::uint64_t         seed        = 0;
    std::uint64_t         capacity    = 1;
    std::array<double, 3> baseHalfExtent{1, 1, 1};
    int                   lengthExponent = 0;
    double                density        = 1.0;
    CapacityMode          capacityMode   = CapacityMode::FixedVolume;
    Revision              revision{1};
};

/**
 * @brief Analytic summary of a complete subtree, requiring no descendant materialization.
 * @remarks Mass fraction and centroid define first moment relative to root mass. Uniform static
 * definitions have zero momentum. Arithmetic is binary64 with tolerance, not bit-exact physical sums.
 */
struct NodeSummary {
    NodeKey               key;
    std::uint64_t         firstLeaf = 0;
    std::uint64_t         leafCount = 0;
    std::array<double, 3> center{};
    std::array<double, 3> halfExtent{};
    int                   lengthExponent   = 0;
    double                rootMassFraction = 0.0;
    ScaledMass            mass;
    Revision              revision;
};

/**
 * @brief Immutable conservative lazy binary partition, independent of old Procgen seeds and caches.
 * @ownership Owns its definition only; summaries own values and never keep scene/provider pointers.
 * @thread Const queries may run concurrently; moves/destruction require exclusivity. No callbacks or RNG state.
 * @remarks Potential leaves are bounded at 2^52, path depth at 52. This defines volumes/summaries,
 * not rendered ship/city meshes, mutable edits, or an engine update loop.
 */
class EVENGINE_API_FOUNDATION RecursiveModel {
public:
    /** @brief Release the immutable definition. */
    ~RecursiveModel();
    /** @brief Transfer owning definition storage. */
    RecursiveModel(RecursiveModel&&) noexcept;
    /** @brief Replace the owning definition storage. */
    RecursiveModel& operator=(RecursiveModel&&) noexcept;
    RecursiveModel(const RecursiveModel&)            = delete;
    RecursiveModel& operator=(const RecursiveModel&) = delete;

    /**
     * @brief Validate rule version, ranges and physical scaling, without generating descendants.
     * @return Owning model or structured validation/range/allocation failure.
     * @cost Constant work and one owning allocation per definition; capacity does not cause enumeration.
     */
    [[nodiscard]] static Result<RecursiveModel> build(ModelDefinition definition);
    /**
     * @brief Evaluate one subtree's analytic geometry/mass summary directly from its path.
     * @return Owning summary or invalid/nonexistent-path failure; input key is definition-local.
     * @cost O(path depth), at most 52 splits, constant storage per query.
     */
    [[nodiscard]] Result<NodeSummary> describe(NodeKey key) const;
    /**
     * @brief Locate one stable leaf ordinal without enumerating preceding leaves.
     * @return Owning terminal summary or range/moved-from failure.
     * @cost O(depth) bounded traversal and constant storage per query.
     */
    [[nodiscard]] Result<NodeSummary> leaf(std::uint64_t ordinal) const;
    /**
     * @brief Return both children of a nonterminal node, preserving its complete source coverage.
     * @return Owning child pair, or failure if the path is invalid or already terminal.
     * @cost O(path depth) work and constant storage; no grandchildren are generated.
     */
    [[nodiscard]] Result<std::array<NodeSummary, 2>> split(NodeKey key) const;
    /**
     * @brief Compile a non-overlapping complete source cover; rejects parent/child duplicate mass or holes.
     * @param keys Borrowed only during this call; returned summaries are sorted by first leaf.
     * @param maxNodes Explicit active representation budget; potential capacity is not this budget.
     * @return Owning source cover or failure with no partially published frontier.
     * @cost O(K*depth + K log K) work and O(K) storage per explicit frontier rebuild, not a per-frame linear claim.
     */
    [[nodiscard]] Result<std::vector<NodeSummary>> prepareFrontier(std::span<const NodeKey> keys,
                                                                   std::size_t              maxNodes) const;
    /**
     * @brief Return a deterministic random word keyed by seed, rule version, path, stream and counter.
     * @return Word or invalid node/stream failure; no shared random state is advanced.
     * @cost O(path depth) validity check followed by constant integer work per request.
     */
    [[nodiscard]] Result<std::uint64_t> randomWord(NodeKey key, RandomStream stream, std::uint64_t counter) const;

private:
    struct Impl;
    explicit RecursiveModel(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::procgen::recursive
