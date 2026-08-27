#pragma once

/** @file SchemaVersion.h @brief Persistent data-contract version identity. */

#include "common/StrongUint64.h"

namespace eve {
namespace detail {
struct SchemaVersionTag {};
}
/** @brief Persistent data-format version; not a runtime replacement generation. */
using SchemaVersion = detail::StrongUint64<detail::SchemaVersionTag>;
}  // namespace eve
