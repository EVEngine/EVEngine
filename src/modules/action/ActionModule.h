#pragma once
#include "common/Export.h"

/** @file ActionModule.h @brief Script-facing factory for ActionRuntime + AbilityRuntime. */

#include "common/Module.h"

namespace eve::action {

/**
 * @brief Factory module for script-owned action/ability protocol runtimes.
 *
 * Each runtime uniquely owns one ActionRuntime and a borrowing AbilityRuntime.
 * The module itself stores no arena state. Phase, cooldown and grant identity
 * remain owned by those C++ runtimes; script only injects tick/delta and
 * observes Result projections.
 */
class EVENGINE_API_PLATFORM Action final : public Module {
public:
    Module_REG(Action);
    Action()           = default;
    ~Action() override = default;
};

}  // namespace eve::action
