#pragma once

/** @file EventSequence.h @brief Stream-local event ordering value. */

#include "common/StrongUint64.h"

namespace eve {
namespace detail {
/** @brief EventSequenceTag public API. */
struct EventSequenceTag {};
}  // namespace detail
/** @brief Stream-local event ordering value; it is not a global event identity. */
using EventSequence = detail::StrongUint64<detail::EventSequenceTag>;
}  // namespace eve
