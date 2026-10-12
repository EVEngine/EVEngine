#pragma once
#include "scene/editing/SceneRelations.h"
namespace eve::scene_editing {
/**
 * @brief Derive a stable geometry recipe revision from the authoritative document source.
 * @thread Owner thread, no callbacks; snapshot borrowed only during call.
 * @cost Source recipe serialization per explicit inspection; excludes transforms/materials.
 * @return Owning recipe identity or missing-source diagnostic; no native upload id persisted.
 */
[[nodiscard]] EVENGINE_API_BACKENDS editing::Result<std::string> sceneGeometryRevision(const editing::Value& snapshot,
                                                                                       const editing::ObjectId& object);
/**
 * @brief Build bounded raster targets and reject regions whose source geometry changed.
 * @thread Owner thread; pure value planning without projection or scene mutation.
 * @cost Linear in visual relations plus recipe serialization per explicit preview preparation.
 * @return Owning target array; repeated object/region definitions must be identical.
 */
[[nodiscard]] EVENGINE_API_BACKENDS editing::Result<editing::Value::Array> sceneVisibilityTargets(
    const std::vector<SceneRelation>& relations, const editing::Value& snapshot);
}  // namespace eve::scene_editing
