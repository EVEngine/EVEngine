#pragma once

/** @file ActionBlockRuntime.h @brief Deterministic notify-state lifecycle routing. */

#include "action/ActionNotifyRegistry.h"

#include <map>
#include <vector>

namespace eve::action {

/**
 * @brief Owner-thread lifecycle router for timeline notifies and active blocks.
 *
 * ActionRuntime remains the timeline authority. This adapter retains only the
 * last owning block projection per execution so interruption can emit paired
 * exits. The borrowed registry must outlive this object. Handler callbacks are
 * synchronous, run without locks, and may reenter unrelated systems but must
 * not reenter this instance.
 */
class ActionBlockRuntime {
public:
    /**
     * @brief Bind the canonical registry used for all synchronous callbacks.
     * @param registry Borrowed owner-thread registry.
     */
    explicit ActionBlockRuntime(ActionNotifyRegistry& registry) noexcept : registry_(registry) {}

    ActionBlockRuntime(const ActionBlockRuntime&)            = delete;
    ActionBlockRuntime& operator=(const ActionBlockRuntime&) = delete;

    /**
     * @brief Deliver boundaries and active updates from one authoritative advance.
     * @param advance Owning result produced by ActionRuntime.
     * @param context Owning execution/target context; its id must match advance.
     * @return Applied after all handlers succeed, otherwise the first handler failure.
     */
    [[nodiscard]] Result<void> apply(const ActionAdvance& advance, ActionNotifyContext context);

    /**
     * @brief Emit paired exits for all blocks retained for one interrupted execution.
     * @param context Owning context identifying the execution to interrupt.
     * @return Applied/NoOp or the first exit failure; successfully exited blocks are removed.
     */
    [[nodiscard]] Result<void> interrupt(ActionNotifyContext context);

    /**
     * @brief Evaluate active blocks for editor scrubbing without retaining lifecycle state.
     * @param blocks Owning block samples from ActionTimeline::activeBlocks.
     * @param context Preview context; preview and scrubbing are forced true.
     */
    [[nodiscard]] Result<void> sample(const std::vector<ActionActiveBlock>& blocks,
                                      ActionNotifyContext                  context) const;

    /** @brief Number of execution projections retained for interruption cleanup. */
    [[nodiscard]] std::size_t executionCount() const noexcept { return active_.size(); }

private:
    ActionNotifyRegistry&                                      registry_;
    std::map<ActionExecutionId, std::vector<ActionActiveBlock>> active_;
};

}  // namespace eve::action
