#pragma once

#include "common/Export.h"

namespace eve::script {

class BindingContractRegistry;

/** @brief Registers the build-generated contracts for every SimpleSquirrel binding. */
EVENGINE_API void registerEngineBindingContracts(BindingContractRegistry& registry);

}  // namespace eve::script
