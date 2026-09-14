#pragma once
namespace ssq {
class Table;
}
namespace eve::procgen {
/** @brief Register owner-thread runtime-generation script interfaces. */
void exposeRuntimeGeneration(ssq::Table& table);
}  // namespace eve::procgen
