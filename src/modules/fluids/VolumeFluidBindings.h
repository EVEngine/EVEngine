#pragma once
namespace ssq {
class Table;
class Class;
}  // namespace ssq
namespace eve::fluids {
/** @brief Registers the script-owned volume-fluid class on the VM thread. */
void exposeVolumeFluidType(ssq::Table& table);
/** @brief Registers volume-fluid factories on the existing Fluids class. */
void exposeVolumeFluidFactory(ssq::Class& cls);
}  // namespace eve::fluids
