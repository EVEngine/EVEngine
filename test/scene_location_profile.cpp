#include "scene/LocationProfile.h"
#include "scene/LocationProfileBindings.h"

#include "common/SquirrelBinding.h"
#include <simplesquirrel/simplesquirrel.hpp>

#include <limits>
#include <zeroerr/unittest.h>

namespace {
eve::scene::LocationPose pose(float x) {
    eve::scene::LocationPose value;
    value.x = x;
    return value;
}

eve::scene::LocationBookmark bookmark(std::string name, float x) {
    eve::scene::LocationBookmark value;
    value.name = std::move(name);
    value.camera = pose(x);
    value.player = pose(x + 10);
    value.controller = "FlyingCamera";
    value.scene = "World";
    return value;
}
}  // namespace

TEST_CASE("scene.locationProfile.oneShotLocationAndFailureAtomicity") {
    eve::scene::LocationProfile profile;
    REQUIRE(profile.saveLocation(pose(2), pose(12)).ok());
    CHECK(profile.hasSavedLocation());
    auto loaded = profile.loadLocation();
    REQUIRE(loaded.ok());
    CHECK(loaded.value().camera.x == 2);
    CHECK(loaded.value().player->x == 12);
    CHECK(!profile.hasSavedLocation());
    CHECK(!profile.loadLocation().ok());

    auto invalid = pose(3);
    invalid.qw = std::numeric_limits<float>::quiet_NaN();
    CHECK(!profile.saveLocation(invalid).ok());
    CHECK(!profile.hasSavedLocation());
}

TEST_CASE("scene.locationProfile.bookmarksMatchPcgSelectionRules") {
    eve::scene::LocationProfile profile;
    REQUIRE(profile.addBookmark(bookmark("A", 1)).ok());
    REQUIRE(profile.addBookmark(bookmark("B", 2)).ok());
    REQUIRE(profile.addBookmark(bookmark("C", 3)).ok());
    CHECK(!profile.addBookmark(bookmark("B", 9)).ok());
    CHECK(profile.getBookmarkCount() == 3);
    CHECK(profile.previousBookmark(0).value() == 2);
    CHECK(profile.nextBookmark(2).value() == 0);

    auto snapshot = profile.loadBookmark(1);
    REQUIRE(snapshot.ok());
    snapshot.value().camera.x = 22;
    REQUIRE(profile.overrideBookmark(1, snapshot.value(), "FirstPerson", "Second").ok());
    CHECK(profile.bookmarkAt(1)->name == "B");
    CHECK(profile.bookmarkAt(1)->camera.x == 22);
    CHECK(profile.bookmarkAt(1)->controller == "FirstPerson");
    CHECK(profile.getBookmarkName(1).value() == "B");
    CHECK(profile.getBookmarkController(1).value() == "FirstPerson");
    CHECK(profile.getBookmarkScene(1).value() == "Second");
    CHECK(!profile.getBookmarkName(3).ok());

    auto selected = profile.removeBookmark(2, 2);
    REQUIRE(selected.ok());
    CHECK(selected.value() == 1);
    CHECK(profile.getBookmarkCount() == 2);
    selected = profile.removeBookmark(0, 1);
    REQUIRE(selected.ok());
    CHECK(selected.value() == 0);
    profile.clearBookmarks();
    CHECK(profile.getBookmarkCount() == 0);
    CHECK(!profile.nextBookmark(0).ok());
}

TEST_CASE("scene.locationProfile.squirrelContract") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    auto table = vm.addTable("eve");
    eve::script::exposeResultBindings(table);
    eve::scene::exposeLocationProfileBindings(table);
    vm.run(vm.compileSource(R"(
        local profile = eve.LocationProfile();
        local camera = eve.LocationPose(); camera.x = 4.5;
        local player = eve.LocationPose(); player.x = 8.0;
        assert(profile.saveLocationWithPlayer(camera, player).ok);
        local saved = profile.loadLocation();
        assert(saved.ok && saved.value.hasPlayer && saved.value.camera.x == 4.5);
        local mark = eve.LocationBookmark(); mark.name = "Vista"; mark.controller = "Fly";
        mark.scene = "World"; mark.setCamera(camera); mark.setPlayer(player);
        assert(profile.addBookmark(mark).value == 0);
        assert(profile.getBookmarkName(0).value == "Vista");
        camera.x = 9.0; mark.setCamera(camera); mark.controller = "FirstPerson";
        assert(profile.overrideBookmark(0, mark).ok);
        assert(profile.loadBookmark(0).value.camera.x == 9);
        assert(profile.getBookmarkController(0).value == "FirstPerson");
        assert(profile.removeBookmark(0, 0).value == -1);
    )"));
}

TEST_CASE("scene.locationProfile.versionedJsonRoundTripAndAtomicRestore") {
    eve::scene::LocationProfile source;
    REQUIRE(source.saveLocation(pose(5), pose(15)).ok());
    REQUIRE(source.addBookmark(bookmark("Summit", 7)).ok());
    auto json = source.serializeJson();
    REQUIRE(json.ok());
    CHECK(json.value().find("eve.scene.location-profile") != std::string::npos);

    eve::scene::LocationProfile restored;
    REQUIRE(restored.restoreJson(json.value()).ok());
    CHECK(restored.hasSavedLocation());
    CHECK(restored.getBookmarkCount() == 1);
    CHECK(restored.getBookmarkName(0).value() == "Summit");
    CHECK(restored.loadBookmark(0).value().player->x == 17);

    CHECK(!restored.restoreJson(R"({"schemaId":"eve.scene.location-profile","schemaVersion":1,
        "saved":null,"bookmarks":[],"future":true})").ok());
    CHECK(restored.getBookmarkCount() == 1);
    CHECK(!restored.restoreJson(R"({"schemaId":"eve.scene.location-profile","schemaVersion":2,
        "saved":null,"bookmarks":[]})").ok());
    CHECK(restored.getBookmarkName(0).value() == "Summit");
}
