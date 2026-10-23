#include "fluids/editor/FluidsEditorPackage.h"

#include "editing/PropertySchemaJson.h"

namespace eve::fluids_editor {

editing::EditorPackage makeFluidsEditorPackage() {
    editing::EditorPackage package;
    package.ownerModule = "fluids_editor";
    package.incomplete  = true;
    package.undoPolicy  = editing::EditorUndoPolicy::Private;
    for (const char* type : {"fluid-simulation", "surface-fluid", "volume-fluid"}) {
        editing::EditorTargetTypeSpec target;
        target.type         = type;
        target.createSchema = editing::emptyObjectPropertySchema(std::string(type) + ".create");
        package.targetTypes.push_back(std::move(target));
    }
    return package;
}

FluidsEditorPackageProvider::FluidsEditorPackageProvider() : package_(makeFluidsEditorPackage()) {}

std::string_view FluidsEditorPackageProvider::ownerModule() const { return package_.ownerModule; }

const editing::EditorPackage& FluidsEditorPackageProvider::package() const { return package_; }

}  // namespace eve::fluids_editor
