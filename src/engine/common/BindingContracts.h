#pragma once

#include "common/Export.h"

namespace eve::script {

class BindingContractRegistry;

/** @brief Registers the build-generated contracts for every SimpleSquirrel binding. */
EVENGINE_API_FOUNDATION void registerEngineBindingContracts(BindingContractRegistry& registry);

/**
 * @brief Process-wide generated Binding Contracts for this linked engine build.
 * @return Shared registry populated once from registerEngineBindingContracts.
 * @ownership Borrowed; the process retains the registry.
 * @lifetime Valid for the remainder of the process.
 * @thread Safe to read from the main / MCP poll thread after static initialization.
 */
EVENGINE_API const BindingContractRegistry& generatedBindingContracts();

}  // namespace eve::script
