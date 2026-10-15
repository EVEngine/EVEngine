#pragma once

#include "common/Export.h"

namespace ssq {
class Table;
}
namespace eve::fluids {
/** @brief Register owning ocean script types on the VM owner thread; table is borrowed only during registration. */
EVENGINE_API_DOMAINS void exposeOceanTypes(ssq::Table& table);
/** @brief Register owning wake/boat adapters; VM owner thread, call-only table, no runtime objects retained. */
EVENGINE_API_DOMAINS void exposeOceanInteractionTypes(ssq::Table& table);
/** @brief Register owning render projections on the VM/render thread; table is borrowed only during registration. */
EVENGINE_API_DOMAINS void exposeOceanRenderTypes(ssq::Table& table);
/** @brief Register owning spray simulation/render types; owner-thread, call-only table, no retained inputs. */
EVENGINE_API_DOMAINS void exposeOceanSprayTypes(ssq::Table& table);
}  // namespace eve::fluids
