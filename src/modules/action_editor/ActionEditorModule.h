#pragma once

#include "common/Module.h"

namespace eve::action_editor {

/** @brief Script composition module for action-timeline authoring. */
class ActionEditorModule final : public Module {
public:
    Module_REG(ActionEditorModule);
};

}  // namespace eve::action_editor
