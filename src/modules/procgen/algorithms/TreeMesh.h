#pragma once

#include "procgen/MeshBuild.h"
#include "procgen/Params.h"

#include <string>

namespace eve::procgen {

/** @brief Build a deterministic procedural tree. Registered as the `mesh.tree` recipe. */
bool generateTreeMesh(const Params& params, MeshBuild& out, std::string& error);

}  // namespace eve::procgen
