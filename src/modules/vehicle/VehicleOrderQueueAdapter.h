#pragma once

/**
 * @brief Vehicle-to-orders compatibility adapter.
 *
 * The adapter owns the generic order lifecycle while retaining VehicleOrder as
 * the Vehicle domain payload.  Vehicle commands must not be added to the
 * generic orders module; the adapter only maps their kind and stable id.
 */

#include "vehicle/VehicleTypes.h"
#include "common/Result.h"

#include <memory>
#include <string>

namespace eve::vehicle {

/**
 * @brief Maps VehicleOrder domain payloads onto an orders::CommandQueue.
 *
 * The adapter owns its queue and domain payload map.  The observation returned
 * by current() is borrowed until the next mutating adapter call.  The legacy
 * Orders::queue/current fields are refreshed by syncCompatibility() and are a
 * read-only compatibility projection for existing Vehicle consumers.
 */
class VehicleOrderQueueAdapter {
public:
    /** @brief Creates an empty adapter with an owned generic queue. */
    VehicleOrderQueueAdapter();
    /** @brief Releases the owned generic queue and domain payloads. */
    ~VehicleOrderQueueAdapter();

    /** @brief Deep-copies generic lifecycle state and Vehicle payloads. */
    VehicleOrderQueueAdapter(const VehicleOrderQueueAdapter&);
    /** @brief Deep-copies generic lifecycle state and Vehicle payloads. */
    VehicleOrderQueueAdapter& operator=(const VehicleOrderQueueAdapter&);
    /** @brief Transfers adapter ownership without copying queue state. */
    VehicleOrderQueueAdapter(VehicleOrderQueueAdapter&&) noexcept;
    /** @brief Transfers adapter ownership without copying queue state. */
    VehicleOrderQueueAdapter& operator=(VehicleOrderQueueAdapter&&) noexcept;

    /** @brief Appends a Vehicle domain order to the generic queue. */
    [[nodiscard]] eve::Result<std::string> append(const VehicleOrder& order, int priority = 0,
                                                  double timeoutSeconds = 0.0);
    /** @brief Cancels unfinished orders and starts a replacement. */
    [[nodiscard]] eve::Result<std::string> replace(const VehicleOrder& order, int priority = 0,
                                                   double timeoutSeconds = 0.0);
    /** @brief Interrupts the active order when the generic priority permits. */
    [[nodiscard]] eve::Result<std::string> interrupt(const VehicleOrder& order, int priority = 0,
                                                     double timeoutSeconds = 0.0);

    /** @brief Completes the current generic order and activates the next one. */
    [[nodiscard]] eve::Result<void> completeCurrent();
    /** @brief Fails the current generic order and activates the next one. */
    [[nodiscard]] eve::Result<void> failCurrent(const std::string& reason);
    /** @brief Cancels an active or queued generic order by stable id. */
    [[nodiscard]] eve::Result<void> cancel(const std::string& id, const std::string& reason = "cancelled");
    /** @brief Advances generic timeout handling. */
    void update(double dtSeconds);
    /** @brief Clears generic queue history and Vehicle domain payloads. */
    void clear();

    /**
     * @brief Returns the Vehicle payload for the active generic order, or null.
     * @return Borrowed nullable payload owned by this adapter.
     * @ownership VehicleOrderQueueAdapter owns the payload; callers must not delete it.
     * @lifetime Valid until the next mutating adapter call, clear(), or destruction.
     * @thread Call on the adapter's owning simulation thread.
     * @reentrancy Do not retain or re-enter adapter mutation while observing the result.
     */
    [[nodiscard]] const VehicleOrder* current() const;
    /** @brief Returns the number of active and queued Vehicle orders. */
    [[nodiscard]] int activeOrQueuedCount() const;

    /**
     * @brief Refreshes the legacy component projection.
     * @param legacy VehicleEntity::Orders component to update.
     * @remarks The adapter remains the only lifecycle authority.
     */
    void syncCompatibility(VehicleEntity::Orders& legacy) const;

private:
    struct Impl;
    void pruneTerminalPayloads();

    std::unique_ptr<Impl> impl_;
};

}  // namespace eve::vehicle
