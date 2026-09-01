#include "procgen/PointSet.h"
#include "procgen/heightmap/Heightmap.h"

#include <cmath>

namespace eve::procgen {

PointSet projectPointsToHeightmap(const PointSet& input, const Heightmap& heightmap, float originX, float originZ,
                                  float cellSize, float heightScale) {
    if (cellSize <= 0.f || heightmap.getWidth() <= 0 || heightmap.getHeight() <= 0) return {};
    PointSet output = input;
    for (size_t index = 0; index < output.points().size(); ++index) {
        auto&       point = output.mutablePoint(index);
        const float hx = (point.x - originX) / cellSize;
        const float hz = (point.z - originZ) / cellSize;
        point.y        = heightmap.sampleBilinear(hx, hz) * heightScale;

        const float left   = heightmap.sampleBilinear(hx - 0.5f, hz) * heightScale;
        const float right  = heightmap.sampleBilinear(hx + 0.5f, hz) * heightScale;
        const float down   = heightmap.sampleBilinear(hx, hz - 0.5f) * heightScale;
        const float up     = heightmap.sampleBilinear(hx, hz + 0.5f) * heightScale;
        float       nx     = -(right - left) / cellSize;
        float       ny     = 1.f;
        float       nz     = -(up - down) / cellSize;
        const float length = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (length > 0.f) {
            nx /= length;
            ny /= length;
            nz /= length;
        }
        point.normalX = nx;
        point.normalY = ny;
        point.normalZ = nz;
    }
    return output;
}

}  // namespace eve::procgen
