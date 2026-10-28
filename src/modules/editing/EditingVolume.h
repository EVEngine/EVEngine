#pragma once
#include "common/Export.h"


#include "editing/EditableTarget.h"

namespace eve::editing {

/** @brief Integer axis-aligned volume invalidated by a 3D edit. */
struct EVENGINE_API_PLATFORM EditVolume {
    int minX = 0, minY = 0, minZ = 0;
    int maxX = -1, maxY = -1, maxZ = -1;
    /** @brief Empty. */
    bool empty() const { return maxX < minX || maxY < minY || maxZ < minZ; }
    /** @brief Clears clear. */
    void clear() { *this = {}; }
    /** @brief Include. */
    void include(int x, int y, int z);
    /** @brief Include. */
    void include(const EditVolume& other);
    /** @brief Xz region. */
    EditRegion xzRegion() const;
};

/** @brief Stable capability for sparse integer volumes. */
class IIntVolumeTarget {
public:
    /** @brief Releases IIntVolumeTarget resources. */
    virtual ~IIntVolumeTarget() = default;
    /** @brief Editing capability id. */
    static CapabilityId editingCapabilityId() {
        /** @brief Capability id. */
        return CapabilityId("eve.editing.target.int-volume.v1");
    }
    /** @brief Reads int 3. */
    virtual int readInt3(int x, int y, int z) const = 0;
    /** @brief Writes int 3. */
    [[nodiscard]] virtual FieldWriteStatus writeInt3(int x, int y, int z, int value) = 0;
    /** @brief Dirty volume. */
    virtual EditVolume dirtyVolume() const = 0;
    /** @brief Clears dirty volume. */
    virtual void clearDirtyVolume() = 0;
};

}  // namespace eve::editing
