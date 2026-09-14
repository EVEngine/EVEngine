#pragma once

#include "common/Module.h"

#include <memory>

namespace eve::audio_editor {

/**
 * @brief Composition adapter that contributes audio editing commands, automation
 *        targets, and the script-owned AudioSourceEditor factory.
 *
 * @ownership The automation factory is owned by this module. Registered commands
 *            are owned by the borrowed editing host and unregistered on destruction.
 *            Script editor objects are owned by the Squirrel VM release hook.
 * @threadaffinity Owner/composition thread only.
 * @reentrancy Do not construct or destroy while a command planner is running.
 */
class AudioEditorModule final : public Module {
public:
    Module_REG(AudioEditorModule);
    AudioEditorModule();
    ~AudioEditorModule() override;

private:
    class TargetFactory;
    std::unique_ptr<TargetFactory> factory_;
};

}  // namespace eve::audio_editor
