#pragma once

#include "editing/EditingVolume.h"

#include <memory>
#include <string>

namespace eve::voxel { class VoxelWorld; }
namespace eve::voxel_editing {

/** @brief Non-owning authoring adapter from a live voxel world to the volume protocol. */
class VoxelWorldTarget final : public virtual editing::IEditableTarget,
                               public editing::IIntVolumeTarget {
public:
    VoxelWorldTarget(std::string id, voxel::VoxelWorld* world);
    const std::string& targetId() const override { return id_; }
    unsigned long long revision() const override;
    editing::EditRegion dirtyRegion() const override { return dirty_.xzRegion(); }
    void clearDirtyRegion() override { dirty_.clear(); }
    editing::TargetDescriptor describe() const override;
    /** @brief Query a borrowed capability implemented by this target.
     * @return Borrowed pointer owned by this adapter, or null when unsupported.
     * @lifetime Valid until this adapter is destroyed.
     */
    void* queryCapability(const editing::CapabilityId& capability) override;
    int readInt3(int x, int y, int z) const override;
    [[nodiscard]] editing::FieldWriteStatus writeInt3(int x, int y, int z, int value) override;
    editing::EditVolume dirtyVolume() const override { return dirty_; }
    void clearDirtyVolume() override { dirty_.clear(); }
private:
    std::string id_;
    voxel::VoxelWorld* world_ = nullptr;
    editing::EditVolume dirty_;
};

/**
 * @brief Create a voxel-world adapter.
 * @param id Stable target identity.
 * @param world Borrowed voxel world that must outlive the adapter.
 * @return Independently owned adapter.
 */
[[nodiscard]] std::unique_ptr<VoxelWorldTarget> createVoxelWorldTarget(std::string id,
                                                                      voxel::VoxelWorld* world);

}  // namespace eve::voxel_editing
