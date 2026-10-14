#pragma once
#include "common/Export.h"
#include "scene/editing/SceneEditingTypes.h"

#include <array>

namespace eve::editing {
class IEditingCommandRegistry;
}
namespace eve::scene_editing {
class ISceneHierarchyEditTarget;

/** @brief Immutable asset-local bounds, ordered minimum XYZ then maximum XYZ. */
using AuthoringBounds = std::array<double, 6>;

/**
 * @brief Inspect world geometry derived from the canonical hierarchy and asset bounds.
 * @param target Borrowed hierarchy, retained only for this synchronous call.
 * @param object Stable host-local object identity.
 * @param bounds Immutable asset-local AABB; finite, ordered coordinates are required.
 * @return Owning world matrix, corners, AABB and bottom-center anchor, or a diagnostic.
 * @thread Owner thread only. No mutation, callbacks or retained references.
 * @cost Eight corner transforms plus ancestor traversal per object; use per edit/query.
 */
[[nodiscard]] EVENGINE_API_BACKENDS Result<EditorValue> inspectAuthoringObject(const ISceneHierarchyEditTarget& target,
                                                                               const ObjectId&                  object,
                                                                               const AuthoringBounds&           bounds);

/**
 * @brief Register ground, surface-edge alignment and screen-anchor placement planners.
 * @param registry Borrowed registry owned by the editing host.
 * @return Registration status; planners produce reversible operations without mutation.
 * @thread Owner thread only. Registry must outlive registered planners.
 */
[[nodiscard]] EVENGINE_API_BACKENDS Result<void> registerAuthoringCommands(editing::IEditingCommandRegistry& registry);
}  // namespace eve::scene_editing
