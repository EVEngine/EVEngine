#pragma once

#include "common/Module.h"

namespace eve::rpg::balance {
/** @brief Script registration owner for the RPG balance suite. */
class EVENGINE_API_BACKENDS RPGBalance : public Module {
public:
    Module_REG(RPGBalance);
};
}  // namespace eve::rpg::balance
