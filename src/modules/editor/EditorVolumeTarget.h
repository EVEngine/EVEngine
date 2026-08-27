#pragma once

#include "editor/EditCommand.h"
#include "editor/EditorTargetV2.h"

#include <memory>
#include <string>
#include <vector>

namespace eve::voxel {
class VoxelWorld;
}

namespace eve::editor {

/** @brief Integer axis-aligned volume invalidated by a 3D edit. */
struct EditVolume {
    int minX = 0;
    int minY = 0;
    int minZ = 0;
    int maxX = -1;
    int maxY = -1;
    int maxZ = -1;

    /** @brief Return true when no coordinate is included. */
    bool empty() const { return maxX < minX || maxY < minY || maxZ < minZ; }
    /** @brief Reset to an empty volume. */
    void clear() { *this = {}; }
    /** @brief Include one integer coordinate. */
    void include(int x, int y, int z);
    /** @brief Include another volume. */
    void include(const EditVolume& other);
    /** @brief Project X/Z bounds for compatibility with 2D invalidation consumers. */
    EditRegion xzRegion() const;
};

/** @brief Stable capability for sparse integer volumes such as voxel worlds. */
class IIntVolumeTarget {
public:
    virtual ~IIntVolumeTarget() = default;
    /** @brief Stable capability identity used by V2 target discovery. */
    static CapabilityId editorCapabilityId() { return CapabilityId("eve.editor.target.int-volume"); }
    /** @brief Read one integer cell. */
    virtual int readInt3(int x, int y, int z) const = 0;
    /** @brief Write one integer cell and return true only when it changed. */
    virtual bool writeInt3(int x, int y, int z, int value) = 0;
    /** @brief Return accumulated three-dimensional invalidation. */
    virtual EditVolume dirtyVolume() const = 0;
    /** @brief Clear accumulated three-dimensional invalidation. */
    virtual void clearDirtyVolume() = 0;
};

#ifdef EVENGINE_HAS_VOXEL
/** @brief Non-owning adapter from a live voxel world to the generic volume protocol. */
class VoxelWorldTarget final : public IEditableTargetV2, public IIntVolumeTarget {
public:
    /** @brief Create a non-owning editor adapter for a live voxel world. */
    VoxelWorldTarget(std::string id, voxel::VoxelWorld* world);
    const std::string& targetId() const override { return id_; }
    unsigned long long revision() const override;
    EditRegion dirtyRegion() const override { return dirty_.xzRegion(); }
    void clearDirtyRegion() override { dirty_.clear(); }
    TargetDescriptor describe() const override;
    void* queryCapability(const CapabilityId& capability) override;
    int readInt3(int x, int y, int z) const override;
    bool writeInt3(int x, int y, int z, int value) override;
    EditVolume dirtyVolume() const override { return dirty_; }
    void clearDirtyVolume() override { dirty_.clear(); }

private:
    std::string id_;
    voxel::VoxelWorld* world_ = nullptr;
    EditVolume dirty_;
};
#endif

/** @brief Reversible integer-volume command shared by voxel and custom volume targets. */
class IntVolumeEditCommand final : public IEditCommand {
public:
    /** @brief Create an unapplied command for one editable volume target. */
    IntVolumeEditCommand(std::string name, IEditableTarget* target);
    /** @brief Record a desired value without applying it. */
    bool record(int x, int y, int z, int after);
    const std::string& name() const override { return name_; }
    EditRegion dirtyRegion() const override { return dirty_.xzRegion(); }
    bool apply() override;
    void revert() override;
    [[nodiscard]] std::unique_ptr<IEditCommand> clone() const override;
    bool mergeWith(const IEditCommand& later) override;

private:
    struct Change {
        int x = 0;
        int y = 0;
        int z = 0;
        int before = 0;
        int after = 0;
    };
    std::string name_;
    IEditableTarget* target_ = nullptr;
    EditVolume dirty_;
    std::vector<Change> changes_;
};

}  // namespace eve::editor
