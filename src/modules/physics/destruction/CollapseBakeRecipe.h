#pragma once
#include "physics/bake/BakeRecipe.h"
namespace ssq {
class Table;
}
namespace eve::physics {
/** @brief Compatibility name for the canonical checked BakeRecipe API; encoding remains EVECREC1. */
using CollapseBakeRecipe = BakeRecipe;
/** @brief Register the owning recipe builder and checked script admission methods. */
void exposeCollapseBakeRecipe(ssq::Table& table);
}  // namespace eve::physics
