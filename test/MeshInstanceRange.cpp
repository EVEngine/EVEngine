#include "graphics/MeshInstanceRange.h"
#include <limits>
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

TEST_CASE("graphics.instance range culling respects Vulkan near plane distance and transformed bounds") {
    using namespace eve::graphics;
    MeshInstanceRange range{0, 3, {-.2f, -.2f, .2f}, {.2f, .2f, .8f}, 0};
    REQUIRE(validateMeshInstanceRange(range).ok());
    const glm::mat4 identity(1);
    CHECK(meshInstanceRangeVisible(range, identity, identity, glm::vec3(0)));
    auto moved  = identity;
    moved[3][0] = 1.1f;
    CHECK(meshInstanceRangeVisible(range, moved, identity, glm::vec3(0)));
    moved[3][0] = 1.3f;
    CHECK(!meshInstanceRangeVisible(range, moved, identity, glm::vec3(0)));
    moved       = identity;
    moved[3][2] = -1;
    CHECK(!meshInstanceRangeVisible(range, moved, identity, glm::vec3(0)));
    range.maximumHorizontalDistance = 1;
    CHECK(!meshInstanceRangeVisible(range, identity, identity, glm::vec3(10, 0, 0)));
    CHECK(meshInstanceRangeVisible(range, identity, identity, glm::vec3(0, 100, 0)));
    range.count = 0;
    CHECK(!meshInstanceRangeVisible(range, identity, identity, glm::vec3(0)));
}
TEST_CASE("graphics.instance range rejects overflow inverted bounds and invalid distance") {
    using namespace eve::graphics;
    MeshInstanceRange range;
    range.first = UINT32_MAX;
    range.count = 1;
    CHECK(!validateMeshInstanceRange(range).ok());
    range.first      = 0;
    range.minimum[0] = 1;
    CHECK(!validateMeshInstanceRange(range).ok());
    range.minimum[0]                = 0;
    range.maximumHorizontalDistance = -1;
    CHECK(!validateMeshInstanceRange(range).ok());
    range.maximumHorizontalDistance = 0;
    range.maximum[1]                = std::numeric_limits<float>::infinity();
    CHECK(!validateMeshInstanceRange(range).ok());
}
