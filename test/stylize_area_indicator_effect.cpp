#include "zeroerr/unittest.h"
#include "zeroerr/assert.h"

#include "stylize/AreaIndicatorEffect.h"

#include <glm/geometric.hpp>

using namespace eve::stylize;

TEST_CASE("stylize.areaIndicator.circleBuildsClosedFan") {
    AreaIndicatorConfig config;
    config.shape = AreaIndicatorShape::Circle;
    config.radius = 2.f;
    config.segments = 16;
    const auto result = buildAreaIndicator(config);
    REQUIRE_EQ(static_cast<int>(result.status),
               static_cast<int>(AreaIndicatorBuildStatus::Built));
    REQUIRE_EQ(result.mesh.vertices.size(), 18u);
    REQUIRE_EQ(result.mesh.indices.size(), 48u);
    REQUIRE(glm::length(result.mesh.vertices[1].position -
                        result.mesh.vertices.back().position) < 1e-4f);
}

TEST_CASE("stylize.areaIndicator.ringBuildsInnerOuterStrip") {
    AreaIndicatorConfig config;
    config.shape = AreaIndicatorShape::Ring;
    config.radius = 3.f;
    config.innerRadius = 2.f;
    config.segments = 12;
    const auto result = buildAreaIndicator(config);
    REQUIRE_EQ(static_cast<int>(result.status),
               static_cast<int>(AreaIndicatorBuildStatus::Built));
    REQUIRE_EQ(result.mesh.vertices.size(), 26u);
    REQUIRE_EQ(result.mesh.indices.size(), 72u);
    REQUIRE(glm::length(result.mesh.vertices[0].position) > 1.99f);
    REQUIRE(glm::length(result.mesh.vertices[1].position) > 2.99f);
}

TEST_CASE("stylize.areaIndicator.sectorAndRectangleRespectBounds") {
    AreaIndicatorConfig sector;
    sector.shape = AreaIndicatorShape::Sector;
    sector.radius = 4.f;
    sector.sectorAngleRadians = 1.57079632679f;
    sector.segments = 8;
    const auto cone = buildAreaIndicator(sector);
    REQUIRE_EQ(cone.mesh.indices.size(), 24u);

    AreaIndicatorConfig rectangle;
    rectangle.shape = AreaIndicatorShape::Rectangle;
    rectangle.width = 2.f;
    rectangle.length = 5.f;
    const auto box = buildAreaIndicator(rectangle);
    REQUIRE_EQ(box.mesh.vertices.size(), 4u);
    REQUIRE_EQ(box.mesh.indices.size(), 6u);
    REQUIRE(box.mesh.vertices[0].position.x == -1.f);
    REQUIRE(box.mesh.vertices[3].position.z == 5.f);
}

TEST_CASE("stylize.areaIndicator.rejectsInvalidDimensions") {
    AreaIndicatorConfig ring;
    ring.shape = AreaIndicatorShape::Ring;
    ring.radius = 1.f;
    ring.innerRadius = 1.f;
    const auto result = buildAreaIndicator(ring);
    REQUIRE_EQ(static_cast<int>(result.status),
               static_cast<int>(AreaIndicatorBuildStatus::InvalidConfig));
    REQUIRE(result.mesh.vertices.empty());
}
