#pragma once
#include <memory>
#include "common/Export.h"
#include "common/Time.h"

namespace eve::physics {
struct ForceStepState;
/** @brief Completion of the world step to which a submitted force batch belongs. */
enum class ForceStepStatus { Pending, Applied, Cancelled };
/** @brief Read-only completion receipt, safe to retain after world destruction.
 * @ownership Shares only receipt storage; does not retain a world or body. World owns status transitions.
 * @thread Query on the simulation owner thread, serialized with world mutation; no callbacks.
 * @details Applied proves the requested world step succeeded, not that subsequently destroyed targets survived.
 * Pending survives backend failure. Destroy or successful restore cancels pending receipts.
 * Receipts are transient and are not serialized. Discarding a receipt does not cancel submitted forces.
 */
class EVENGINE_API_WORLD ForceReceipt {
public:
    /** @brief Current completion status; a moved-from receipt is Cancelled. */
    [[nodiscard]] ForceStepStatus status() const noexcept;
    /** @brief Requested tick and duration; a moved-from receipt returns the zero step. */
    [[nodiscard]] SimulationStep step() const noexcept;

private:
    friend class ForceBatch;
    explicit ForceReceipt(std::shared_ptr<const ForceStepState> state) : state_(std::move(state)) {}
    std::shared_ptr<const ForceStepState> state_;
};
}  // namespace eve::physics
