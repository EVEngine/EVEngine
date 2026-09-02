#include "action_editor/ActionEditorModule.h"

#include "action_editor/ActionTimelineScriptBindings.h"

#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::action_editor {

Module_IMPL(ActionEditorModule, new ActionEditorModule());

void ActionEditorModule::expose(ssq::Table& table) {
    auto module = table.addClass(name, ActionEditorModule::create, false);
    eve::editor::exposeActionTimelineScriptBindings(table, module);
}

void ActionEditorModule::expose(ssq::Class&) {}

}  // namespace eve::action_editor
