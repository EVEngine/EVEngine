#pragma once
#include "common/Export.h"
namespace ssq {
class Table;
}
namespace eve::procgen {
/** @brief Register material map and optical metadata accessors; owner-thread only.
 * @param table Borrowed module table, not retained. No application callbacks are invoked. */
EVENGINE_API_DOMAINS void exposePbrMaterial(ssq::Table &table);
}  // namespace eve::procgen
