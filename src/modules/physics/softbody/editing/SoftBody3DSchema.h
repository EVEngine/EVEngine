#pragma once

#include "editing/EditingProperty.h"

namespace eve::physics_editing {

/**
 * @brief Return the UI-independent creation schema for physics:softbody3d v1.
 * @return An owning schema whose defaults match SoftBody3DDefinition.
 */
[[nodiscard]] editing::PropertySchema softBody3DDefinitionSchema();

}  // namespace eve::physics_editing
