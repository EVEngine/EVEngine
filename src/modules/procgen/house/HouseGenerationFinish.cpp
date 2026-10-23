#include "procgen/house/HouseGenerationState.h"

namespace eve::housegen::house_detail {
eve::Result<void> State::finish() {
    // Expand the canonical footprint to every cell occupied by rotated multi-cell components.
    int occupiedWidth = r.width, occupiedDepth = r.depth;
    for (const HouseInstance& instance : generated.instances) {
        const auto component = library_.find(instance.componentId);
        if (!component) continue;
        const int  rotation = (instance.rotationDeg % 360 + 360) % 360;
        const bool quarter  = (rotation / 90) % 2 != 0;
        occupiedWidth =
            std::max(occupiedWidth, instance.x + (quarter ? component->get().depth : component->get().width));
        occupiedDepth =
            std::max(occupiedDepth, instance.y + (quarter ? component->get().width : component->get().depth));
    }
    std::vector<uint8_t> occupiedMask(size_t(occupiedWidth * occupiedDepth), 0);
    for (int y = 0; y < r.depth; ++y)
        for (int x = 0; x < r.width; ++x)
            occupiedMask[size_t(y * occupiedWidth + x)] = baseMask[size_t(y * r.width + x)];
    for (const HouseInstance& instance : generated.instances) {
        const auto component = library_.find(instance.componentId);
        if (!component) continue;
        const int  rotation  = (instance.rotationDeg % 360 + 360) % 360;
        const bool quarter   = (rotation / 90) % 2 != 0;
        const int  cellWidth = quarter ? component->get().depth : component->get().width;
        const int  cellDepth = quarter ? component->get().width : component->get().depth;
        for (int y = 0; y < cellDepth; ++y)
            for (int x = 0; x < cellWidth; ++x)
                occupiedMask[size_t((instance.y + y) * occupiedWidth + instance.x + x)] = 1;
    }
    generated.footprintWidth = occupiedWidth;
    generated.footprintDepth = occupiedDepth;
    generated.footprintMask  = std::move(occupiedMask);
    auto validated           = generated.validate(library_);
    if (!validated.ok()) return validated;

    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}
}  // namespace eve::housegen::house_detail
