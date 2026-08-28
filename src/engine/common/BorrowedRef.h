#pragma once

/**
 * @file BorrowedRef.h
 * @brief Vocabulary aliases for nullable and checked borrowed references.
 *
 * These aliases express lookup results without implying ownership or extending
 * the lifetime of the referenced object. The referenced object remains owned
 * by its registry/container and must outlive the use of the returned value.
 */

#include "common/Result.h"

#include <functional>
#include <optional>

namespace eve {

/**
 * @brief Optional borrowed reference; it does not extend `T`'s lifetime.
 * @tparam T Referenced object type, possibly const-qualified.
 */
template <typename T>
using OptionalRef = std::optional<std::reference_wrapper<T>>;

/**
 * @brief Checked borrowed reference carried by `Result`; it does not own `T`.
 * @tparam T Referenced object type, possibly const-qualified.
 */
template <typename T>
using ResultRef = Result<std::reference_wrapper<T>>;

}  // namespace eve
