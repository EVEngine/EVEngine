#include "archspace/editor/ArchSpaceEditorModule.h"

#include "archspace/editing/ArchSpaceEditingCommands.h"
#include "archspace/editing/ArchSpaceTarget.h"
#include "common/Capability.h"
#include "editing/EditingCommandRegistry.h"
#include "editor/EditorAutomationTargetFactory.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace eve::archspace_editor {
namespace {

std::string stringField(const editor::EditorValue::Object& request, const char* key) {
    const auto found = request.find(key);
    if (found == request.end()) return {};
    const auto* value = found->second.getIf<std::string>();
    return value ? *value : std::string{};
}

}  // namespace

class ArchSpaceEditorModule::TargetFactory final : public editor::IEditorAutomationTargetFactory {
public:
    std::vector<std::string_view> types() const override { return {"archspace", "archspace-document"}; }

    editor::EditorResult<editor::AutomationOwnedTarget> create(const editor::TargetId& target, std::string_view type,
                                                               const editor::EditorValue::Object& request) override {
        (void)type;
        auto              document = std::make_unique<archspace_editing::ArchSpaceDocumentTarget>(target.value());
        const std::string site     = stringField(request, "siteId");
        if (!site.empty()) {
            const std::string building = stringField(request, "buildingId");
            const std::string level    = stringField(request, "levelId");
            auto operation             = document->makeBootstrap(site, building.empty() ? site + ".building" : building,
                                                     level.empty() ? site + ".level0" : level);
            if (!operation.ok())
                return editor::EditorResult<editor::AutomationOwnedTarget>::failure(operation.status());
            auto applied = document->applyDomainOperation(operation.value());
            if (!applied.ok()) return editor::EditorResult<editor::AutomationOwnedTarget>::failure(applied.status());
        }
        editor::AutomationOwnedTarget owned;
        owned.target = std::move(document);
        return eve::editing::applied<editor::AutomationOwnedTarget>(std::move(owned));
    }
};

Module_IMPL(ArchSpaceEditorModule, new ArchSpaceEditorModule());

ArchSpaceEditorModule::ArchSpaceEditorModule() : factory_(std::make_unique<TargetFactory>()) {
    auto* registry = eve::cap::query<editing::IEditingCommandRegistry>();
    if (!registry || !archspace_editing::registerEditingCommands(*registry).ok())
        throw std::runtime_error("Failed to register ArchSpace editing commands");
    eve::cap::addListener<editor::IEditorAutomationTargetFactory>(factory_.get());
}

ArchSpaceEditorModule::~ArchSpaceEditorModule() {
    eve::cap::removeListener<editor::IEditorAutomationTargetFactory>(factory_.get());
    if (auto* registry = eve::cap::query<editing::IEditingCommandRegistry>())
        registry->unregisterOwner("archspace_editing").ignore("archspace editor adapter shutdown");
}

void ArchSpaceEditorModule::expose(ssq::Table& table) { table.addClass(name, ArchSpaceEditorModule::create, false); }

void ArchSpaceEditorModule::expose(ssq::Class&) {}

}  // namespace eve::archspace_editor
