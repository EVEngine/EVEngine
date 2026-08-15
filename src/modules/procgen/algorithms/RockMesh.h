#pragma once

#include "procgen/MeshBuild.h"
#include "procgen/Params.h"

#include <string>

namespace eve::procgen {

/** Build a shared-vertex, deformed icosphere rock for economical game props. */
bool generateRockMesh(const Params &params, MeshBuild &out, std::string &error);

}  // namespace eve::procgen
