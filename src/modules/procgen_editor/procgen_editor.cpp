#include "procgen_editor/ProcgenEditorModule.h"

#include "common/Capability.h"
#include "editing/EditingResult.h"
#include "editor/EditorAutomationTargetFactory.h"
#include "procgen_editing/ProcgenScriptTarget.h"
#include "procgen_editor/ProcgenScriptEditorScriptBindings.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <utility>

namespace eve::procgen_editor {

class ProcgenEditorModule::TargetFactory final : public editor::IEditorAutomationTargetFactory {
public:
    bool supports(std::string_view type) const override { return type == "procgen-script"; }

    editor::EditorResult<editor::AutomationOwnedTarget> create(
        const editor::TargetId& target, std::string_view, const editor::EditorValue::Object&) override {
        editor::AutomationOwnedTarget owned;
        owned.target = std::make_unique<procgen_editing::ProcgenScriptDocumentTarget>(target.value());
        return eve::editing::applied(std::move(owned));
    }
};

Module_IMPL(ProcgenEditorModule, new ProcgenEditorModule());

ProcgenEditorModule::ProcgenEditorModule() : factory_(std::make_unique<TargetFactory>()) {
    eve::cap::addListener<editor::IEditorAutomationTargetFactory>(factory_.get());
}

ProcgenEditorModule::~ProcgenEditorModule() {
    eve::cap::removeListener<editor::IEditorAutomationTargetFactory>(factory_.get());
}

void ProcgenEditorModule::expose(ssq::Table& table) {
    auto module = table.addClass(name, ProcgenEditorModule::create, false);
    exposeProcgenScriptEditorScriptBindings(table, module);
}
void ProcgenEditorModule::expose(ssq::Class&) {}

}  // namespace eve::procgen_editor
