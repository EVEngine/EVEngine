#pragma once

#include "procgen/Grid2D.h"

#include <string>

namespace eve::procgen {

/** Compact JSON for debugging / reload workflows (not full Tiled). */
std::string gridToJson(const Grid2D &grid);

bool writeGridJson(const Grid2D &grid, const std::string &path, std::string *error);

}  // namespace eve::procgen
