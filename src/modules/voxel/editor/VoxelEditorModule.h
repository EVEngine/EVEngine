#pragma once

#include "common/Module.h"

#include <memory>

namespace eve::voxel_editor {

/**
 * @brief Composition adapter that contributes MagicaVoxel-style sculpt commands and automation targets.
 *
 * @ownership The automation factory is owned by this module. Registered commands are owned by the
 *            borrowed editing host and unregistered on destruction.
 * @threadaffinity Owner/composition thread only.
 * @reentrancy Do not construct or destroy while a command planner is running.
 */
class VoxelEditorModule final : public Module {
public:
    Module_REG(VoxelEditorModule);
    VoxelEditorModule();
    ~VoxelEditorModule() override;

private:
    class TargetFactory;
    std::unique_ptr<TargetFactory> factory_;
};

}  // namespace eve::voxel_editor
