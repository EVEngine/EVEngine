#pragma once

#include "common/Result.h"

#include <array>
#include <cstdint>

namespace eve::spatial {

/**
 * @brief Position in a caller-owned, common axis-aligned dyadic lattice.
 * @remarks Each axis denotes cell + fraction, with fraction in [-0.5, 0.5).
 * The caller supplies the lattice scale to projectCellRelative. This value does not
 * identify a reference frame: never mix coordinates from different frames/lattices.
 * @ownership Owns all values; no entity, scene, registry or pointer is retained.
 * @thread Value semantics; immutable copies may be read concurrently. No callbacks.
 */
struct CellPosition {
    std::array<int64_t, 3> cell{};
    std::array<double, 3>  fraction{};
};

/** @brief Explicit projection units and absolute per-axis numerical error budget. */
struct CellProjectionPolicy {
    /** @brief One cell measures 2^cellExponent base units; supported range [-900, 900]. */
    int cellExponent = 0;
    /** @brief One output unit measures 2^outputExponent base units; range [-900, 900]. */
    int outputExponent = 0;
    /** @brief Positive finite maximum absolute error in output units, per axis. */
    double maxAbsoluteError = 1e-9;
};

/** @brief Owning relative offset and conservative arithmetic error bounds in output units. */
struct CellProjection {
    std::array<double, 3> offset{};
    std::array<double, 3> absoluteErrorBound{};
};

/**
 * @brief Carry a finite local offset into the cell address to obtain fractions in [-0.5, 0.5).
 * @param position Owning value; fractions may initially lie outside the canonical interval.
 * @return Exact canonical representation of the supplied binary64 value, or InvalidArgument for
 * nonfinite fractions / Unsupported when the resulting integer address cannot fit int64.
 * @ownership Input and result own their values; failure publishes no partial axes.
 * @thread Reentrant, no callbacks or shared state; IEEE binary64, no fast-math/FTZ required.
 * @cost Constant work and storage per position; does not visit any frame tree or scene.
 * @remarks Does not recover precision already lost when the caller formed the input offset.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<CellPosition> normalizeCellPosition(CellPosition position);

/**
 * @brief Project position minus origin without converting absolute cell addresses to floating point.
 * @param position Value-owned target in the same lattice as origin.
 * @param origin Value-owned local reference; no scene rebasing is performed.
 * @param policy Explicit scale and per-axis absolute tolerance.
 * @return Owning projection, InvalidArgument for invalid input, or Unsupported when range/precision
 * cannot meet the budget. Failure publishes no partial result and changes no engine state.
 * @ownership Inputs and output own their data; nothing is retained.
 * @thread Reentrant, no shared state or callbacks. Requires IEEE binary64 with gradual underflow,
 * without fast-math/flush-to-zero; bounds cover arithmetic, not input quantization or physical error.
 * @cost Constant work and storage per position (three axes); no allocation on success.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<CellProjection> projectCellRelative(CellPosition         position,
                                                                                 CellPosition         origin,
                                                                                 CellProjectionPolicy policy);

}  // namespace eve::spatial
