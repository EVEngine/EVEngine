#pragma once
#include "common/Export.h"
namespace ssq {
class Table;
}
namespace eve::physics {
/** @brief Register owning transmission Result factories; table is borrowed on the VM owner thread during this call. */
EVENGINE_API_WORLD void exposeCoupling3DBindings(ssq::Table& table);
}  // namespace eve::physics
