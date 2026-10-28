#pragma once

namespace ssq {
class Table;
}

namespace eve::procgen {

/** @brief Register PointGraph script types on the Procgen module table. */
void exposePointGraph(ssq::Table& table);

}  // namespace eve::procgen
