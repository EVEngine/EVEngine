#pragma once

#include "procgen/MeshBuild.h"
#include "procgen/Params.h"

#include <string>

namespace eve::procgen {

/**
 * @brief Build a small accent flower (stem + radial petals + centre).
 * Registered as the `mesh.flower` recipe for grassland / bush scatter.
 */
bool generateFlowerMesh(const Params &params, MeshBuild &out, std::string &error);

}  // namespace eve::procgen
