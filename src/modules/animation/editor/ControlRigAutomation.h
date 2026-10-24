#pragma once
#include "common/Result.h"
namespace eve::editing {
class IEditingCommandRegistry;
}
namespace eve::animation_editor {
/** @brief Register schema-described Rig planners with the existing editor command registry. */
[[nodiscard]] Result<void> registerControlRigAutomation(editing::IEditingCommandRegistry& registry);
}  // namespace eve::animation_editor
