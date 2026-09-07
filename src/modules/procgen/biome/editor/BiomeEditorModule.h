#pragma once

#include "common/Module.h"

#include <memory>

namespace eve::biome_editor {

/**
 * @brief Composition adapter that contributes biome editing commands and automation targets.
 *
 * @ownership The automation factory is owned by this module. Registered commands are owned by the
 *            borrowed editing host and unregistered on destruction.
 * @threadaffinity Owner/composition thread only.
 * @reentrancy Do not construct or destroy while a command planner is running.
 */
class BiomeEditorModule final : public Module {
public:
    Module_REG(BiomeEditorModule);
    BiomeEditorModule();
    ~BiomeEditorModule() override;

private:
    class TargetFactory;
    std::unique_ptr<TargetFactory> factory_;
};

}  // namespace eve::biome_editor
