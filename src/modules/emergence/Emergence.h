#pragma once
#include "common/Export.h"

/**
 * @file Emergence.h
 * @brief Script module factory for indexed emergent rule engines.
 */

#include "common/Module.h"
#include "common/Result.h"
#include "common/SquirrelOwnership.h"
#include "emergence/RuleEngine.h"

namespace eve::emergence {

/**
 * @brief Script-facing module that owns RuleEngine instances.
 *
 * Games bind facts from quest/economy/world systems into a RuleEngine, then
 * call `drain(tick)` once per simulation step. See
 * `docs/dev/涌现式规则触发框架.md`.
 */
class EVENGINE_API_FOUNDATION Emergence : public Module {
public:
    Module_REG(Emergence);
    Emergence()           = default;
    ~Emergence() override = default;

    /**
     * @brief Allocate a module-owned rule engine.
     * @return Generation-qualified handle, or allocation failure.
     * @ownership Module-owned until release(); the handle is not an owning pointer.
     * @thread Module owner / simulation thread.
     * @reentrancy No callbacks are invoked.
     */
    [[nodiscard]] static eve::Result<RuleEngineHandleRef> newEngine();

    /**
     * @brief Resolve a live engine as a non-owning observation.
     * @ownership Borrowed from the module registry.
     * @lifetime Until release or module destruction; do not retain across either.
     */
    [[nodiscard]] static eve::script::Borrowed<RuleEngine> resolve(RuleEngineHandleRef reference) noexcept;

    /** @brief Release a module-owned engine. */
    [[nodiscard]] static eve::Result<void> release(RuleEngineHandleRef reference);

    /** @brief Report whether a handle is stale. */
    [[nodiscard]] static bool isStale(RuleEngineHandleRef reference) noexcept;

private:
    eve::script::RuntimeObjectRegistry<RuleEngine, RuleEngineHandleTag> engines_;
};

}  // namespace eve::emergence
