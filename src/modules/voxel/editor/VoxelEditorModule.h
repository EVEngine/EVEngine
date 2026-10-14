#pragma once
#include "common/Export.h"


#include "common/Module.h"

#include <memory>

namespace eve::voxel_editor {

class VoxelEditorPackageProvider;

/**
 * @brief Composition adapter that contributes MagicaVoxel-style sculpt commands, automation targets,
 *        and EditorPackage advertisement for Host autoMount.
 *
 * @ownership The automation factory and package provider are owned by this module. Registered
 *            commands are owned by the borrowed editing host and unregistered on destruction.
 * @threadaffinity Owner/composition thread only.
 * @reentrancy Do not construct or destroy while a command planner is running.
 */
class EVENGINE_API_EDITORS VoxelEditorModule final : public Module {
public:
    Module_REG(VoxelEditorModule);
    /** @brief Voxel editor module. */
    VoxelEditorModule();
    /** @brief Voxel editor module. */
    ~VoxelEditorModule() override;

private:
    class TargetFactory;
    std::unique_ptr<TargetFactory>              factory_;
    std::unique_ptr<VoxelEditorPackageProvider> packageProvider_;
};

}  // namespace eve::voxel_editor
