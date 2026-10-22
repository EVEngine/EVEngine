#pragma once
#include "common/Export.h"
namespace ssq {
class Table;
}
namespace eve::physics {
/**
 * @brief Register force model value classes and Result-projected functions.
 * @ownership Table is borrowed for registration only; VM owns registered classes.
 * @thread VM owner thread only; does not invoke user callbacks.
 */
EVENGINE_API_WORLD void exposeForceBindings(ssq::Table& table);
}  // namespace eve::physics
