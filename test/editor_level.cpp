#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "level_editing/LevelDocument.h"
#include "level_editing/LevelFormat.h"

using namespace eve::level_editing;

TEST_CASE("editor.level.document_and_formats") {
    LevelDocument level(4, 3, 16.f, 24.f);
    level.setOrientation("hexagonal");
    const int ground = level.addTileLayer("Ground");
    level.getTileLayer(ground)->get().setGid(2, 1, 17);
    const int actors = level.addObjectLayer("Actors");
    const int hero   = level.addObject(actors, "spawn", 32.f, 24.f);
    REQUIRE(hero >= 0);
    auto heroRef = level.object(actors, hero);
    REQUIRE(heroRef.has_value());
    heroRef->get().name               = "Player";
    heroRef->get().properties["team"] = "blue";
    level.setProperty("music", "dungeon");

    LevelFormatRegistry formats;
    CHECK_EQ(formats.getFormatCount(), 2);
    auto nativeResult = formats.encode("eve.level", level);
    REQUIRE(nativeResult.ok());
    std::string native = std::move(nativeResult).takeValue();
    CHECK_EQ(formats.detect("room.level.json", native), std::string("eve.level"));

    auto roundtripResult = formats.decode("eve.level", native);
    REQUIRE(roundtripResult.ok());
    auto roundtrip = std::move(roundtripResult).takeValue();
    CHECK_EQ(roundtrip->getOrientation(), std::string("hexagonal"));
    CHECK_EQ(roundtrip->getTileLayer(0)->get().getGid(2, 1), 17);
    CHECK_EQ(roundtrip->object(1, 0)->get().properties.at("team"), std::string("blue"));

    auto tiledResult = formats.encode("tiled.json", level);
    REQUIRE(tiledResult.ok());
    std::string tiled = std::move(tiledResult).takeValue();
    CHECK_EQ(formats.detect("room.tmj", tiled), std::string("tiled.json"));
    auto importedResult = formats.decode("tiled.json", tiled);
    REQUIRE(importedResult.ok());
    auto imported = std::move(importedResult).takeValue();
    CHECK_EQ(imported->getLayerCount(), 2);
    CHECK_EQ(imported->getTileLayer(0)->get().getGid(2, 1), 17);
}
