#pragma once
#include "common/Module.h"
namespace eve::physics {
/** @brief Optional fluid-backed destruction presentation module; no ECS systems or retained service links. */
class EVENGINE_API_DOMAINS DestructionDust final : public Module {
public:
    Module_REG(DestructionDust);
};
}  // namespace eve::physics
