#pragma once
#include <string>
#include "common/Result.h"
namespace ssq {
class Table;
}
namespace eve::graphics {
class GrassField;
namespace grass { struct GrassFoliageSettings; }
}
namespace eve::image {
class ImageData;
}
namespace eve::procgen {
class PointSet;
/** @brief Upload one asset group from generated points into a grass field using its built-in atlas.
 * Preserves roots and independent horizontal/vertical scale; 64-bit instance IDs remain in PointSet, folded to 24-bit
 * animation phases. Asset string selects points; it is not a texture loader. Normals/yaw are not used by upright
 * billboards.
 * @param field Exclusively borrowed field on its Graphics owner thread, outside rendering callbacks.
 * @param points Immutable points; only matching asset rows are validated/uploaded, no borrow survives.
 * @param asset Nonempty exact resource attribute selector.
 * @param width Positive finite billboard width.
 * @param height Positive finite billboard height.
 * @return Uploaded count or diagnostic; rejects differing X/Z scales, nonpositive scales or zero IDs. Failed validation
 * preserves field. GPU allocation exceptions preserve published field pointers; allocations remain Graphics-owned.
 */
[[nodiscard]] Result<int> bakeTerrainGrass(graphics::GrassField& field, const PointSet& points,
                                           const std::string& asset, float width, float height);
/** @brief Upload an asset group using one original static RGBA8 texture instead of the built-in atlas.
 * Same selection/thread/publication contract as bakeTerrainGrass; no image reference is retained.
 * RGB and alpha are uploaded unchanged with one frame. Invalid image storage preserves the field.
 * @return Uploaded point count or diagnostic; resource identity remains on the input PointSet. */
[[nodiscard]] Result<int> bakeTerrainGrassImage(graphics::GrassField& field, const PointSet& points,
                                                const std::string& asset, float width, float height,
                                                const image::ImageData& image);
/** @brief Upload a Pcg-style static foliage surface with albedo, normal and packed mask maps. */
[[nodiscard]] Result<int> bakeTerrainGrassFoliage(graphics::GrassField& field, const PointSet& points,
                                                  const std::string& asset, float width, float height,
                                                  const image::ImageData& albedo, const image::ImageData& normal,
                                                  const image::ImageData& mask,
                                                  const graphics::grass::GrassFoliageSettings& settings);
/** @brief Register full-host grass adapter; VM-thread only, no retained table reference. */
void exposeTerrainGrassAdapter(ssq::Table& table);
}  // namespace eve::procgen
