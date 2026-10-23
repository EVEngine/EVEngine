#pragma once

namespace ssq {
class Table;
}
namespace eve::procgen {
/** @brief Register owning generation sessions on the VM owner thread; no callbacks are retained. */
void exposeGenerationStages(ssq::Table& table);
}  // namespace eve::procgen
