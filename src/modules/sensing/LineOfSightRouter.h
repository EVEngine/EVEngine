#pragma once

/**
 * @file LineOfSightRouter.h
 * @brief One registered line-of-sight provider that dispatches to backends by coordinate space.
 *
 * `eve.sensing.ILineOfSightQuery` is a **single-slot** capability, and its own contract already
 * says a provider may support only some coordinate spaces and must return `Unsupported` for the
 * rest — the class remarks in `Targeting.h` state it. A project normally has several unrelated sight
 * implementations - the 3D physics probe for world points, a grid walk for a hex or square
 * board - and registering them directly against the capability means the last registration wins
 * and the effective behaviour depends on module load order.
 *
 * This router is the provider that gets registered once. Each backend claims the coordinate
 * spaces it can interpret; a space with no backend is answered with `Unsupported` (naming the
 * space), and a query whose endpoints are in different spaces is rejected rather than converted,
 * because the interface forbids implicit conversion between spaces.
 */

#include "common/Result.h"
#include "sensing/Targeting.h"

#include <array>
#include <string_view>
#include <vector>

namespace eve::sensing {

/**
 * @brief Routes each line-of-sight query to the backend that claimed its coordinate space.
 *
 * @thread Affine to the caller; it holds borrowed pointers and does no synchronization. Claim all
 *         backends during startup, before queries run.
 * @reentrancy `query` invokes the selected backend synchronously and performs no locking.
 */
class LineOfSightRouter final : public ILineOfSightQuery {
public:
    LineOfSightRouter() = default;

    LineOfSightRouter(const LineOfSightRouter&)            = delete;
    LineOfSightRouter& operator=(const LineOfSightRouter&) = delete;

    /**
     * @brief Claim one coordinate space for a backend.
     * @param space Space this backend can interpret; it must be one of the four enum values.
     * @param provider Backend to answer with.
     * @return Applied, InvalidArgument for a null provider, or Conflict when the space is taken.
     * @ownership Borrowed: the router never owns or deletes the backend. The claiming module must
     *            keep it alive for as long as it is claimed.
     * @lifetime Valid until @ref removeProvider releases that exact space and provider.
     */
    [[nodiscard]] Result<void> addProvider(CoordinateSpace space, const ILineOfSightQuery* provider);

    /**
     * @brief Release a claimed space when @p provider is the one that claimed it.
     * @return Applied when released, or NoOp when this provider does not own that space (so one
     *         module can never release another module's backend, and a double release is safe).
     */
    [[nodiscard]] Result<void> removeProvider(CoordinateSpace space, const ILineOfSightQuery* provider);

    /** @brief Whether some backend currently answers for @p space. */
    [[nodiscard]] bool hasProvider(CoordinateSpace space) const noexcept;

    /** @brief Claimed spaces in enum order, so a caller can report coverage deterministically. */
    [[nodiscard]] std::vector<CoordinateSpace> spaces() const;

    /**
     * @brief Answer one segment query by routing it to the backend for its coordinate space.
     * @return The backend's answer, Rejected when the two endpoints are in different spaces, or
     *         Unsupported when no backend claimed that space.
     */
    [[nodiscard]] Result<LineOfSightResult> query(const TargetLocation& from,
                                                 const TargetLocation& to) const override;

private:
    /** @brief Space of a location, or nullopt when the variant is somehow empty. */
    [[nodiscard]] static std::optional<CoordinateSpace> spaceOf(const TargetLocation& location) noexcept;

    static constexpr std::size_t kSpaceCount = 4;
    /** Borrowed backends, indexed by the numeric value of CoordinateSpace. */
    std::array<const ILineOfSightQuery*, kSpaceCount> providers_{};
};

}  // namespace eve::sensing
