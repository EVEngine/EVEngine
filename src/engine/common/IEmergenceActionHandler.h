#pragma once

/**
 * @file IEmergenceActionHandler.h
 * @brief Optional multi-listener sink for emergence rule actions outside built-ins.
 */

#include "common/Result.h"
#include "common/Value.h"

#include <string_view>

namespace eve {

/**
 * @brief Capability listener that may consume one emergence action kind.
 *
 * Providers register with `eve::cap::addListener<IEmergenceActionHandler>(...)`.
 * The emergence engine invokes listeners in priority order until one returns
 * `success(true)` (consumed). Built-in `fact.set` / `economy.*` never reach here.
 *
 * @thread Owning simulation thread of the RuleEngine that is draining.
 * @reentrancy Handlers must not call back into the draining RuleEngine; enqueue
 *             work for a later drain instead.
 * @lifetime Registered instances must outlive their registry entries.
 */
class IEmergenceActionHandler {
public:
    static constexpr const char* capabilityName = "IEmergenceActionHandler";

    virtual ~IEmergenceActionHandler() = default;

    /**
     * @brief Attempt to execute one deferred emergence action.
     * @param kind Stable action kind (for example `quest.notify`).
     * @param args Owning argument object owned by the activation; do not retain.
     * @return `success(true)` when consumed, `success(false)` to continue, or a
     *         structured failure that aborts the remaining handlers for this action.
     */
    [[nodiscard]] virtual eve::Result<bool> tryHandle(std::string_view kind, const eve::Value& args) = 0;
};

}  // namespace eve
