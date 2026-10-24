#include "voxel/editor/VoxelEditorPackage.h"

#include "editing/PropertySchemaJson.h"
#include "voxel/editing/VoxelEditingCommands.h"

namespace eve::voxel_editor {

editing::EditorPackage makeVoxelEditorPackage() {
    editing::EditorPackage package;
    package.ownerModule     = "voxel_editing";
    package.selectionDomain = "voxel.model";
    package.previewKind     = "voxel.sculpt.preview";
    package.undoPolicy      = editing::EditorUndoPolicy::Private;
    package.incomplete      = false;
    package.panels          = {
        {"voxel.models", "Models", "left", "voxel.sculpt", "list", 100, false},
        // primaryViewport stays false so Action (or Scene) can own the host primary slot.
        {"voxel.viewport", "Sculpt", "center", "voxel.sculpt", "preview", 100, false},
        {"voxel.tools", "Tools", "right", "voxel.sculpt", "inspector", 100, false},
        {"voxel.inspector", "Model", "bottom", "voxel.sculpt", "inspector", 110, false},
    };
    package.commands = voxel_editing::voxelEditingCommandDescriptors();
    {
        editing::EditorTargetTypeSpec catalog;
        catalog.type         = "voxel-catalog";
        catalog.createSchema = editing::emptyObjectPropertySchema("voxel-catalog.create");
        {
            editing::PropertyDescriptor snapshot;
            snapshot.path           = editing::PropertyPath("snapshot");
            snapshot.displayNameKey = "Catalog snapshot";
            snapshot.type           = editing::PropertyType::Struct;
            snapshot.flags          = editing::PropertyFlag::Advanced;
            catalog.createSchema.properties.push_back(std::move(snapshot));
        }
        package.targetTypes.push_back(std::move(catalog));
    }
    {
        editing::EditorTargetTypeSpec model;
        model.type         = "voxel-model";
        model.createSchema = editing::emptyObjectPropertySchema("voxel-model.create");
        package.targetTypes.push_back(std::move(model));
    }
    return package;
}

VoxelEditorPackageProvider::VoxelEditorPackageProvider() : package_(makeVoxelEditorPackage()) {}

std::string_view VoxelEditorPackageProvider::ownerModule() const { return package_.ownerModule; }

const editing::EditorPackage& VoxelEditorPackageProvider::package() const { return package_; }

}  // namespace eve::voxel_editor
