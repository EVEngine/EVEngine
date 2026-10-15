#pragma once
#include "common/Export.h"

/** @file ActionModule.h @brief Script-facing factory module for action/ability runtimes. */

#include "common/Module.h"

namespace eve::action {

/**
 * @brief Factory module for script-owned action/ability protocol runtimes.
 *
 * The module itself stores no arena state. Call `newRuntime()` (bound in
 * `ActionScriptBindings`) to create a `ScriptActionRuntime` that uniquely owns
 * one `ActionRuntime` and a borrowing `AbilityRuntime`. Phase, cooldown and
 * grant identity remain owned by those C++ runtimes; script only injects
 * tick/delta and observes Result projections.
 *
 * C++ gameplay code should include `action/Action.h` / `action/AbilitySystem.h`
 * and use those runtimes directly instead of going through this module.
 */
class EVENGINE_API_PLATFORM Action final : public Module {
public:
    Module_REG(Action);
    /** @brief Construct the process-local Action module instance. */
    Action() = default;
    /** @brief Releases Action module resources. */
    ~Action() override = default;
};

}  // namespace eve::action
