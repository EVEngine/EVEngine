#pragma once

namespace ssq {
class Table;
}

namespace eve::procgen {

/** @brief Register biome rules on the Procgen module table. */
void exposeBiomeRules(ssq::Table& table);

}  // namespace eve::procgen
