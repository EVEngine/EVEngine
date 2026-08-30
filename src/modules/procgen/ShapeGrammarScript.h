#pragma once

namespace ssq {
class Table;
}

namespace eve::procgen {

/** @brief Register shape-grammar script types on the Procgen module table. */
void exposeShapeGrammar(ssq::Table& table);

}  // namespace eve::procgen
