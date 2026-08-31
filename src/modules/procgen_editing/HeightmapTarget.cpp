#include "procgen_editing/HeightmapTarget.h"

#include "procgen/heightmap/Heightmap.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace eve::procgen_editing {

HeightmapTarget::HeightmapTarget(std::string id, procgen::Heightmap* heightmap)
    : id_(std::move(id)), heightmap_(heightmap) {}
int HeightmapTarget::width() const { return heightmap_ ? heightmap_->getWidth() : 0; }
int HeightmapTarget::height() const { return heightmap_ ? heightmap_->getHeight() : 0; }
bool HeightmapTarget::containsCell(int x, int y) const { return heightmap_ && heightmap_->inBounds(x, y); }
float HeightmapTarget::readScalar(int x, int y) const {
    return containsCell(x, y) ? heightmap_->height(x, y) : 0.0F;
}
editing::FieldWriteStatus HeightmapTarget::writeScalar(int x, int y, float value) {
    if (!containsCell(x, y)) return editing::FieldWriteStatus::Rejected;
    if (heightmap_->height(x, y) == value) return editing::FieldWriteStatus::Unchanged;
    heightmap_->setHeight(x, y, value);
    ++revision_;
    dirty_.include(x, y);
    return editing::FieldWriteStatus::Applied;
}
float HeightmapTarget::sampleScalar(float x, float y) const {
    return heightmap_ ? heightmap_->sampleBilinear(x, y) : 0.0F;
}

std::unique_ptr<HeightmapTarget> createHeightmapTarget(std::string id,
                                                       procgen::Heightmap* heightmap) {
    return std::make_unique<HeightmapTarget>(std::move(id), heightmap);
}

editing::Result<int> applyHeightmapBrush(procgen::Heightmap* heightmap, float centerX,
                                         float centerY, float radius, float strength) {
    if (!heightmap || radius < 0.0F)
        return editing::Result<int>::error(
            editing::Status::Rejected, editing::RuleId("procgen.heightmap.invalid-brush"),
            "Heightmap brush requires a target and non-negative radius");
    if (strength == 0.0F) {
        editing::Result<int> result;
        result.status = editing::Status::NoOp;
        result.value  = 0;
        return result;
    }
    const int minX = std::max(0, static_cast<int>(std::floor(centerX - radius)));
    const int maxX = std::min(heightmap->getWidth() - 1, static_cast<int>(std::ceil(centerX + radius)));
    const int minY = std::max(0, static_cast<int>(std::floor(centerY - radius)));
    const int maxY = std::min(heightmap->getHeight() - 1, static_cast<int>(std::ceil(centerY + radius)));
    const float edge = radius + 0.5F;
    int changed = 0;
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const float dx = static_cast<float>(x) - centerX;
            const float dy = static_cast<float>(y) - centerY;
            const float distance = std::sqrt(dx * dx + dy * dy);
            if (distance > radius) continue;
            const float oldHeight = heightmap->height(x, y);
            const float newHeight = std::clamp(oldHeight + strength * (1.0F - distance / edge), 0.0F, 1.0F);
            if (newHeight == oldHeight) continue;
            heightmap->setHeight(x, y, newHeight);
            ++changed;
        }
    }
    return editing::Result<int>::applied(changed);
}

}  // namespace eve::procgen_editing
