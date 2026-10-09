#pragma once

namespace ssq {
class Table;
}
namespace eve::procgen {
/** @brief Bind the VM-owned assembly planner synchronously on the VM owner thread. */
void exposeModuleAssembly(ssq::Table& table);
}  // namespace eve::procgen
