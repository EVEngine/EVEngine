#pragma once
#include "scene/editing/SceneTarget.h"

#include <optional>
#include <vector>
#include "common/SceneLocalRegion.h"

namespace eve::scene_editing {
/** @brief Supported authoring relations; these do not create transform parenting or runtime joints. */
enum class SceneRelationKind { SupportedOn, Nonpenetrating, VisibleFraction, Occludes, ProjectedOverlap };
/** @brief Owning relation between stable scene objects, measured against admitted contact proxies or native camera
 * raster evidence. */
struct SceneRelation {
    std::string                          id;
    SceneRelationKind                    kind = SceneRelationKind::SupportedOn;
    editing::ObjectId                    subject, reference;
    bool                                 hard         = true;
    int                                  priority     = 0;
    double                               weight       = 1.0;
    double                               tolerance    = 0.02;
    double                               normalMinDot = 0.8;
    double                               minimum = 0.0, maximum = 1.0;
    std::optional<eve::SceneLocalRegion> region;
};
/**
 * @brief Strictly decode physical and visual relations and validate all referenced objects.
 * @param value Array of relation objects; unknown types and fields are rejected.
 * @param hierarchy Borrowed only for this owner-thread call.
 * @return Independent owning relations or a diagnostic, without changing the target.
 * @cost Linear in relation count plus object lookup per restore or edit.
 */
[[nodiscard]] EVENGINE_API_BACKENDS editing::Result<std::vector<SceneRelation>> parseSceneRelations(
    const editing::Value& value, const ISceneHierarchyEditTarget& hierarchy);
}  // namespace eve::scene_editing
