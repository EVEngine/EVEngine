#pragma once

/** @file Generation.h @brief Runtime replacement generation for stale-handle rejection. */

#include "common/StrongUint64.h"

namespace eve {
namespace detail {
struct GenerationTag {};
}  // namespace detail
/** @brief Registry/object replacement generation used to reject stale handles. */
using Generation = detail::StrongUint64<detail::GenerationTag>;
}  // namespace eve
