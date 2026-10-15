#include "procgen/ObjectBuildLayer.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <set>

using namespace eve::procgen;

TEST_CASE("procgen.objectBuild.weightedTransformsOrientationChildrenAndPlacement") {
    PointSet source;
    for (int index = 0; index < 32; ++index) {
        const int point = source.add(float(index), 2.f, 0.f);
        REQUIRE(source.trySetPointId(point, std::uint64_t(index + 1)).ok());
        REQUIRE(source.trySetFloatAttribute(point, "surface_highest_y", 10.f + float(index)).ok());
    }
    PointSet orientation;
    orientation.add(0.f, 0.f, 1.f);  // North has priority for the first source point.

    ObjectBuildLayer layer;
    REQUIRE(layer.addAsset("tree/oak", 3.f).ok());
    REQUIRE(layer.addAsset("tree/birch", 1.f).ok());
    layer.setSeed(77);
    layer.setLayerOffset(1.f, 2.f, 3.f);
    REQUIRE(layer.setLayerScale(2.f, 3.f, 4.f).ok());
    REQUIRE(layer.setRandomRotation(5.f, 5.f, 7.f, 7.f, 9.f, 9.f).ok());
    REQUIRE(layer.setRandomScale(0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, true).ok());
    REQUIRE(layer.setOrientation(1.f, 10.f, false).ok());
    layer.setPlaceOnTop(true, false, 0.25f);
    REQUIRE(layer.addChild("grass/tuft", 2, 0.5f, 0.8f, 1.2f, -30.f, 30.f).ok());

    auto first = layer.build(source, &orientation);
    auto again = layer.build(source, &orientation);
    REQUIRE(first.ok());
    REQUIRE(again.ok());
    CHECK_EQ(first.value().getCount(), 96);
    CHECK_EQ(again.value().getCount(), first.value().getCount());

    std::set<std::string> selectedAssets;
    for (int index = 0; index < first.value().getCount(); ++index) {
        CHECK_EQ(first.value().getX(index), again.value().getX(index));
        CHECK_EQ(first.value().getY(index), again.value().getY(index));
        CHECK_EQ(first.value().getZ(index), again.value().getZ(index));
        CHECK_EQ(first.value().getYaw(index), again.value().getYaw(index));
        CHECK_EQ(first.value().getStringAttribute(index, "asset", ""),
                 again.value().getStringAttribute(index, "asset", ""));
        const auto role = first.value().getStringAttribute(index, "object_role", "");
        if (role == "parent") selectedAssets.insert(first.value().getStringAttribute(index, "asset", ""));
    }
    CHECK_EQ(selectedAssets.size(), 2);

    CHECK_EQ(first.value().getStringAttribute(0, "object_role", ""), "parent");
    CHECK_EQ(first.value().getYaw(0), 287.f);  // North 270 + offset 10 + fixed random yaw 7.
    CHECK_EQ(first.value().getPitch(0), 5.f);
    CHECK_EQ(first.value().getRoll(0), 9.f);
    CHECK_EQ(first.value().getScaleX(0), 1.f);
    CHECK_EQ(first.value().getScaleY(0), 1.5f);
    CHECK_EQ(first.value().getScaleZ(0), 2.f);
    CHECK_EQ(first.value().getY(0), 12.25f);  // surface 10 + top 0.25 + layer Y 2.
    CHECK_EQ(first.value().getStringAttribute(1, "object_role", ""), "child");
    CHECK_EQ(first.value().getIntAttribute(1, "parent_index", -2), 0);
}

TEST_CASE("procgen.objectBuild.rejectsInvalidConfigurationWithoutPartialOutput") {
    PointSet source;
    source.add(0.f, 0.f, 0.f);
    ObjectBuildLayer layer;
    CHECK(!layer.build(source).ok());
    CHECK(!layer.addAsset("", 1.f).ok());
    CHECK(!layer.addAsset("tree", 0.f).ok());
    CHECK(!layer.setLayerScale(1.f, 0.f, 1.f).ok());
    CHECK(!layer.setPositionRadius(-1.f).ok());
    CHECK(!layer.setOrientation(0.f, 0.f, false).ok());
    CHECK(!layer.addChild("grass", -1, 1.f, 1.f, 1.f, 0.f, 0.f).ok());
}
