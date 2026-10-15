#include "gpuagents/editor/GpuAgentsEditorPackage.h"

#include "editing/PropertySchemaJson.h"

namespace eve::gpuagents_editor {

editing::EditorPackage makeGpuAgentsEditorPackage() {
    editing::EditorPackage package;
    package.ownerModule = "gpuagents_editor";
    package.incomplete  = true;
    package.undoPolicy  = editing::EditorUndoPolicy::Private;
    editing::EditorTargetTypeSpec target;
    target.type         = "gpuagents-effect";
    target.createSchema = editing::emptyObjectPropertySchema("gpuagents-effect.create");
    package.targetTypes.push_back(std::move(target));
    return package;
}

GpuAgentsEditorPackageProvider::GpuAgentsEditorPackageProvider() : package_(makeGpuAgentsEditorPackage()) {}

std::string_view GpuAgentsEditorPackageProvider::ownerModule() const { return package_.ownerModule; }

const editing::EditorPackage& GpuAgentsEditorPackageProvider::package() const { return package_; }

}  // namespace eve::gpuagents_editor
