#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Json.h"
#include "map/level/editing/Brush.h"
#include "map/level/editing/EditorHistory.h"
#include "map/level/editing/LevelDocument.h"
#include "map/level/editing/LevelFormat.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>

using namespace eve::level_editing;

TEST_CASE("editor.tilemap.formats.preserveAssetsFlagsAndObjectIdentity") {
    LevelFormatRegistry formats;
    auto                decoded = formats.decode("tiled.json", R"({
      "type":"map","width":1,"height":1,"tilewidth":16,"tileheight":16,
      "tilesets":[{"firstgid":1,"source":"tiles/ground.tsj"}],
      "staggeraxis":"x","staggerindex":"even","hexsidelength":8,
      "layers":[{"id":7,"type":"tilelayer","data":[2147483649]},
                {"id":9,"type":"objectgroup","objects":[
                  {"id":42,"name":"gate","class":"door","x":1,"y":2,
                   "polygon":[{"x":0,"y":0},{"x":4,"y":0},{"x":4,"y":4}]}]}]
    })");
    REQUIRE(decoded.ok());
    auto encoded = formats.encode("tiled.json", *decoded.value());
    REQUIRE(encoded.ok());
    auto json = eve::json::Document::parse(encoded.value());
    REQUIRE(json.valid());
    auto root = json.root();
    CHECK_EQ(root.get("tilesets").at(0).getString("source"), std::string("tiles/ground.tsj"));
    CHECK_EQ(root.getString("staggeraxis"), std::string("x"));
    CHECK_EQ(root.get("layers").at(0).getInt("id"), 7);
    CHECK_EQ(root.get("layers").at(0).get("data").at(0).asDouble(), 2147483649.0);
    auto object = root.get("layers").at(1).get("objects").at(0);
    CHECK_EQ(object.getInt("id"), 42);
    CHECK_EQ(object.get("polygon").size(), size_t(3));
    CHECK_EQ(object.getString("class"), std::string("door"));
}

TEST_CASE("editor.tilemap.formats.rejectUnsupportedWithoutBlankMaps") {
    LevelFormatRegistry formats;
    for (const char* layer : {R"({"type":"group","layers":[{"type":"tilelayer","data":[1]}]})",
                              R"({"type":"imagelayer","image":"background.png"})",
                              R"({"type":"tilelayer","encoding":"base64","data":"AQAAAA=="})",
                              R"({"type":"tilelayer","chunks":[{"x":0,"y":0,"width":1,"height":1,"data":[1]}]})"}) {
        auto decoded = formats.decode("tiled.json",
                                      std::string(R"({"type":"map","width":1,"height":1,"layers":[)") + layer + "]}");
        CHECK(!decoded.ok());
    }
}

TEST_CASE("editor.tilemap.formats.rejectMalformedCellsAndFutureSchema") {
    LevelFormatRegistry formats;
    for (const char* data : {"[]", "[1,2]", "[-1]", "[4294967296]", "[1.5]", "[\"1\"]"}) {
        auto decoded = formats.decode(
            "tiled.json",
            std::string(R"({"type":"map","width":1,"height":1,"layers":[{"type":"tilelayer","data":)") + data + "}]}");
        CHECK(!decoded.ok());
    }
    auto future =
        formats.decode("eve.level", R"({"format":"eve.level","version":999,"width":1,"height":1,"layers":[]})");
    CHECK(!future.ok());
}

TEST_CASE("editor.tilemap.formats.nativePreservesLocksAndStringIds") {
    LevelFormatRegistry formats;
    LevelDocument       level(1, 1);
    int                 li        = level.addObjectLayer("objects");
    level.layer(li)->get().locked = true;
    int               oi          = level.addObject(li, "spawn", 0, 0);
    const std::string id          = level.object(li, oi)->get().id;
    auto              encoded     = formats.encode("eve.level", level);
    REQUIRE(encoded.ok());
    auto decoded = formats.decode("eve.level", encoded.value());
    REQUIRE(decoded.ok());
    CHECK(decoded.value()->layer(0)->get().locked);
    CHECK_EQ(decoded.value()->object(0, 0)->get().id, id);
}

TEST_CASE("editor.tilemap.formats.typedPropertiesSurviveNativeConversion") {
    LevelFormatRegistry formats;
    auto                decoded = formats.decode("tiled.json", R"({"type":"map","width":1,"height":1,
      "tilesets":[{"firstgid":1,"tiles":[{"id":0,"animation":[{"tileid":0,"duration":100}]}]}],
      "properties":[{"name":"speed","type":"float","value":1.5},
                    {"name":"enabled","type":"bool","value":true},
                    {"name":"label","type":"string","value":"old"}],
      "layers":[{"id":10,"type":"tilelayer","data":[1]}]})");
    REQUIRE(decoded.ok());
    decoded.value()->setProperty("label", "new");
    auto native = formats.encode("eve.level", *decoded.value());
    REQUIRE(native.ok());
    auto restored = formats.decode("eve.level", native.value());
    REQUIRE(restored.ok());
    auto tiled = formats.encode("tiled.json", *restored.value());
    REQUIRE(tiled.ok());
    auto json = eve::json::Document::parse(tiled.value());
    REQUIRE(json.valid());
    auto properties = json.root().get("properties");
    REQUIRE(properties.size() == 3);
    CHECK_EQ(properties.at(0).getString("type"), std::string("float"));
    CHECK_EQ(properties.at(0).get("value").asDouble(), 1.5);
    CHECK(properties.at(1).get("value").asBool());
    CHECK_EQ(properties.at(2).getString("value"), std::string("new"));
    CHECK_EQ(json.root().get("tilesets").at(0).get("tiles").at(0).get("animation").at(0).getInt("duration"), 100);
    auto again = formats.encode("tiled.json", *restored.value());
    REQUIRE(again.ok());
    CHECK_EQ(again.value(), tiled.value());
}

TEST_CASE("editor.tilemap.formats.failedSavePreservesPreviousFile") {
    const auto directory =
        std::filesystem::temp_directory_path() /
        ("eve-tilemap-save-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    REQUIRE(std::filesystem::create_directory(directory));
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    } cleanup{directory};
    const auto          destination = directory / "map.tmj";
    LevelFormatRegistry formats;
    LevelDocument       level(1, 1);
    int                 layer = level.addTileLayer("ground");
    level.getTileLayer(layer)->get().setGid(0, 0, 7);
    REQUIRE(formats.save(destination.string(), level, "tiled.json").ok());
    level.layer(layer)->get().opacity = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(!formats.save(destination.string(), level, "tiled.json").ok());
    auto preserved = formats.load(destination.string());
    REQUIRE(preserved.ok());
    CHECK_EQ(preserved.value()->getTileLayer(0)->get().getGid(0, 0), 7);
    level.layer(layer)->get().opacity = 1;
    level.getTileLayer(layer)->get().setGid(0, 0, 9);
    REQUIRE(formats.save(destination.string(), level, "tiled.json").ok());
    auto replaced = formats.load(destination.string());
    REQUIRE(replaced.ok());
    CHECK_EQ(replaced.value()->getTileLayer(0)->get().getGid(0, 0), 9);
    const auto blocked = directory / "blocked";
    REQUIRE(std::filesystem::create_directory(blocked));
    REQUIRE(!formats.save(blocked.string(), level, "tiled.json").ok());
    CHECK(std::filesystem::is_directory(blocked));
    CHECK_EQ(std::distance(std::filesystem::directory_iterator(directory), std::filesystem::directory_iterator{}), 2);
}

TEST_CASE("editor.tilemap.formats.newIdsDoNotCollideAfterRestore") {
    LevelFormatRegistry formats;
    auto decoded = formats.decode("eve.level", R"({"format":"eve.level","version":1,"width":1,"height":1,
      "layers":[{"id":"layer-2","type":"objectgroup","objects":[{"id":"object-4","type":"spawn"}]}]})");
    REQUIRE(decoded.ok());
    const int added = decoded.value()->addObjectLayer("new");
    CHECK_NE(decoded.value()->layer(added)->get().id, std::string("layer-2"));
    const int object = decoded.value()->addObject(0, "spawn", 1, 1);
    CHECK_NE(decoded.value()->object(0, object)->get().id, std::string("object-4"));
}

TEST_CASE("editor.tilemap.formats.paintUndoRedoSaveJourney") {
    LevelFormatRegistry formats;
    auto                decoded = formats.decode("tiled.json", R"({"type":"map","width":1,"height":1,
      "tilesets":[{"firstgid":1,"source":"ground.tsj"}],
      "layers":[{"id":1,"type":"tilelayer","data":[2147483649]}]})");
    REQUIRE(decoded.ok());
    auto& buffer = decoded.value()->getTileLayer(0)->get();
    Brush brush;
    brush.setSize(1);
    brush.setTile(2);
    REQUIRE(brush.paintAt(&buffer, 0, 0) == 1);
    EditorHistory history;
    history.beginGroup("paint");
    for (int i = 0; i < brush.getChangeCount(); ++i)
        history.recordTile(brush.getChangeX(i), brush.getChangeY(i), brush.getChangeOldGid(i),
                           brush.getChangeNewGid(i));
    history.endGroup();
    REQUIRE(history.undoAction().ok());
    REQUIRE(history.applyLastToBufferChecked(buffer).ok());
    CHECK_EQ(uint32_t(buffer.getGid(0, 0)), uint32_t{0x80000001});
    auto undone = formats.encode("tiled.json", *decoded.value());
    REQUIRE(undone.ok());
    auto reopened = formats.decode("tiled.json", undone.value());
    REQUIRE(reopened.ok());
    CHECK_EQ(uint32_t(reopened.value()->getTileLayer(0)->get().getGid(0, 0)), uint32_t{0x80000001});
    REQUIRE(history.redoAction().ok());
    REQUIRE(history.applyLastToBufferChecked(buffer).ok());
    CHECK_EQ(buffer.getGid(0, 0), 2);
}

TEST_CASE("editor.tilemap.formats.migrateNativeSignedGid") {
    LevelFormatRegistry formats;
    auto decoded = formats.decode("eve.level", R"({"format":"eve.level","version":1,"width":1,"height":1,
      "layers":[{"id":"layer-1","type":"tilelayer","data":[-2147483647]}]})");
    REQUIRE(decoded.ok());
    CHECK_EQ(uint32_t(decoded.value()->getTileLayer(0)->get().getGid(0, 0)), uint32_t{0x80000001});
    auto encoded = formats.encode("eve.level", *decoded.value());
    REQUIRE(encoded.ok());
    auto json = eve::json::Document::parse(encoded.value());
    REQUIRE(json.valid());
    CHECK_EQ(json.root().get("layers").at(0).get("data").at(0).asDouble(), 2147483649.0);
}
