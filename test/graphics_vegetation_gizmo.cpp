#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "graphics/VegetationField.h"
#include "graphics/editing/VegetationFieldGizmo.h"

using namespace eve;
using namespace eve::graphics;
using namespace eve::graphics_editing;

TEST_CASE("graphics.vegetation.gizmo_preserves_rotated_shapes_channels_and_revision") {
    VegetationElement box;
    box.channel  = VegetationChannel::Motion;
    box.shape    = VegetationShape::Box;
    box.center   = {1.f, 2.f, 3.f};
    box.extents  = {4.f, 5.f, 6.f};
    box.yaw      = 0.75f;
    box.opacity  = 0.6f;
    box.priority = 2;
    box.mask     = {1, 1, {{1.f, 1.f, 1.f, 1.f}}};

    VegetationElement ellipsoid;
    ellipsoid.channel  = VegetationChannel::Color;
    ellipsoid.shape    = VegetationShape::Ellipsoid;
    ellipsoid.center   = {-2.f, 0.5f, 7.f};
    ellipsoid.extents  = {1.f, 2.f, 3.f};
    ellipsoid.yaw      = -0.25f;
    ellipsoid.priority = 1;

    VegetationField field;
    const std::array elements{box, ellipsoid};
    REQUIRE(field.replace({}, elements).ok());
    VegetationFieldGizmoBuilder builder;
    auto result = builder.build("vegetation-volume", field.revision(), field, 2);
    REQUIRE(result.ok());
    CHECK_EQ(result.value().status, StatusCode::Applied);
    CHECK_EQ(result.value().target, std::string("vegetation-volume"));
    CHECK_EQ(result.value().targetRevision, field.revision());
    REQUIRE_EQ(result.value().primitives.size(), 2u);

    const auto& first = result.value().primitives[0];
    CHECK_EQ(first.kind, std::string("ellipsoid"));
    CHECK_EQ(first.position[0], -2.0);
    CHECK_EQ(first.size[1], 4.0);
    CHECK_EQ(first.yaw, -0.25);
    CHECK_EQ(first.color[1], 0.85);
    CHECK(!first.dashed);

    const auto& second = result.value().primitives[1];
    CHECK_EQ(second.kind, std::string("obb"));
    CHECK_EQ(second.position[2], 3.0);
    CHECK_EQ(second.size[0], 8.0);
    CHECK_EQ(second.yaw, 0.75);
    CHECK_EQ(second.color[2], 1.0);
    CHECK(second.dashed);
}

TEST_CASE("graphics.vegetation.gizmo_rejects_stale_revision_and_budget_without_partial_output") {
    VegetationElement element;
    VegetationField field;
    REQUIRE(field.replace({}, std::span(&element, 1)).ok());
    VegetationFieldGizmoBuilder builder;

    auto stale = builder.build("vegetation-volume", field.revision() - 1, field);
    CHECK_EQ(stale.code(), StatusCode::Conflict);
    auto overBudget = builder.build("vegetation-volume", field.revision(), field, 0);
    CHECK_EQ(overBudget.code(), StatusCode::Rejected);
}
