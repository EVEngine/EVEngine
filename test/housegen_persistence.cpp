#include "housegen/HouseComponentLibrary.h"
#include "housegen/HouseGenerator.h"
#include "housegen/HouseLayout.h"
#include "housegen/HousePersistence.h"

#include "procgen/GeneratedArtifact.h"

#include "common/Value.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <string>

using namespace eve::housegen;

namespace {

const char *kKit = R"({"components":[
 {"id":"foundation","model":"fixtures/foundation.glb","category":"foundation"},
 {"id":"floor","model":"fixtures/floor.glb","category":"floor"},
 {"id":"wall","model":"fixtures/wall.glb","category":"wall"},
 {"id":"door","model":"fixtures/door.glb","category":"door"},
 {"id":"roof","model":"fixtures/roof.glb","category":"roof"}
]})";

eve::Result<HouseLayout> makeLayout(uint32_t seed, int width, int depth) {
    HouseComponentLibrary lib;
    auto                  loaded = lib.loadFromJson(kKit);
    if (!loaded.ok()) return eve::Result<HouseLayout>::failure(loaded.status());
    HouseGenerator generator(lib);
    HouseRequest   request;
    request.seed = seed; request.width = width; request.depth = depth; request.floors = 1;
    request.footprint = "rectangle"; request.roof = "flat"; request.entrance = "north";
    HouseLayout layout;
    auto        generated = generator.generate(request, layout);
    if (!generated.ok()) return eve::Result<HouseLayout>::failure(generated.status());
    return eve::Result<HouseLayout>::success(std::move(layout),
                                             eve::Status::success(eve::StatusCode::Applied));
}

}  // namespace

TEST_CASE("housegen.persistence.buildKeyIsDeterministicPerInput") {
    HouseRequest a, b, c;
    a.seed = 42; a.width = 5; a.depth = 4; a.floors = 1;
    b.seed = 42; b.width = 5; b.depth = 4; b.floors = 1;
    c.seed = 43; c.width = 5; c.depth = 4; c.floors = 1;
    CHECK_EQ(houseRequestBuildKeyText(a), houseRequestBuildKeyText(b));
    CHECK_NE(houseRequestBuildKeyText(a), houseRequestBuildKeyText(c));
    // A single scalar change in any input must change the key.
    HouseRequest d = a;
    d.entrance = "east";
    CHECK_NE(houseRequestBuildKeyText(a), houseRequestBuildKeyText(d));
}

TEST_CASE("housegen.persistence.artifactRoundTripAndSnapshot") {
    auto layoutResult = makeLayout(20260820, 6, 5);
    REQUIRE(layoutResult.ok());
    auto layout = std::move(layoutResult).takeValue();

    eve::procgen::ArtifactStore store;
    auto published = publishHouseLayout(store, layout);
    REQUIRE(published.ok());
    const auto id = std::move(published).takeValue();
    REQUIRE(!id.isNil());

    const auto *artifact = store.find(id);
    REQUIRE(artifact != nullptr);
    auto restored = restoreHouseLayout(*artifact);
    REQUIRE(restored.ok());
    CHECK_EQ(std::move(restored).takeValue().toJson(), layout.toJson());

    // The store snapshot/restore round-trips the same layout through a fresh store.
    auto snapshot = store.snapshotState();
    REQUIRE(snapshot.ok());
    eve::procgen::ArtifactStore fresh;
    auto                        restoredState = fresh.restoreState(std::move(snapshot).takeValue());
    REQUIRE(restoredState.ok());
    const auto *again = fresh.find(id);
    REQUIRE(again != nullptr);
    auto restoredAgain = restoreHouseLayout(*again);
    REQUIRE(restoredAgain.ok());
    CHECK_EQ(std::move(restoredAgain).takeValue().toJson(), layout.toJson());
    CHECK_EQ(fresh.size(), 1u);
}

TEST_CASE("housegen.persistence.duplicatePublishConflicts") {
    auto layoutResult = makeLayout(99, 5, 5);
    REQUIRE(layoutResult.ok());
    auto layout = std::move(layoutResult).takeValue();
    eve::procgen::ArtifactStore store;
    auto first = publishHouseLayout(store, layout);
    REQUIRE(first.ok());
    // Identical input yields the same deterministic identity, so re-publishing conflicts.
    auto second = publishHouseLayout(store, layout);
    CHECK(!second.ok());
    CHECK(second.status().hasDiagnostics());
}