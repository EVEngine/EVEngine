#include "audio_editor/AudioEditorModule.h"

#include "audio_editing/AudioEditingCommands.h"
#include "audio_editor/AudioSourceEditorScriptBindings.h"
#include "audio_editing/AudioEffects.h"
#include "audio_editing/AudioTarget.h"
#include "common/Capability.h"
#include "editing/EditingCommandRegistry.h"
#include "editor/EditorAutomationTargetFactory.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <stdexcept>

namespace eve::audio_editor {

class AudioEditorModule::TargetFactory final : public editor::IEditorAutomationTargetFactory {
public:
    bool supports(std::string_view type) const override {
        return type == "audio-source" || type == "audio-mixer" || type == "audio-effects";
    }

    editor::EditorResult<editor::AutomationOwnedTarget> create(
        const editor::TargetId& target, std::string_view type,
        const editor::EditorValue::Object&) override {
        editor::AutomationOwnedTarget owned;
        if (type == "audio-source") {
            owned.target = std::make_unique<audio_editing::AudioSourceTarget>(target.value());
            return eve::editing::applied<editor::AutomationOwnedTarget>(std::move(owned));
        }
        if (type == "audio-mixer") {
            owned.target = std::make_unique<audio_editing::AudioMixerTarget>(target.value());
            return eve::editing::applied<editor::AutomationOwnedTarget>(std::move(owned));
        }
        owned.target = std::make_unique<audio_editing::AudioEffectChainTarget>(target.value());
        return eve::editing::applied<editor::AutomationOwnedTarget>(std::move(owned));
    }
};

Module_IMPL(AudioEditorModule, new AudioEditorModule());

AudioEditorModule::AudioEditorModule() : factory_(std::make_unique<TargetFactory>()) {
    auto* registry = eve::cap::query<editing::IEditingCommandRegistry>();
    if (!registry || !audio_editing::registerEditingCommands(*registry).ok())
        throw std::runtime_error("Failed to register audio editing commands");
    eve::cap::addListener<editor::IEditorAutomationTargetFactory>(factory_.get());
}

AudioEditorModule::~AudioEditorModule() {
    eve::cap::removeListener<editor::IEditorAutomationTargetFactory>(factory_.get());
    if (auto* registry = eve::cap::query<editing::IEditingCommandRegistry>())
        registry->unregisterOwner("audio_editing").ignore("audio editor adapter shutdown");
}

void AudioEditorModule::expose(ssq::Table& table) {
    auto module = table.addClass(name, AudioEditorModule::create, false);
    exposeAudioSourceEditorScriptBindings(table, module);
}
void AudioEditorModule::expose(ssq::Class&) {}

}  // namespace eve::audio_editor
