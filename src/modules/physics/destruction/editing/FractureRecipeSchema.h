#pragma once
#include "common/Export.h"

#include "editing/EditingProperty.h"

namespace eve::physics_editing {

/**
 * @brief Return the UI-independent authoring schema for physics:fracture-recipe v1.
 * @return An owning schema whose defaults match FractureRecipe.
 */
[[nodiscard]] EVENGINE_API_DOMAINS editing::PropertySchema fractureRecipeSchema();

}  // namespace eve::physics_editing
