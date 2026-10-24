#pragma once
#include "common/Export.h"

/** @file ActionNotifyPlayback.h
 *  @brief Host-owned composition of ActionNotifyRegistry + ActionBlockRuntime.
 *
 *  This is the canonical notify playback face for gameplay and editor hosts.
 *  ScriptActionRuntime deliberately does **not** own this pipeline: it only
 *  projects ActionRuntime / AbilityRuntime phases. Callers that need
 *  Enter/Update/Exit dispatch compose an ActionNotifyPlayback beside their
 *  ActionRuntime advances.
 */

#include "action/ActionBlockRuntime.h"

#include <memory>

namespace eve::action {

/**
 * @brief Owner-thread notify playback host.
 *
 * Owns the descriptor/handler registry and the per-execution block lifecycle
 * router. ActionRuntime remains the sole phase/timeline sampling authority;
 * hosts call `apply` with each ActionAdvance they already produced.
 *
 * Not movable: ActionBlockRuntime borrows the owned registry by reference.
 * Prefer `withBuiltins()` / heap ownership when the value must cross a factory.
 *
 * @ownership Unique; must outlive every borrowed ActionNotifyContext reference
 *            passed into apply/interrupt/sample for the duration of those calls.
 * @thread Owner/main thread only; handler callbacks are synchronous.
 */
class EVENGINE_API_PLATFORM ActionNotifyPlayback {
public:
    /** @brief Build heap-owned playback with the engine's standard notify descriptors/handlers. */
    [[nodiscard]] static Result<std::unique_ptr<ActionNotifyPlayback>> withBuiltins();

    /**
     * @brief Take ownership of an already-configured registry.
     * @param registry Owning registry moved into this playback.
     */
    explicit ActionNotifyPlayback(ActionNotifyRegistry registry);

    ActionNotifyPlayback(const ActionNotifyPlayback&)            = delete;
    ActionNotifyPlayback& operator=(const ActionNotifyPlayback&) = delete;
    ActionNotifyPlayback(ActionNotifyPlayback&&)                 = delete;
    ActionNotifyPlayback& operator=(ActionNotifyPlayback&&)      = delete;

    /** @brief Mutable registry for installing provider handlers. */
    [[nodiscard]] ActionNotifyRegistry& registry() noexcept { return registry_; }
    /** @brief Borrowed registry for authoring/descriptor queries. */
    [[nodiscard]] const ActionNotifyRegistry& registry() const noexcept { return registry_; }

    /**
     * @brief Deliver boundaries and active updates from one ActionRuntime advance.
     * @see ActionBlockRuntime::apply
     */
    [[nodiscard]] Result<void> apply(const ActionAdvance& advance, ActionNotifyContext context);

    /**
     * @brief Emit paired exits for one interrupted execution.
     * @see ActionBlockRuntime::interrupt
     */
    [[nodiscard]] Result<void> interrupt(ActionNotifyContext context);

    /**
     * @brief Side-effect-free preview sample path.
     * @see ActionBlockRuntime::sample
     */
    [[nodiscard]] Result<void> sample(const std::vector<ActionActiveBlock>& blocks, ActionNotifyContext context) const;

    /** @brief Number of execution projections retained for interruption cleanup. */
    [[nodiscard]] std::size_t executionCount() const noexcept { return blocks_.executionCount(); }

private:
    ActionNotifyRegistry registry_;
    ActionBlockRuntime   blocks_{registry_};
};

}  // namespace eve::action
