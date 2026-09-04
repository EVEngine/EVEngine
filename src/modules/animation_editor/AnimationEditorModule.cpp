#include "animation_editor/AnimationEditorModule.h"

#include "animation_editing/AnimationClip.h"
#include "animation_editing/AnimationEditingCommands.h"
#include "animation_editor/AnimationClipEditorScriptBindings.h"
#include "common/Capability.h"
#include "editing/EditingCommandRegistry.h"
#include "editor/EditorAutomationTargetFactory.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <stdexcept>
#include <utility>

namespace eve::animation_editor {
namespace {

const editor::EditorValue* field(const editor::EditorValue::Object& request, const char* key) {
    const auto found = request.find(key);
    return found == request.end() ? nullptr : &found->second;
}

}  // namespace

class AnimationEditorModule::TargetFactory final : public editor::IEditorAutomationTargetFactory {
public:
    bool supports(std::string_view type) const override {
        return type == "animation-clip" || type == "animation-clip-document";
    }

    editor::EditorResult<editor::AutomationOwnedTarget> create(
        const editor::TargetId& target, std::string_view, const editor::EditorValue::Object& request) override {
        auto clip = std::make_unique<animation_editing::AnimationClipDocumentTarget>(target.value());
        if (const editor::EditorValue* snapshot = field(request, "snapshot")) {
            auto loaded = clip->loadSnapshot(*snapshot);
            if (!loaded.ok())
                return editor::EditorResult<editor::AutomationOwnedTarget>::failure(loaded.status());
        }
        editor::AutomationOwnedTarget owned;
        owned.target = std::move(clip);
        return eve::editing::applied<editor::AutomationOwnedTarget>(std::move(owned));
    }
};

Module_IMPL(AnimationEditorModule, new AnimationEditorModule());

AnimationEditorModule::AnimationEditorModule() : factory_(std::make_unique<TargetFactory>()) {
    auto* registry = eve::cap::query<editing::IEditingCommandRegistry>();
    if (!registry || !animation_editing::registerEditingCommands(*registry).ok())
        throw std::runtime_error("Failed to register animation editing commands");
    eve::cap::addListener<editor::IEditorAutomationTargetFactory>(factory_.get());
}

AnimationEditorModule::~AnimationEditorModule() {
    eve::cap::removeListener<editor::IEditorAutomationTargetFactory>(factory_.get());
    if (auto* registry = eve::cap::query<editing::IEditingCommandRegistry>())
        registry->unregisterOwner("animation_editing").ignore("animation editor adapter shutdown");
}

void AnimationEditorModule::expose(ssq::Table& table) {
    auto module = table.addClass(name, AnimationEditorModule::create, false);
    exposeAnimationClipEditorScriptBindings(table, module);
}
void AnimationEditorModule::expose(ssq::Class&) {}

}  // namespace eve::animation_editor
