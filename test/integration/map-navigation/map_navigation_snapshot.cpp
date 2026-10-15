#include <limits>
#include "map/Pathfinder.h"
#include "zeroerr/unittest.h"
using namespace eve::map;
TEST_CASE("map.navigation.snapshotOwnsInputsAndOnlyVersionsChangedContent") {
    NavigationGridSnapshot captured;
    {
        Pathfinder map(3, 3);
        auto       first = map.captureNavigationGrid();
        REQUIRE(first.ok());
        captured = std::move(first).takeValue();
        CHECK_EQ(captured.width(), 3);
        CHECK_EQ(captured.costs().size(), 9u);
        const auto revision = captured.revision();
        CHECK_EQ(map.captureNavigationGrid().value().revision(), revision);
        map.setBlocked(1, 1, true);
        auto changed = map.captureNavigationGrid().takeValue();
        CHECK(changed.revision() > revision);
        CHECK_EQ(changed.costs()[4], 0.f);
        CHECK_EQ(captured.costs()[4], 1.f);
        map.setBlocked(1, 1, true);
        CHECK_EQ(map.captureNavigationGrid().value().revision(), changed.revision());
        map.setTopology("hex");
        auto hex = map.captureNavigationGrid().takeValue();
        CHECK(hex.topology() == NavigationTopology::Hex);
        CHECK(hex.revision() > changed.revision());
    }
    CHECK(captured.valid());
    CHECK_EQ(captured.costs()[4], 1.f);
}
TEST_CASE("map.navigation.snapshotRejectsAbsentAndMalformedSources") {
    Pathfinder map;
    REQUIRE(!map.captureNavigationGrid().ok());
    map.setSize(2, 2);
    auto previous = map.captureNavigationGrid().takeValue();
    map.setCellCost(0, 0, std::numeric_limits<float>::quiet_NaN());
    REQUIRE(!map.captureNavigationGrid().ok());
    CHECK_EQ(previous.costs()[0], 1.f);
    map.setCellCost(0, 0, 1.f);
    CHECK_EQ(map.captureNavigationGrid().value().revision(), previous.revision());
}

TEST_CASE("map.navigation.changeHintObservesLayerEditsWithoutCapturing") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    auto*            layer = TileLayer::createLayer(2, 2, 1.f, 1.f);
    Pathfinder       map(layer);
    map.blockGid(3);
    const auto before = map.getNavigationChangeStamp();
    CHECK(map.getNavigationChangeStamp() == before);
    layer->setTile(0, 0, 3);
    const auto edited = map.getNavigationChangeStamp();
    CHECK(edited.configuration == before.configuration);
    CHECK(edited.layer != before.layer);
    auto captured = map.captureNavigationGrid().takeValue();
    CHECK_EQ(captured.costs()[0], 0.f);
    CHECK(map.getNavigationChangeStamp() == edited);
    map.setCellCost(1, 1, 2.f);
    CHECK(map.getNavigationChangeStamp().configuration > edited.configuration);
}
