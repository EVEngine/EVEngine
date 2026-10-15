#include "action/editor/ActionEditorPackage.h"

#include "action/editor/ActionEditingCommands.h"

namespace eve::action_editor {
namespace {

editing::PropertySchema actionTargetCreateSchema() {
    editing::PropertySchema schema;
    schema.typeId  = "action-timeline.create";
    schema.version = 1;
    {
        editing::PropertyDescriptor timeline;
        timeline.path           = editing::PropertyPath("timeline");
        timeline.displayNameKey = "Timeline snapshot";
        timeline.type           = editing::PropertyType::Struct;
        timeline.flags          = editing::PropertyFlag::Advanced;
        schema.properties.push_back(std::move(timeline));
    }
    {
        editing::PropertyDescriptor animationUri;
        animationUri.path           = editing::PropertyPath("animationUri");
        animationUri.displayNameKey = "Animation URI";
        animationUri.type           = editing::PropertyType::AssetRef;
        schema.properties.push_back(std::move(animationUri));
    }
    return schema;
}

}  // namespace

editing::EditorPackage makeActionEditorPackage() {
    editing::EditorPackage package;
    package.ownerModule     = "action_editor";
    package.selectionDomain = "action.timeline";
    package.previewKind     = "action.timeline.preview";
    package.undoPolicy      = editing::EditorUndoPolicy::Private;
    package.incomplete      = false;
    package.panels          = {
        {"action.assets", "Actions", "left", "action.timeline", "list", 100, false},
        {"action.preview", "Action Preview", "center", "action.timeline", "preview", 100, true},
        {"action.inspector", "Action Inspector", "right", "action.timeline", "inspector", 100, false},
        {"action.timeline", "Action Timeline", "bottom", "action.timeline", "timeline", 100, false},
    };
    package.commands = {actionPropertySetCommandDescriptor()};
    {
        editing::EditorTargetTypeSpec action;
        action.type         = "action";
        action.createSchema = actionTargetCreateSchema();
        package.targetTypes.push_back(std::move(action));
    }
    {
        editing::EditorTargetTypeSpec timeline;
        timeline.type         = "action-timeline";
        timeline.createSchema = actionTargetCreateSchema();
        package.targetTypes.push_back(std::move(timeline));
    }
    return package;
}

ActionEditorPackageProvider::ActionEditorPackageProvider() : package_(makeActionEditorPackage()) {}

std::string_view ActionEditorPackageProvider::ownerModule() const { return package_.ownerModule; }

const editing::EditorPackage& ActionEditorPackageProvider::package() const { return package_; }

}  // namespace eve::action_editor
