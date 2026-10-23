#pragma once
#include "common/Export.h"


#include "editing/EditingVolume.h"

#include <memory>
#include <string>

namespace eve::voxel {
class VoxelWorld;
}
namespace eve::voxel_editing {

/** @brief Non-owning authoring adapter from a live voxel world to the volume protocol. */
class EVENGINE_API_ORCHESTRATION VoxelWorldTarget final : public virtual editing::IEditableTarget,
                                                          public editing::IIntVolumeTarget {
public:
    /** @brief Voxel world target. */
    VoxelWorldTarget(std::string id, voxel::VoxelWorld* world);
    /** @brief Target id. */
    editing::TargetId         targetId() const override { return editing::TargetId(id_); }
    /** @brief Revision. */
    std::uint64_t             revision() const override;
    /** @brief Dirty region. */
    editing::EditRegion       dirtyRegion() const override { return dirty_.xzRegion(); }
    /** @brief Clears dirty region. */
    void                      clearDirtyRegion() override { dirty_.clear(); }
    /** @brief Describe. */
    editing::TargetDescriptor describe() const override;
    /** @brief Query a borrowed capability implemented by this target.
     * @return Borrowed pointer owned by this adapter, or null when unsupported.
     * @lifetime Valid until this adapter is destroyed.
     */
    void*                                   queryCapability(const editing::CapabilityId& capability) override;
    /** @brief Reads int 3. */
    int                                     readInt3(int x, int y, int z) const override;
    /** @brief Writes int 3. */
    [[nodiscard]] editing::FieldWriteStatus writeInt3(int x, int y, int z, int value) override;
    /** @brief Dirty volume. */
    editing::EditVolume                     dirtyVolume() const override { return dirty_; }
    /** @brief Clears dirty volume. */
    void                                    clearDirtyVolume() override { dirty_.clear(); }

private:
    std::string         id_;
    voxel::VoxelWorld*  world_ = nullptr;
    editing::EditVolume dirty_;
};

/**
 * @brief Create a voxel-world adapter.
 * @param id Stable target identity.
 * @param world Borrowed voxel world that must outlive the adapter.
 * @return Independently owned adapter.
 */
[[nodiscard]] EVENGINE_API_ORCHESTRATION std::unique_ptr<VoxelWorldTarget> createVoxelWorldTarget(
    std::string id, voxel::VoxelWorld* world);

}  // namespace eve::voxel_editing
