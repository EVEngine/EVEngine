#pragma once
#include "scene/editing/SceneRelations.h"
#include "scene/editor/ScenePhysicsPlacement.h"

namespace eve::scene_editor {
/**
 * @brief Measure supported_on/nonpenetrating against owning private-world contact evidence.
 * @param relations Validated relations; all participants must be admitted for measurement.
 * @param frame Evidence captured after a fixed physics tick, borrowed only for this call.
 * @return Owning report; hardSatisfied is false for violations and unmeasurable hard relations.
 * @thread Owner thread; pure value calculation, no callbacks or world mutation.
 * @cost Relations times contact count per explicit check; no per-frame runtime work.
 */
[[nodiscard]] EVENGINE_API_EDITORS editing::Result<editing::Value> evaluateSceneRelations(
    const std::vector<scene_editing::SceneRelation>& relations, const PhysicsPlacementFrame& frame);
}  // namespace eve::scene_editor
