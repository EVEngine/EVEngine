#pragma once

namespace ssq {
class Table;
}

namespace eve::procgen {

/** @brief Register MeshModifierGraph script types on the Procgen module table. */
void exposeMeshModifierGraph(ssq::Table& table);

}  // namespace eve::procgen
