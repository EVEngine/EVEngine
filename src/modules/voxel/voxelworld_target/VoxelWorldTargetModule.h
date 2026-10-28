#pragma once

#include "common/Module.h"

#include <memory>
#include <string>

namespace eve::editor { class EditorSession; }
namespace eve::voxel { class VoxelWorld; }
namespace eve::voxel_editing { class VoxelWorldTarget; }

namespace eve::voxelworld_target {

/** @brief Script-facing composition module for live voxel-world editing targets. */
class VoxelWorldTargetModule final : public Module {
public:
    Module_REG(VoxelWorldTargetModule);

    /** @brief Create an independently owned adapter over a borrowed voxel world. */
    [[nodiscard]] std::unique_ptr<voxel_editing::VoxelWorldTarget> newTarget(
        std::string id, voxel::VoxelWorld* world) const;
    /** @brief Bind borrowed target and session objects without taking ownership. @thread Owner-thread only. */
    void bind(editor::EditorSession* session, voxel_editing::VoxelWorldTarget* target) const;
};

}  // namespace eve::voxelworld_target
