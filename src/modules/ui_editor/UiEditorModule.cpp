#include "ui_editor/UiEditorModule.h"
#include "ui_editor/UiThemeEditorScriptBindings.h"

#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::ui_editor {

Module_IMPL(UiEditorModule, new UiEditorModule());

UiEditorModule::UiEditorModule() = default;

void UiEditorModule::expose(ssq::Table& table) {
    auto module = table.addClass(name, UiEditorModule::create, false);
    exposeUiThemeEditorScriptBindings(table, module);
}
void UiEditorModule::expose(ssq::Class&) {}

}  // namespace eve::ui_editor
