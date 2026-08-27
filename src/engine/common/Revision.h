#pragma once

/** @file Revision.h @brief Monotonic state revision for concurrency and change checks. */

#include "common/StrongUint64.h"

namespace eve {
namespace detail {
struct RevisionTag {};
}
/** @brief Monotonic content/state revision used for optimistic-concurrency checks. */
using Revision = detail::StrongUint64<detail::RevisionTag>;
}  // namespace eve
