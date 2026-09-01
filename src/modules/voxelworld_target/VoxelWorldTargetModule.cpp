#include "voxelworld_target/VoxelWorldTargetModule.h"

#include "editor/EditorSession.h"
#include "voxel_editing/VoxelWorldTarget.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cstdint>
#include <functional>
#include <utility>

namespace eve::voxelworld_target {

Module_IMPL(VoxelWorldTargetModule, new VoxelWorldTargetModule());

std::unique_ptr<voxel_editing::VoxelWorldTarget> VoxelWorldTargetModule::newTarget(
    std::string id, voxel::VoxelWorld* world) const {
    return voxel_editing::createVoxelWorldTarget(std::move(id), world);
}

void VoxelWorldTargetModule::bind(editor::EditorSession* session,
                                  voxel_editing::VoxelWorldTarget* target) const {
    if (session) session->bindTarget(target);
}

void VoxelWorldTargetModule::expose(ssq::Table& table) {
    auto target = table.addClass<voxel_editing::VoxelWorldTarget>(
        "VoxelWorldTarget",
        std::function<voxel_editing::VoxelWorldTarget*()>([]() -> voxel_editing::VoxelWorldTarget* {
            return nullptr;
        }),
        true);
    target.addFunc("getTargetId", [](voxel_editing::VoxelWorldTarget* self) {
        return self ? self->targetId().value() : std::string{};
    });
    target.addFunc("getRevision", [](voxel_editing::VoxelWorldTarget* self) {
        return self ? static_cast<int64_t>(self->revision()) : int64_t{0};
    });
    target.addFunc("readInt3", &voxel_editing::VoxelWorldTarget::readInt3);
    target.addFunc("writeInt3", [](voxel_editing::VoxelWorldTarget* self, int x, int y, int z, int value) {
        return self && self->writeInt3(x, y, z, value) == editing::FieldWriteStatus::Applied;
    });
    target.addFunc("clearDirtyVolume", &voxel_editing::VoxelWorldTarget::clearDirtyVolume);
    target.addFunc("getDirtyMinX", [](voxel_editing::VoxelWorldTarget* self) {
        return self ? self->dirtyVolume().minX : 0;
    });
    target.addFunc("getDirtyMinY", [](voxel_editing::VoxelWorldTarget* self) {
        return self ? self->dirtyVolume().minY : 0;
    });
    target.addFunc("getDirtyMinZ", [](voxel_editing::VoxelWorldTarget* self) {
        return self ? self->dirtyVolume().minZ : 0;
    });
    target.addFunc("getDirtyMaxX", [](voxel_editing::VoxelWorldTarget* self) {
        return self ? self->dirtyVolume().maxX : -1;
    });
    target.addFunc("getDirtyMaxY", [](voxel_editing::VoxelWorldTarget* self) {
        return self ? self->dirtyVolume().maxY : -1;
    });
    target.addFunc("getDirtyMaxZ", [](voxel_editing::VoxelWorldTarget* self) {
        return self ? self->dirtyVolume().maxZ : -1;
    });

    auto module = table.addClass(name, VoxelWorldTargetModule::create, false);
    expose(module);
}

void VoxelWorldTargetModule::expose(ssq::Class& cls) {
    cls.addFunc("create", [](VoxelWorldTargetModule* self, const std::string& id,
                             voxel::VoxelWorld* world) {
        return self->newTarget(id, world).release();
    });
    cls.addFunc("bind", &VoxelWorldTargetModule::bind);
}

}  // namespace eve::voxelworld_target
