#include "biome_editor/BiomeEditorModule.h"

#include "biome_editing/BiomeEditingCommands.h"
#include "biome_editing/BiomeTarget.h"
#include "biome_editor/BiomeRulesEditorScriptBindings.h"
#include "common/Capability.h"
#include "editing/EditingCommandRegistry.h"
#include "editor/EditorAutomationTargetFactory.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <stdexcept>
#include <string>

namespace eve::biome_editor {
namespace {

std::string stringField(const editor::EditorValue::Object& request, const char* key) {
    const auto found = request.find(key);
    if (found == request.end()) return {};
    const auto* value = found->second.getIf<std::string>();
    return value ? *value : std::string{};
}

}  // namespace

class BiomeEditorModule::TargetFactory final : public editor::IEditorAutomationTargetFactory {
public:
    bool supports(std::string_view type) const override { return type == "biome" || type == "biome-rules"; }

    editor::EditorResult<editor::AutomationOwnedTarget> create(
        const editor::TargetId& target, std::string_view type,
        const editor::EditorValue::Object& request) override {
        (void)type;
        auto biome = std::make_unique<biome_editing::BiomeDocumentTarget>(target.value());
        const std::string layer = stringField(request, "layer");
        if (!layer.empty()) {
            biome_editing::BiomeLayerValue created;
            created.id           = editor::ObjectId(layer);
            created.name         = stringField(request, "name");
            if (created.name.empty()) created.name = layer;
            created.spatialAsset = stringField(request, "spatial");
            auto operation = biome->makeCreateLayer(created);
            if (!operation.ok())
                return editor::EditorResult<editor::AutomationOwnedTarget>::failure(operation.status());
            auto applied = biome->applyDomainOperation(operation.value());
            if (!applied.ok())
                return editor::EditorResult<editor::AutomationOwnedTarget>::failure(applied.status());
        }
        editor::AutomationOwnedTarget owned;
        owned.target = std::move(biome);
        return eve::editing::applied<editor::AutomationOwnedTarget>(std::move(owned));
    }
};

Module_IMPL(BiomeEditorModule, new BiomeEditorModule());

BiomeEditorModule::BiomeEditorModule() : factory_(std::make_unique<TargetFactory>()) {
    auto* registry = eve::cap::query<editing::IEditingCommandRegistry>();
    if (!registry || !biome_editing::registerEditingCommands(*registry).ok())
        throw std::runtime_error("Failed to register biome editing commands");
    eve::cap::addListener<editor::IEditorAutomationTargetFactory>(factory_.get());
}

BiomeEditorModule::~BiomeEditorModule() {
    eve::cap::removeListener<editor::IEditorAutomationTargetFactory>(factory_.get());
    if (auto* registry = eve::cap::query<editing::IEditingCommandRegistry>())
        registry->unregisterOwner("biome_editing").ignore("biome editor adapter shutdown");
}

void BiomeEditorModule::expose(ssq::Table& table) {
    auto module = table.addClass(name, BiomeEditorModule::create, false);
    exposeBiomeRulesEditorScriptBindings(table, module);
}
void BiomeEditorModule::expose(ssq::Class&) {}

}  // namespace eve::biome_editor
