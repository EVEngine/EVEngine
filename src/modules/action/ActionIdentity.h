#pragma once
/** @file ActionIdentity.h
 * @brief Action-owned in-process execution identity, independent of timeline and service headers.
 */
#include "common/StrongUint64.h"
namespace eve::action {
/** @brief Opaque runtime identity for one ActionExecution, not a persistent entity/account identity. */
struct ActionExecutionIdTag {};
using ActionExecutionId = eve::detail::StrongUint64<ActionExecutionIdTag>;
}  // namespace eve::action
