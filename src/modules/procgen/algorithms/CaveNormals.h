#pragma once

#include <string>
#include <vector>

namespace eve::procgen {

class MeshBuild;

/** @brief CaveNormalStatus public API. */
enum class CaveNormalStatus { applied, unknownMode };

/** @brief Applies cave surface normals. */
CaveNormalStatus applyCaveSurfaceNormals(MeshBuild& mesh, const std::vector<float>& density, int nx, int ny, int nz,
                                         float width, float height, float depth, const std::string& mode, float blend,
                                         std::string& error);

}  // namespace eve::procgen
