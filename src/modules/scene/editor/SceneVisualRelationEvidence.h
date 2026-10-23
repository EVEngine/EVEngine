#pragma once
#include "scene/editor/SceneRelationEvidence.h"
namespace eve::scene_editor {
/**
 * @brief Merge native raster evidence into the physical report for selected visual relations.
 * @thread Owner thread; pure owning-value evaluation, no callbacks or mutation.
 * @cost Linear in relations and target evidence per explicit candidate check.
 * @return Combined report; ambiguous/unmeasurable hard constraints fail closed.
 */
[[nodiscard]] EVENGINE_API_EDITORS editing::Result<editing::Value> evaluateSceneVisualRelations(
    const std::vector<scene_editing::SceneRelation>& relations, const PhysicsPlacementFrame& frame,
    const editing::Value& evidence, editing::Value report);
}  // namespace eve::scene_editor
