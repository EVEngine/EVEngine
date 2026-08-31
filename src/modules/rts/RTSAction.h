#pragma once

/**
 * @file RTSAction.h
 * @brief RTS adapter boundary over the shared action lifecycle.
 */

#include "rts/RTSTypes.h"

#include <memory>

namespace eve::action {
class ActionRuntime;
}

namespace eve::rts {

/** @brief Outcome of one RTS order-to-action adapter call. */
enum class ActionDisposition : std::uint8_t {
    Pending,
    Completed,
};

/** @brief Owning, domain-local projection of an action adapter outcome. */
struct ActionExecutionResult {
    ActionDisposition disposition = ActionDisposition::Pending;
};

/**
 * @brief Interface isolating RTS orders from the shared action runtime.
 *
 * Implementations may use any action provider, but they must not copy the
 * action lifecycle or retain a Unit pointer. The unit reference is borrowed
 * for this call only and the injected SimulationStep is the sole time input.
 */
class IRTSActionExecutor {
public:
    virtual ~IRTSActionExecutor() = default;

    /**
     * @brief Execute or advance one active RTS order.
     * @param unit Unit whose order is being serviced; valid for this call only.
     * @param order Owning projection of the current generic order.
     * @param step Deterministic simulation step supplied by the caller.
     * @return Pending or Completed, or a structured provider failure.
     */
    [[nodiscard]] virtual Result<ActionExecutionResult> execute(Unit& unit, const OrderRecord& order,
                                                                const SimulationStep& step) = 0;
};

/**
 * @brief Adapter that maps RTS orders to the existing action::ActionRuntime.
 *
 * ActionRuntime is borrowed and must outlive this adapter on the same owner
 * thread. Pending executions are keyed by ECS generation plus order id, so
 * slot reuse cannot resume the old action.
 */
class ActionAdapter final : public IRTSActionExecutor {
public:
    /** @brief Bind the adapter to an owner-thread action runtime. */
    explicit ActionAdapter(action::ActionRuntime& runtime);
    ~ActionAdapter() override;

    ActionAdapter(const ActionAdapter&)            = delete;
    ActionAdapter& operator=(const ActionAdapter&) = delete;

    /** @copydoc IRTSActionExecutor::execute */
    [[nodiscard]] Result<ActionExecutionResult> execute(Unit& unit, const OrderRecord& order,
                                                        const SimulationStep& step) override;
    /** @brief Forget runtime execution correlations so restored orders are rebound on their next step. */
    void clear() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace eve::rts
