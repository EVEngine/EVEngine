#pragma once

#include "editing/EditingResult.h"
#include "editing/EditingValue.h"

namespace eve::editing {

/**
 * @brief Read an Integer or Number as an owning double without applying domain validation.
 * @param value Borrowed stable input, used synchronously and never retained.
 * @return Numeric value, or TypeMismatch for other kinds. Nonfinite doubles are preserved.
 * @remarks Required/optional fields, finite checks and domain ranges remain caller policy.
 * @note Callable on any thread with stable input; no callbacks or shared mutable state.
 */
[[nodiscard]] EVENGINE_API_PLATFORM Result<double> numericValue(const Value& value);

}  // namespace eve::editing
