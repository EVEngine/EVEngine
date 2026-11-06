#pragma once

#include "procgen/recursive/RecursiveModel.h"

namespace eve::procgen::recursive {

/** @brief Owning override retained by stable leaf ordinal, independent of active refinement. */
struct LeafEdit {
    std::uint64_t         ordinal   = 0;
    double                massScale = 1.0;
    std::array<double, 3> displacement{};
    std::array<double, 3> velocity{};
};

/** @brief Explicit staging budgets; exceeding them fails before publishing any candidate. */
struct EditBudget {
    std::size_t maxEdits     = 100000;
    std::size_t maxSummaries = 1000000;
};

/** @brief Owning edited subtree summary; fractions/momentum are relative to the nominal unedited root mass. */
struct EditedNodeSummary {
    NodeSummary           nominal;
    double                massFraction = 0;
    ScaledMass            mass;
    std::array<double, 3> centerOfMass{};
    std::array<double, 3> momentumPerRootMass{};
    /** @brief Integral of (x-COM)(x-COM)^T divided by nominal root mass, in root-local length squared.
     * Packed xx, yy, zz, xy, xz, yz. Multiply by nominal physical root mass and 2^(2*lengthExponent)
     * for physical second moments; inertia is trace(Q)I-Q. Derived cache, never serialized authority.
     */
    std::array<double, 6> centralSecondMomentPerRootMass{};
    std::array<double, 3> boundsMin{}, boundsMax{};
    Revision              revision;
};

/** @brief Owning runtime capture, not a serialized schema or portable save format. */
struct RecursiveState {
    ModelDefinition       definition;
    std::vector<LeafEdit> edits;
    Revision              revision;
};

/**
 * @brief Single-use staged sparse ledger and derived ancestor cache.
 * @ownership Owns candidate storage, weakly observes its instance; destruction cancels without publication.
 * @thread Owner-thread-affine, no callbacks; owner destruction safely revokes publication rights.
 */
class EVENGINE_API_FOUNDATION PreparedModelUpdate {
public:
    /** @brief Discard candidate state. */
    ~PreparedModelUpdate();
    /** @brief Transfer publication rights. */
    PreparedModelUpdate(PreparedModelUpdate&&) noexcept;
    /** @brief Discard the old candidate and transfer publication rights. */
    PreparedModelUpdate& operator=(PreparedModelUpdate&&) noexcept;
    PreparedModelUpdate(const PreparedModelUpdate&)            = delete;
    PreparedModelUpdate& operator=(const PreparedModelUpdate&) = delete;

private:
    friend class RecursiveInstance;
    struct Impl;
    explicit PreparedModelUpdate(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

/**
 * @brief Sole owner of a sparse edit ledger; ancestor summaries are derived, not another editable authority.
 * @ownership Owns definition, edits and cache. No scene, mesh or rigid body pointers are retained.
 * @thread All operations are serialized by the owner. No callbacks, locks, clock or implicit RNG.
 * @remarks Leaf displacements/velocities are root-local units. This stores motion observations but does
 * not integrate them. Snapshot persistence/restore belongs to a separate versioned envelope adapter.
 */
class EVENGINE_API_FOUNDATION RecursiveInstance {
public:
    /** @brief Destroy state and revoke candidates. */
    ~RecursiveInstance();
    /** @brief Transfer state and candidate identity. */
    RecursiveInstance(RecursiveInstance&&) noexcept;
    /** @brief Revoke this owner's candidates and transfer state. */
    RecursiveInstance& operator=(RecursiveInstance&&) noexcept;
    RecursiveInstance(const RecursiveInstance&)            = delete;
    RecursiveInstance& operator=(const RecursiveInstance&) = delete;
    /**
     * @brief Build an initially unedited owning instance with explicit resource limits.
     * @return Instance or invalid-definition/budget/allocation failure.
     * @cost Constant work/storage for an unedited definition; does not enumerate capacity.
     */
    [[nodiscard]] static Result<RecursiveInstance> create(ModelDefinition definition, EditBudget budget);
    /**
     * @brief Rebuild an independent owner from a validated runtime capture; never mutates an existing instance.
     * @param state Borrowed until return; edits are copied and fully revalidated, cached summaries are rebuilt.
     * @param budget Explicit admission limits, including restore work.
     * @return New owner preserving captured revision, or failure with no partially visible state.
     * @cost Same ledger compilation cost as prepareEdits plus O(E) copying per restore.
     * @remarks Persistent input must first pass the versioned envelope adapter. Old runtime candidates
     * cannot commit into this new owner, even when the restored revision matches their base revision.
     */
    [[nodiscard]] static Result<RecursiveInstance> restoreState(const RecursiveState& state, EditBudget budget);
    /**
     * @brief Stage replacement of the complete sparse ledger; omitted ordinals revert to the base rule.
     * @param completeEdits Borrowed until return; duplicate ordinals are rejected, input order is irrelevant.
     * @return Candidate for the next revision; no live state changes. Dropping it cancels publication.
     * @cost O(E*D + E*D*log S + E log E + S*D) work, O(E+S) storage per edit publication;
     * D<=52 and S<=budget. This is budgeted authoring work, not a per-frame linear field operation.
     */
    [[nodiscard]] Result<PreparedModelUpdate> prepareEdits(std::span<const LeafEdit> completeEdits) const;
    /**
     * @brief Consume and atomically publish a current candidate belonging to this instance.
     * @return Success or stale/foreign rejection, leaving live state unchanged on failure.
     * @cost Constant-time swaps plus O(E+S) destruction of the previous ledger/cache per commit.
     */
    [[nodiscard]] Result<void> commit(PreparedModelUpdate candidate);
    /**
     * @brief Read an edited subtree without materializing or merging descendants.
     * @return Owning summary with current revision, or invalid-path/range failure.
     * @cost O(D+log S) work and constant output storage per query.
     */
    [[nodiscard]] Result<EditedNodeSummary> describe(NodeKey key) const;
    /**
     * @brief Compile a complete source frontier from the current edited revision.
     * @param keys Borrowed only until return; no path or span reference is retained.
     * @param maxNodes Explicit active representation bound, separate from potential capacity.
     * @return Owning coverage-validated summaries; no parent/child double contribution.
     * @cost O(K*D+K log K+K log S) work and O(K) storage per explicit rebuild.
     */
    [[nodiscard]] Result<std::vector<EditedNodeSummary>> prepareFrontier(std::span<const NodeKey> keys,
                                                                         std::size_t              maxNodes) const;
    /**
     * @brief Capture owning definition/ledger/revision values, independent of active visual refinement.
     * @return Runtime capture or moved-from/allocation failure; no runtime owner token escapes.
     * @cost O(E) copy and storage per explicit capture; reuse across consumers.
     */
    [[nodiscard]] Result<RecursiveState> capture() const;

private:
    struct Impl;
    explicit RecursiveInstance(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::procgen::recursive
