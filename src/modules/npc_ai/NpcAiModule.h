#pragma once
#include "common/Export.h"

/** @file NpcAiModule.h @brief Script-facing factory for NpcAiWorld instances. */

#include "common/Module.h"

namespace eve::npc_ai {

/**
 * @brief Factory module for script-owned NPC AI worlds.
 *
 * Each world uniquely owns behavior definitions, agent slots, perception
 * memory and the deterministic scheduler. Task providers remain C++-only;
 * script can drive signal/blackboard/perception agents without them.
 */
class EVENGINE_API_PLATFORM NpcAi final : public Module {
public:
    Module_REG(NpcAi);
    NpcAi()           = default;
    ~NpcAi() override = default;
};

}  // namespace eve::npc_ai
