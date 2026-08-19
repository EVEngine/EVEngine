#pragma once

#include "procgen/MeshBuild.h"
#include "procgen/Params.h"

#include <string>

namespace eve::procgen {

/** Build a deterministic procedural small bush. Registered as the `mesh.bush` recipe. */
bool generateBushMesh(const Params &params, MeshBuild &out, std::string &error);

}  // namespace eve::procgen
