#include "hexmap/HexCoordinates.h"

namespace eve::hexmap {

HexCoordinates HexCoordinates::fromWorldPosition(HexVec3 position) noexcept {
    const float diameter = HexMetrics::innerDiameter();
    if (diameter <= 0.f) return HexCoordinates{};
    const float fx     = position.x / diameter;
    const float fy     = -fx;
    const float offset = position.z / (HexMetrics::outerRadius() * 3.f);

    const float ax = fx - offset;
    const float ay = fy - offset;

    std::int32_t ix = static_cast<std::int32_t>(std::lround(ax));
    std::int32_t iy = static_cast<std::int32_t>(std::lround(ay));
    std::int32_t iz = static_cast<std::int32_t>(std::lround(-ax - ay));

    if (ix + iy + iz != 0) {
        const float dx = std::fabs(ax - static_cast<float>(ix));
        const float dy = std::fabs(ay - static_cast<float>(iy));
        const float dz = std::fabs(-ax - ay - static_cast<float>(iz));
        if (dx > dy && dx > dz)
            ix = -iy - iz;
        else if (dz > dy)
            iz = -ix - iy;
    }
    return HexCoordinates{ix, iz};
}

}  // namespace eve::hexmap
