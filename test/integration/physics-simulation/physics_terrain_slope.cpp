#include <algorithm>
#include <array>
#include <vector>
#include "physics/Body3D.h"
#include "physics/World3D.h"
#include "zeroerr/unittest.h"
namespace {
std::array<float, 3> traverse(float limit, float start, float dx, float dz, int ticks) {
    eve::physics::World3D world(0, 0, 0, false);
    std::vector<float>    heights(25 * 25);
    for (int z = 0; z < 25; ++z)
        for (int x = 0; x < 25; ++x) heights[z * 25 + x] = std::max(0.f, x - 4.f) * 0.577350269f;
    auto* ground = world.newBody("static", 0, 0, 0);
    REQUIRE(ground->newHeightFieldShape(25, 25, 1, 1, heights, -1, 13) != nullptr);
    world.setMoverSlopeLimit(limit);
    std::array<float, 3> p{start, std::max(0.f, start - 4) * 0.577350269f + 0.05f, 8};
    for (int i = 0; i < ticks; ++i) {
        world.moveCapsule(p[0], p[1] + 0.3f, p[2], p[0], p[1] + 1.3f, p[2], 0.3f, dx, -0.01f, dz);
        p[0] += world.getMoverDeltaX();
        p[1] += world.getMoverDeltaY();
        p[2] += world.getMoverDeltaZ();
    }
    return p;
}
}  // namespace
TEST_CASE("physics.terrainSlope.blocksUphillByPolicy") {
    const auto low  = traverse(20, 2, 0.04f, 0, 300);
    const auto high = traverse(40, 2, 0.04f, 0, 300);
    CHECK(low[0] < 5);
    CHECK(high[0] > 10);
}
TEST_CASE("physics.terrainSlope.permitsDownhillAndLateral") {
    const auto down = traverse(20, 10, -0.04f, 0, 100);
    const auto side = traverse(20, 10, 0, 0.04f, 100);
    CHECK(down[0] < 8);
    CHECK(side[2] > 10);
}
