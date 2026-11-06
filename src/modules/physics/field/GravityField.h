#pragma once

#include "common/Result.h"
#include "common/Revision.h"
#include "common/Time.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace eve::physics::field {

/** @brief A finite Newtonian point source; units are caller-selected but consistent across a batch. */
struct Particle {
    std::array<double, 3> position{};
    double                mass = 1.0;
};

/** @brief Independent evaluation location, optionally excluding its coincident source by input index. */
struct FieldTarget {
    std::array<double, 3> position{};
    /** @brief Only valid when this target exactly equals the indexed source position; no arbitrary source masking. */
    std::optional<std::size_t> excludedSource;
};

/** @brief Exact identity of an immutable simulation input; no wall-clock dependency. */
struct FieldStamp {
    SimulationTick tick;
    Revision       revision;
    /** @brief Compare both tick and source revision. */
    bool operator==(const FieldStamp&) const = default;
};

/** @brief Explicit approximation and resource limits, never inferred from a camera. */
struct GravityPolicy {
    double      gravitationalConstant     = 1.0;
    double      absoluteAccelerationError = 1e-5;
    double      openingRatio              = 0.4;
    unsigned    expansionOrder            = 4;
    std::size_t leafCapacity              = 16;
    unsigned    maxDepth                  = 32;
    std::size_t maxParticles              = 1000000;
    std::size_t maxNodes                  = 2000000;
    std::size_t maxInteractions           = 16000000;
    std::size_t maxDirectPairs            = 64000000;
};

/** @brief Complete stage-A work counts, including tree construction and direct-work admission. */
struct GravityMetrics {
    /** @brief Source row count; unchanged meaning for the original build entry point. */
    std::size_t particles = 0;
    /** @brief Evaluation target row count, which may differ from the source count. */
    std::size_t targets          = 0;
    std::size_t nodes            = 0;
    std::size_t partitionVisits  = 0;
    std::size_t traversalPairs   = 0;
    std::size_t farInteractions  = 0;
    std::size_t nearInteractions = 0;
    std::size_t directPairs      = 0;
    std::size_t coefficientCount = 0;
    unsigned    treeDepth        = 0;
};

/** @brief Field at one target position; self interaction is excluded only by the batch's explicit index identity. */
struct GravitySample {
    double                potential = 0.0;
    std::array<double, 3> acceleration{};
    /** @brief Conservative analytic Taylor remainder bound on acceleration norm; excludes roundoff. */
    double truncationErrorBound = 0.0;
};

/**
 * @brief Owning immutable Cartesian multipole/local expansion with admitted near-field lists.
 * @ownership Owns a copy of inputs and all derived storage; no scene, body, provider or script pointers.
 * @thread Const evaluation may run concurrently with other evaluations; never with move/destruction.
 * @reentrancy No callbacks. Destruction has no simulation side effects.
 * @remarks Experimental point-source kernel, no softening or distance cutoff. Coincident distinct
 * sources are rejected at evaluation. IEEE binary64, no fast-math. Error contract explicitly separates
 * analytic truncation from floating-point roundoff; it is not a certified total-error solver.
 */
class EVENGINE_API_FOUNDATION GravityField {
public:
    /** @brief Release the owning snapshot. */
    ~GravityField();
    /** @brief Move the snapshot; evaluation of the moved-from object is rejected. */
    GravityField(GravityField&&) noexcept;
    /** @brief Replace a snapshot by moving; old views are not exposed. */
    GravityField& operator=(GravityField&&) noexcept;
    GravityField(const GravityField&)            = delete;
    GravityField& operator=(const GravityField&) = delete;

    /**
     * @brief Stage A: copy sources, build a bounded-depth tree and compile local potential expansions.
     * @param particles Borrowed only until return; each row is also an independent evaluation target.
     * @param stamp Tick and revision of all rows.
     * @param policy Explicit kernel, accuracy and work bounds.
     * @return Owning snapshot or structured invalid/range/budget diagnostic; never partially publishes.
     * @cost O(N*depth + N*C(p) + nodes*(T(p)+C(p)) + interactions*(T(p)+C(p)+depth) + C(p)^2) work
     * and O(N + nodes*C(p) + interactions + T(p)) storage per build. The coefficient count is binomial(p+3,3);
     * the translation term count is binomial(p+6,6). Fixed depth/order and bounded node/interaction counts give
     * linear work in N; increasing order also increases translation cost, not merely coefficient count.
     */
    [[nodiscard]] static Result<GravityField> build(std::span<const Particle> particles, FieldStamp stamp,
                                                    GravityPolicy policy);

    /**
     * @brief Build a field for independent source and target batches, preserving target input order.
     * @param sources Nonempty finite positive point masses, copied before return.
     * @param targets Finite locations, copied before return; empty is allowed. An excluded source must coincide
     * exactly.
     * @param stamp Identity shared by both immutable batches.
     * @param policy Explicit limits; maxParticles bounds the combined source and target row count here.
     * @return Owning snapshot or structured failure, without partial publication.
     * @ownership All input data is copied; no spans or pointers are retained.
     * @thread Reentrant; no callbacks or shared mutable state.
     * @cost Same build cost as build with N=Ns+Nt and both trees included, plus O(Nt+P) exclusion accounting
     * for admitted direct pairs P; amortize the snapshot across evaluations. Rejected final work adds at most O(Nt).
     * @remarks Separate trees share normalization and the existing analytic error contract; no distance cutoff.
     */
    [[nodiscard]] static Result<GravityField> buildAtTargets(std::span<const Particle>    sources,
                                                             std::span<const FieldTarget> targets, FieldStamp stamp,
                                                             GravityPolicy policy);

    /**
     * @brief Stage B: evaluate all target rows in their original input order using this snapshot only.
     * @param expected Must match the snapshot tick and revision; stale data is rejected.
     * @return Owning samples, or failure with no partial result on singularity, stale stamp or overflow.
     * @cost O(nodes + N*C(p) + admitted direct pairs) per evaluation, O(N) output storage;
     * the coefficient count is binomial(p+3,3). No tree traversal per target; monomials share computed prefixes.
     */
    [[nodiscard]] Result<std::vector<GravitySample>> evaluate(FieldStamp expected) const;

    /** @brief Return owning stage-A metrics; a moved-from snapshot reports zero counts. */
    [[nodiscard]] GravityMetrics metrics() const noexcept;

private:
    struct Impl;
    explicit GravityField(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace eve::physics::field
