#pragma once

/** @file ClimbingPrimitives.h @brief Shared value types with no climbing runtime dependency. */

#include "common/StrongUint64.h"

namespace eve::climbing {

namespace detail {
struct ClimbingExecutionIdTag {};
}

/** @brief Stable non-zero identity assigned when a climbing selection transaction commits. */
using ClimbingExecutionId = eve::detail::StrongUint64<detail::ClimbingExecutionIdTag>;

/** @brief Small owning world-space vector used at climbing module boundaries. */
struct Vec3 {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
};

}  // namespace eve::climbing
