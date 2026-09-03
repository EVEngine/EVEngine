#pragma once

/** @file RTSTech.h @brief Research settlement over canonical Definitions and Production. */

#include "common/Result.h"
#include "definitions/Definitions.h"

#include <cstddef>

namespace eve::rts {

/** @brief Settles completed research tasks and projects unlocked upgrades idempotently. */
class TechnologySystem {
public:
    /** @brief Consume completed `research` tasks and apply their canonical upgrade definitions. */
    [[nodiscard]] static Result<std::size_t> step(definitions::DefinitionRegistry& registry);
};

}  // namespace eve::rts
