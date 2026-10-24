#pragma once
#include <box3d/box3d.h>
#include <vector>
namespace eve::physics::box3d {
/** @brief Validates and cooks native geometry; throws on invalid input.
 * @ownership Transfers native allocation to caller; release with b3DestroyHull. Simulation thread only.
 * @cost Geometry-sized allocation and preprocessing per topology edit; retain cooked geometry across frames.
 */
[[nodiscard]] b3HullData* createCheckedHull(const std::vector<float>& vertices, int maxVertices, const char* operation);
/** @brief Validates and cooks native geometry; throws on invalid input.
 * @ownership Transfers native allocation to caller; release with b3DestroyMesh. Simulation thread only.
 * @cost Geometry-sized allocation and preprocessing per topology edit; retain cooked geometry across frames.
 */
[[nodiscard]] b3MeshData* createCheckedMesh(const std::vector<float>& vertices, const std::vector<int32_t>& indices,
                                            bool weldVertices, float weldTolerance, bool identifyEdges,
                                            bool useMedianSplit, b3Transform transform,
                                            const std::vector<uint8_t>* materialIndices, const char* operation);
/** @brief Validates and cooks native geometry; throws on invalid input.
 * @ownership Transfers native allocation to caller; release with b3DestroyHeightField. Simulation thread only.
 * @cost Geometry-sized allocation and preprocessing per topology edit; retain cooked geometry across frames.
 */
[[nodiscard]] b3HeightFieldData* createCheckedHeightField(int countX, int countZ, float cellSizeX, float cellSizeZ,
                                                          const std::vector<float>& heights, float globalMin,
                                                          float globalMax, bool clockwiseWinding,
                                                          const char* operation);
}  // namespace eve::physics::box3d
