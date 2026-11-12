#pragma once

#include "common/RuntimeRegistry.h"
#include "spatial/multiscale/CellPosition.h"

#include <memory>
#include <span>

namespace eve::spatial {

/** @brief Domain tag for process-local frame identities. */
struct MultiscaleFrameTag;
/** @brief Generation and owner-qualified frame identity; never persist this runtime value. */
using FrameHandle = script::RuntimeHandleRef<MultiscaleFrameTag>;

/** @brief Static frame: parent position = translation + 2^exponent * rotation * local position. */
struct FrameDefinition {
    /** @brief Parent's earlier input index; the first entry is the root and uses zero. */
    std::size_t  parentIndex = 0;
    CellPosition translation;
    int          exponent = 0;
    /** @brief Row-major proper orthonormal local-to-parent basis, checked within 1e-12.
     * @remarks Represented coefficients define the map; arithmetic bounds exclude input orientation error.
     */
    std::array<double, 9> rotation{1, 0, 0, 0, 1, 0, 0, 0, 1};
};

/** @brief Position explicitly bound to a runtime reference frame. */
struct FramePosition {
    FrameHandle  frame;
    CellPosition position;
};

/** @brief A position re-expressed in a static destination frame, with per-axis error in its local units. */
struct FrameRelocation {
    FramePosition         position;
    std::array<double, 3> absoluteErrorBound{};
};

/**
 * @brief Immutable, bounded translation/scale/orientation hierarchy with owner-qualified frame identities.
 * @ownership Owns copied definitions and identity registry; retains no scene or caller pointers.
 * @thread Const reads may run concurrently; move/destruction require exclusive ownership. No callbacks.
 * @remarks Static orientations only, not time-dependent rotating/accelerating dynamics. Rebuild creates new
 * identities; all old handles are rejected by the replacement graph. Runtime handles are not save data.
 */
class EVENGINE_API_FOUNDATION FrameGraph {
public:
    /** @brief Destroy the graph and invalidate its frame identities. */
    ~FrameGraph();
    /** @brief Transfer graph ownership; existing handles follow the graph. */
    FrameGraph(FrameGraph&&) noexcept;
    /** @brief Invalidate destination identities and transfer graph ownership. */
    FrameGraph& operator=(FrameGraph&&) noexcept;
    FrameGraph(const FrameGraph&)            = delete;
    FrameGraph& operator=(const FrameGraph&) = delete;

    /**
     * @brief Validate a topologically ordered hierarchy and build an owning immutable graph.
     * @param definitions Borrowed until return. Root must be identity; parents precede children.
     * @param maxFrames Explicit allocation bound, at most 1000000; tree depth is bounded at 64.
     * @return Owning graph or failure, without partial publication; cumulative exponents must lie in [-900,900].
     * @cost O(F) validation, identity allocation and storage per build; reuse across many position projections.
     */
    [[nodiscard]] static Result<FrameGraph> build(std::span<const FrameDefinition> definitions, std::size_t maxFrames);
    /** @brief Return an owning handle for an input index, or structured range/moved-from failure. */
    [[nodiscard]] Result<FrameHandle> frame(std::size_t inputIndex) const;
    /**
     * @brief Project target minus origin in root-axis output units, cancelling their common ancestry first.
     * @param target Value bound to this graph; stale/foreign identities are rejected.
     * @param origin Value bound to this graph; no scene rebasing is performed.
     * @param outputExponent One output unit equals 2^exponent root units, in [-900,900].
     * @param maxAbsoluteError Positive finite per-axis arithmetic error budget.
     * @return Owning projection and bound; Unsupported if branch conversion cannot meet precision/range.
     * @cost O(depth) work and fixed bounded storage per query; equal-frame projection is constant work.
     * @remarks Conservative arithmetic bounds, not input quantization. Requires IEEE gradual underflow,
     * no fast-math/FTZ. No conversion of common ancestors into absolute floating-point positions.
     */
    [[nodiscard]] Result<CellProjection> project(FramePosition target, FramePosition origin, int outputExponent,
                                                 double maxAbsoluteError) const;

    /**
     * @brief Re-express a position in another static frame without changing any scene or physical state.
     * @param source Position owned by this graph's identity domain.
     * @param destination Live destination frame in this graph.
     * @param maxAbsoluteError Positive finite per-axis arithmetic budget in destination local units.
     * @return Canonical destination position and error bounds, or stale/range/precision failure.
     * @ownership Inputs and result own values; frame handles remain borrowed identities of this graph.
     * @thread Const query; concurrent reads allowed, not concurrent move/destruction. No callbacks.
     * @cost O(depth) work and fixed bounded storage per relocation; same-frame normalization is constant work.
     * @remarks Uses the inverse of represented orientation coefficients, not an assumed exact transpose.
     * Does not transform velocity or acceleration, migrate rigid bodies, or add non-inertial forces.
     * Caller must retain the returned uncertainty; discarding it does not make the conversion exact.
     */
    [[nodiscard]] Result<FrameRelocation> reexpress(FramePosition source, FrameHandle destination,
                                                    double maxAbsoluteError) const;

private:
    struct Impl;
    explicit FrameGraph(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::spatial
