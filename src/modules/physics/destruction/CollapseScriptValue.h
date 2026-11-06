#pragma once
#include "common/BorrowedRef.h"
#include "common/SquirrelOwnership.h"
#include "physics/destruction/CollapseAnimationInternal.h"

namespace eve::physics::collapse_detail {
/** @brief Type-check a VM-owned bake argument before obtaining its native reference.
 * @ownership Borrowed only during this native binding call; never store the returned reference across frames.
 * @lifetime The input object remains rooted by the active VM call. No callbacks or locks are used.
 * @return InvalidArgument for null, wrong native class or invalid wrapper, through the common Result channel.
 */
template <class T>
[[nodiscard]] ResultRef<T> scriptValue(const ssq::Object& object) {
    if (object.getType() == ssq::Type::INSTANCE && object.getTypeTag() == script::detail::squirrelTypeHash<T*>()) {
        try {
            if (auto* value = object.to<T*>()) return ResultRef<T>::success(std::ref(*value));
        } catch (const ssq::TypeException&) {
            // Malformed native wrappers are ordinary invalid arguments at this boundary.
        }
    }
    return ResultRef<T>::failure(invalid("native bake argument has the wrong type or is null"));
}
}  // namespace eve::physics::collapse_detail
