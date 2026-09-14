#include <algorithm>
#include <cmath>
#include "graphics/Grass.h"
namespace eve::graphics::grass {
namespace {
float encodedIdentity(const Point &point) {
    if (point.tint.x < 0) return float(point.id);
    const auto     channel = [](float value) { return uint32_t(std::lround(std::clamp(value, 0.f, 1.f) * 255.f)); };
    const uint32_t rgb     = (channel(point.tint.r) << 16) | (channel(point.tint.g) << 8) | channel(point.tint.b);
    return -float(rgb + 1u);
}
}  // namespace
BillboardMesh buildBillboards(const std::vector<Point> &points, float width, float height, bool alwaysDark) {
    (void)width;
    (void)height;
    BillboardMesh mesh;
    const size_t  n = points.size();
    mesh.posXYZ.reserve(n * 4 * 3);
    mesh.nrmXYZ.reserve(n * 4 * 3);
    mesh.uvST.reserve(n * 4 * 2);
    mesh.indices.reserve(n * 6);

    // Local corners: (0,0) bottom-left, (1,0) bottom-right, (1,1) top-right, (0,1) top-left.
    // Root is the bottom-center of the rectangle, i.e. UV (0.5, 0).
    const float    cu[4]      = {0.f, 1.f, 1.f, 0.f};
    const float    cv[4]      = {0.f, 0.f, 1.f, 1.f};
    const uint32_t corners[6] = {0, 1, 2, 0, 2, 3};

    for (size_t i = 0; i < n; ++i) {
        const Point   &p        = points[i];
        const uint32_t base     = uint32_t(i * 4);
        const float    identity = encodedIdentity(p);
        for (int c = 0; c < 4; ++c) {
            mesh.posXYZ.push_back(p.position.x);
            mesh.posXYZ.push_back(p.position.y);
            mesh.posXYZ.push_back(p.position.z);
            mesh.nrmXYZ.push_back(identity);
            mesh.nrmXYZ.push_back(p.scale > 1e-3f ? p.scale : 1.f);
            mesh.nrmXYZ.push_back(p.widthScale > 0.f ? (alwaysDark ? 1.f + p.widthScale : -p.widthScale)
                                                     : (alwaysDark ? 1.f : 0.f));
            mesh.uvST.push_back(cu[c]);
            mesh.uvST.push_back(cv[c]);
        }
        for (uint32_t k : corners) mesh.indices.push_back(base + k);
    }
    return mesh;
}

}  // namespace eve::graphics::grass
