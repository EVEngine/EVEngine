#pragma once

#include "common/Export.h"
namespace ssq { class Table; }
namespace eve::scene {
/** @brief Register Pcg location profile value types and operations. */
EVENGINE_API_PLATFORM void exposeLocationProfileBindings(ssq::Table& table);
}
