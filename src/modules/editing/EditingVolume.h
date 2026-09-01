#pragma once

#include "editing/EditableTarget.h"

namespace eve::editing {

/** @brief Integer axis-aligned volume invalidated by a 3D edit. */
struct EditVolume {
    int minX = 0, minY = 0, minZ = 0;
    int maxX = -1, maxY = -1, maxZ = -1;
    bool empty() const { return maxX < minX || maxY < minY || maxZ < minZ; }
    void clear() { *this = {}; }
    void include(int x, int y, int z);
    void include(const EditVolume& other);
    EditRegion xzRegion() const;
};

/** @brief Stable capability for sparse integer volumes. */
class IIntVolumeTarget {
public:
    virtual ~IIntVolumeTarget() = default;
    static CapabilityId editingCapabilityId() {
        return CapabilityId("eve.editing.target.int-volume.v1");
    }
    virtual int readInt3(int x, int y, int z) const = 0;
    [[nodiscard]] virtual FieldWriteStatus writeInt3(int x, int y, int z, int value) = 0;
    virtual EditVolume dirtyVolume() const = 0;
    virtual void clearDirtyVolume() = 0;
};

}  // namespace eve::editing
