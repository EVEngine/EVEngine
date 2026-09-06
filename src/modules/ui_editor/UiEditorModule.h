#pragma once

#include "common/Module.h"

namespace eve::ui_editor {

/** @brief Composition adapter that exposes the script-owned UiThemeEditor factory. */
class UiEditorModule final : public Module {
public:
    Module_REG(UiEditorModule);
    UiEditorModule();
    ~UiEditorModule() override = default;
};

}  // namespace eve::ui_editor
