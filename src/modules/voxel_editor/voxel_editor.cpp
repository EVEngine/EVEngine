#include "voxel_editor/VoxelEditorModule.h"

#include "common/Capability.h"
#include "editing/EditingCommandRegistry.h"
#include "editor/EditorAutomationTargetFactory.h"
#include "voxel_editing/VoxelCatalog.h"
#include "voxel_editing/VoxelEditingCommands.h"
#include "voxel_editor/EditorVoxelPaletteTarget.h"
#include "voxel_editor/VoxelCatalogEditorScriptBindings.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <stdexcept>
#include <string>
#include <utility>

namespace eve::voxel_editor {
namespace {

const editor::EditorValue* field(const editor::EditorValue::Object& request, const char* key) {
    const auto found = request.find(key);
    return found == request.end() ? nullptr : &found->second;
}

}  // namespace

class VoxelEditorModule::TargetFactory final : public editor::IEditorAutomationTargetFactory {
public:
    bool supports(std::string_view type) const override {
        return type == "voxel-catalog" || type == "voxel-model";
    }

    editor::EditorResult<editor::AutomationOwnedTarget> create(
        const editor::TargetId& target, std::string_view, const editor::EditorValue::Object& request) override {
        auto catalog = std::make_unique<voxel_editing::VoxelCatalogTarget>(target.value());
        if (const editor::EditorValue* snapshot = field(request, "snapshot")) {
            auto loaded = catalog->loadSnapshot(*snapshot);
            if (!loaded.ok())
                return editor::EditorResult<editor::AutomationOwnedTarget>::failure(loaded.status());
        }
        editor::AutomationOwnedTarget owned;
        owned.target = std::move(catalog);
        return eve::editing::applied<editor::AutomationOwnedTarget>(std::move(owned));
    }
};

Module_IMPL(VoxelEditorModule, new VoxelEditorModule());

VoxelEditorModule::VoxelEditorModule() : factory_(std::make_unique<TargetFactory>()) {
    auto* registry = eve::cap::query<editing::IEditingCommandRegistry>();
    if (!registry || !voxel_editing::registerEditingCommands(*registry).ok())
        throw std::runtime_error("Failed to register voxel editing commands");
    eve::cap::addListener<editor::IEditorAutomationTargetFactory>(factory_.get());
}

VoxelEditorModule::~VoxelEditorModule() {
    eve::cap::removeListener<editor::IEditorAutomationTargetFactory>(factory_.get());
    if (auto* registry = eve::cap::query<editing::IEditingCommandRegistry>())
        registry->unregisterOwner("voxel_editing").ignore("voxel editor adapter shutdown");
}

void VoxelEditorModule::expose(ssq::Table& table) {
    auto module = table.addClass(name, VoxelEditorModule::create, false);
    exposeVoxelCatalogEditorScriptBindings(table, module);
}
void VoxelEditorModule::expose(ssq::Class&) {}

}  // namespace eve::voxel_editor
