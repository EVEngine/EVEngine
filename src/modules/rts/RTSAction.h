#pragma once
#include "common/Export.h"


/**
 * @file RTSAction.h
 * @brief RTS adapter boundary over the shared action lifecycle.
 */

#include "rts/RTSTypes.h"

#include <memory>
#include <span>

namespace eve::action {
class ActionRuntime;
struct ActionEntityBinding;
}

namespace eve::rts {
class RTSActionCapture;
class PreparedRTSActionRestore;

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
    /** @brief Releases IRTSActionExecutor resources. */
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
class EVENGINE_API_DOMAINS ActionAdapter final : public IRTSActionExecutor {
public:
    /** @brief Bind the adapter to an owner-thread action runtime. */
    explicit ActionAdapter(action::ActionRuntime& runtime);
    /** @brief Action adapter. */
    ~ActionAdapter() override;

    ActionAdapter(const ActionAdapter&)            = delete;
    ActionAdapter& operator=(const ActionAdapter&) = delete;

    /** @copydoc IRTSActionExecutor::execute */
    /** @brief Execute. */
    [[nodiscard]] Result<ActionExecutionResult> execute(Unit& unit, const OrderRecord& order,
                                                        const SimulationStep& step) override;
    /** @brief Forget runtime execution correlations so restored orders are rebound on their next step. */
    void clear() noexcept;

    /** @brief Freeze native actions and their order correlations at one idle owner-thread boundary.
     * @ownership Output owns facts only; world and bindings are borrowed for this call, outside Views.
     * @return Detached facts or invalid source/correlation/runtime failure; no callbacks or time advance.
     * @cost Copies all native executions and pending order strings per checkpoint attempt.
     */
    [[nodiscard]] Result<RTSActionCapture> capture(PersistentId saveEpoch, std::shared_ptr<ecs::Table> world,
                                                   std::span<const action::ActionEntityBinding> bindings) const;
    /** @brief Publish prepared native actions and correlations together, without resubmitting orders.
     * @return Applied or unchanged target/token on validation failure; success consumes the token.
     * @thread Owner thread outside Views; invokes no service callbacks and allocates no publication state.
     */
    [[nodiscard]] Result<void> restore(PreparedRTSActionRestore&& prepared);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace eve::rts
