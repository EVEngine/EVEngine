#pragma once

#include "common/Result.h"
#include "procgen/MeshBuild.h"

#include <cstdint>
#include <string_view>

namespace eve::procgen {

/** @brief UV coordinate resolved from a dynamic-mesh triangle hit. */
struct MeshSurfaceUv { float u = 0.f, v = 0.f; };

/**
 * @brief Generate topology-preserving UVs for a procedural or deformed mesh.
 * @param input Source mesh, borrowed for this synchronous call.
 * @param mode planar, box, or spherical.
 * @param scale Positive projection scale. @param offsetU U offset. @param offsetV V offset.
 * @return Owning mesh copy with replaced UV stream; source is never mutated.
 */
[[nodiscard]] Result<MeshBuild> projectMeshUvResult(const MeshBuild& input, std::string_view mode, float scale = 1.f,
                                                    float offsetU = 0.f, float offsetV = 0.f);

/**
 * @brief Map a surface point on a dynamic-mesh triangle to its interpolated UV.
 * @param mesh Current owning mesh snapshot, borrowed for this synchronous call.
 * @param triangleIndex Triangle ordinal, not index-buffer offset.
 * @param x Point X. @param y Point Y. @param z Point Z.
 * @return Interpolated UV, including values outside [0,1] when the projection tiles.
 */
[[nodiscard]] Result<MeshSurfaceUv> mapMeshSurfacePointToUvResult(const MeshBuild& mesh, int triangleIndex, float x,
                                                                  float y, float z);

}  // namespace eve::procgen
