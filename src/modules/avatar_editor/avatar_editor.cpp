#include "avatar_editor/AvatarEditorModule.h"
#include "avatar_editor/EditorAvatarTarget.h"

#include "avatar_editing/AvatarEditingCommands.h"
#include "avatar_editing/AvatarTarget.h"
#include "avatar_editor/AvatarDocumentEditorScriptBindings.h"
#include "common/Capability.h"
#include "editing/EditingCommandRegistry.h"
#include "editor/EditorAutomationTargetFactory.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <stdexcept>
#include <string>
#include <utility>

namespace eve::avatar_editor {
namespace {

std::string stringField(const editor::EditorValue::Object& request, const char* key) {
    const auto found = request.find(key);
    if (found == request.end()) return {};
    const auto* value = found->second.getIf<std::string>();
    return value ? *value : std::string{};
}

editor::EditorResult<void> apply(avatar_editing::AvatarDocumentTarget& target,
                                 editor::EditorResult<editor::DomainOperation> operation) {
    if (!operation.ok())
        return editor::EditorResult<void>::failure(operation.status());
    return target.applyDomainOperation(operation.value());
}

}  // namespace

class AvatarEditorModule::TargetFactory final : public editor::IEditorAutomationTargetFactory {
public:
    bool supports(std::string_view type) const override { return type == "avatar"; }

    editor::EditorResult<editor::AutomationOwnedTarget> create(
        const editor::TargetId& target, std::string_view type, const editor::EditorValue::Object& request) override {
        (void)type;
        auto document = std::make_unique<avatar_editing::AvatarDocumentTarget>(target.value());
        const std::string kind = stringField(request, "kind");
        const std::string source = stringField(request, "source");
        if (!kind.empty() || !source.empty()) {
            auto applied = apply(*document, document->makeSetSource(kind.empty() ? document->kind() : kind, source));
            if (!applied.ok())
                return editor::EditorResult<editor::AutomationOwnedTarget>::failure(applied.status());
        }
        const std::string layer = stringField(request, "layer");
        if (!layer.empty()) {
            avatar_editing::AvatarLayerValue value;
            value.id           = editor::ObjectId(layer);
            value.name         = layer;
            const std::string texture = stringField(request, "texture");
            value.textureAsset = texture.empty() ? layer + ".png" : texture;
            auto applied = apply(*document, document->makeCreateLayer(value));
            if (!applied.ok())
                return editor::EditorResult<editor::AutomationOwnedTarget>::failure(applied.status());
        }
        const std::string parameter = stringField(request, "parameter");
        if (!parameter.empty()) {
            avatar_editing::AvatarParameterValue value;
            value.id   = editor::ObjectId(parameter);
            value.name = parameter;
            auto applied = apply(*document, document->makeCreateParameter(value));
            if (!applied.ok())
                return editor::EditorResult<editor::AutomationOwnedTarget>::failure(applied.status());
        }
        editor::AutomationOwnedTarget owned;
        owned.target = std::move(document);
        return eve::editing::applied<editor::AutomationOwnedTarget>(std::move(owned));
    }
};

Module_IMPL(AvatarEditorModule, new AvatarEditorModule());

AvatarEditorModule::AvatarEditorModule() : factory_(std::make_unique<TargetFactory>()) {
    auto* registry = eve::cap::query<editing::IEditingCommandRegistry>();
    if (!registry || !avatar_editing::registerEditingCommands(*registry).ok())
        throw std::runtime_error("Failed to register avatar editing commands");
    eve::cap::addListener<editor::IEditorAutomationTargetFactory>(factory_.get());
}

AvatarEditorModule::~AvatarEditorModule() {
    eve::cap::removeListener<editor::IEditorAutomationTargetFactory>(factory_.get());
    if (auto* registry = eve::cap::query<editing::IEditingCommandRegistry>())
        registry->unregisterOwner("avatar_editing").ignore("avatar editor adapter shutdown");
}

void AvatarEditorModule::expose(ssq::Table& table) {
    auto module = table.addClass(name, AvatarEditorModule::create, false);
    exposeAvatarDocumentEditorScriptBindings(table, module);
}
void AvatarEditorModule::expose(ssq::Class&) {}

}  // namespace eve::avatar_editor
