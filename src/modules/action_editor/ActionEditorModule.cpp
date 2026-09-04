#include "action_editor/ActionEditorModule.h"

#include "action_editor/ActionEditingCommands.h"
#include "action_editor/ActionTimelineEditor.h"
#include "action_editor/ActionTimelineScriptBindings.h"
#include "common/Capability.h"
#include "editing/EditingCommandRegistry.h"
#include "editor/EditorAutomationTargetFactory.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <stdexcept>
#include <string>
#include <utility>

namespace eve::action_editor {
namespace {

std::string stringField(const editor::EditorValue::Object& request, const char* key) {
    const auto found = request.find(key);
    if (found == request.end()) return {};
    const auto* value = found->second.getIf<std::string>();
    return value ? *value : std::string{};
}

}  // namespace

class ActionEditorModule::TargetFactory final : public editor::IEditorAutomationTargetFactory {
public:
    bool supports(std::string_view type) const override { return type == "action" || type == "action-timeline"; }

    editor::EditorResult<editor::AutomationOwnedTarget> create(
        const editor::TargetId& target, std::string_view type, const editor::EditorValue::Object& request) override {
        (void)type;
        auto document = std::make_unique<editor::ActionTimelineTarget>(target.value());
        const auto found = request.find("timeline");
        if (found != request.end()) {
            auto loaded = document->loadSnapshot(found->second);
            if (!loaded.ok())
                return editor::EditorResult<editor::AutomationOwnedTarget>::failure(loaded.status());
        }
        const std::string animationUri = stringField(request, "animationUri");
        if (!animationUri.empty()) {
            editor::SelectionSnapshot selection;
            selection.channel = "asset";
            editor::SelectionItem item;
            item.domain = editor::SelectionDomain::Asset;
            item.target = editor::TargetId(document->targetId());
            item.item   = editor::StableId(document->targetId().value());
            item.type   = "action.timeline";
            selection.items.push_back(item);
            selection.primary = item;
            auto operation    = document->makeSet(selection, editor::PropertyPath("timeline.animationUri"),
                                                  animationUri, editor::PropertySetMode::Absolute);
            if (!operation.ok())
                return editor::EditorResult<editor::AutomationOwnedTarget>::failure(operation.status());
            auto applied = document->applyDomainOperation(operation.value());
            if (!applied.ok())
                return editor::EditorResult<editor::AutomationOwnedTarget>::failure(applied.status());
        }
        editor::AutomationOwnedTarget owned;
        owned.target = std::move(document);
        return eve::editing::applied<editor::AutomationOwnedTarget>(std::move(owned));
    }
};

Module_IMPL(ActionEditorModule, new ActionEditorModule());

ActionEditorModule::ActionEditorModule() : factory_(std::make_unique<TargetFactory>()) {
    auto* registry = eve::cap::query<editing::IEditingCommandRegistry>();
    if (!registry || !registerEditingCommands(*registry).ok())
        throw std::runtime_error("Failed to register action editing commands");
    eve::cap::addListener<editor::IEditorAutomationTargetFactory>(factory_.get());
}

ActionEditorModule::~ActionEditorModule() {
    eve::cap::removeListener<editor::IEditorAutomationTargetFactory>(factory_.get());
    if (auto* registry = eve::cap::query<editing::IEditingCommandRegistry>())
        registry->unregisterOwner("action_editor").ignore("action editor adapter shutdown");
}

void ActionEditorModule::expose(ssq::Table& table) {
    auto module = table.addClass(name, ActionEditorModule::create, false);
    eve::editor::exposeActionTimelineScriptBindings(table, module);
}

void ActionEditorModule::expose(ssq::Class&) {}

}  // namespace eve::action_editor
