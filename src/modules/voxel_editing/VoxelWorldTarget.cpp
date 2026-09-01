#include "voxel_editing/VoxelWorldTarget.h"
#include "voxel/VoxelWorld.h"

#include <utility>

namespace eve::voxel_editing {

VoxelWorldTarget::VoxelWorldTarget(std::string id, voxel::VoxelWorld* world)
    : id_(std::move(id)), world_(world) {}
unsigned long long VoxelWorldTarget::revision() const { return world_ ? world_->getRevision() : 0; }
editing::TargetDescriptor VoxelWorldTarget::describe() const {
    return {editing::TargetId(id_), "voxel-world", revision(), false,
            {editing::IIntVolumeTarget::editingCapabilityId()}};
}
void* VoxelWorldTarget::queryCapability(const editing::CapabilityId& capability) {
    return capability == editing::IIntVolumeTarget::editingCapabilityId()
               ? static_cast<editing::IIntVolumeTarget*>(this) : nullptr;
}
int VoxelWorldTarget::readInt3(int x, int y, int z) const {
    return world_ ? static_cast<int>(world_->getVoxel(x, y, z)) : 0;
}
editing::FieldWriteStatus VoxelWorldTarget::writeInt3(int x, int y, int z, int value) {
    if (!world_ || value < 0 || value > 255) return editing::FieldWriteStatus::Rejected;
    if (readInt3(x, y, z) == value) return editing::FieldWriteStatus::Unchanged;
    world_->setVoxel(x, y, z, static_cast<uint8_t>(value));
    dirty_.include(x, y, z);
    return editing::FieldWriteStatus::Applied;
}

std::unique_ptr<VoxelWorldTarget> createVoxelWorldTarget(std::string id,
                                                         voxel::VoxelWorld* world) {
    return std::make_unique<VoxelWorldTarget>(std::move(id), world);
}

}  // namespace eve::voxel_editing
