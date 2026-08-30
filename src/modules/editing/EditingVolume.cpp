#include "editing/EditingVolume.h"

namespace eve::editing {

void EditVolume::include(int x, int y, int z) {
    if (empty()) {
        minX = maxX = x;
        minY = maxY = y;
        minZ = maxZ = z;
        return;
    }
    if (x < minX) minX = x;
    if (x > maxX) maxX = x;
    if (y < minY) minY = y;
    if (y > maxY) maxY = y;
    if (z < minZ) minZ = z;
    if (z > maxZ) maxZ = z;
}

void EditVolume::include(const EditVolume& other) {
    if (other.empty()) return;
    include(other.minX, other.minY, other.minZ);
    include(other.maxX, other.maxY, other.maxZ);
}

EditRegion EditVolume::xzRegion() const {
    EditRegion region;
    if (!empty()) {
        region.include(minX, minZ);
        region.include(maxX, maxZ);
    }
    return region;
}

}  // namespace eve::editing
