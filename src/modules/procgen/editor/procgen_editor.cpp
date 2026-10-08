#include "procgen/editor/ProcgenEditorModule.h"

#include "common/Capability.h"
#include "editing/EditingResult.h"
#include "editing/EditingCommandRegistry.h"
#include "editor/EditorAutomationTargetFactory.h"
#include "procgen/editing/ProcgenScriptTarget.h"
#include "procgen/editing/RoadNetworkEditTarget.h"
#include "procgen/editor/ProcgenScriptEditorScriptBindings.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <stdexcept>
#include <utility>

namespace eve::procgen_editor {

class ProcgenEditorModule::TargetFactory final : public editor::IEditorAutomationTargetFactory {
public:
    bool supports(std::string_view type) const override {
        return type == "procgen-script" || type == "road-network";
    }

    editor::EditorResult<editor::AutomationOwnedTarget> create(
        const editor::TargetId& target, std::string_view type, const editor::EditorValue::Object&) override {
        editor::AutomationOwnedTarget owned;
        if (type == "road-network")
            owned.target = procgen_editing::RoadNetworkEditTarget::createOwned(target.value());
        else
            owned.target = std::make_unique<procgen_editing::ProcgenScriptDocumentTarget>(target.value());
        return eve::editing::applied(std::move(owned));
    }
};

Module_IMPL(ProcgenEditorModule, new ProcgenEditorModule());

ProcgenEditorModule::ProcgenEditorModule() : factory_(std::make_unique<TargetFactory>()) {
    auto* registry = eve::cap::query<editing::IEditingCommandRegistry>();
    if (!registry || !procgen_editing::registerRoadNetworkEditingCommands(*registry).ok())
        throw std::runtime_error("Failed to register road network editing commands");
    eve::cap::addListener<editor::IEditorAutomationTargetFactory>(factory_.get());
}

ProcgenEditorModule::~ProcgenEditorModule() {
    eve::cap::removeListener<editor::IEditorAutomationTargetFactory>(factory_.get());
    if (auto* registry = eve::cap::query<editing::IEditingCommandRegistry>())
        registry->unregisterOwner("procgen_editing.road").ignore("procgen editor adapter shutdown");
}

void ProcgenEditorModule::expose(ssq::Table& table) {
    auto module = table.addClass(name, ProcgenEditorModule::create, false);
    exposeProcgenScriptEditorScriptBindings(table, module);
}
void ProcgenEditorModule::expose(ssq::Class&) {}

}  // namespace eve::procgen_editor
