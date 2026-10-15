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
