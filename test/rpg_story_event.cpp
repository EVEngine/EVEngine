#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "rpg/GameState.h"
#include "rpg/StoryEvent.h"

TEST_CASE("rpg.storyEvent.strictCatalogueAndPersistentResume") {
    using namespace eve::rpg;
    StoryEventCatalogue::clear();
    const std::string content = R"({
      "schema":"eve.rpg.story-events",
      "version":1,
      "events":[
        {"id":"forest.arrival","repeatable":false,"steps":[
          {"kind":"dialogue","reference":"story.forest.arrival","actorId":"","x":0,"y":0,"duration":0},
          {"kind":"message","reference":"林间道路已经开放。","actorId":"","x":0,"y":0,"duration":0}
        ]},
        {"id":"inn.rest","repeatable":true,"steps":[
          {"kind":"wait","reference":"","actorId":"","x":0,"y":0,"duration":0.25}
        ]}
      ]
    })";
    REQUIRE(StoryEventCatalogue::replaceFromJsonStrict(content).ok());
    CHECK_EQ(StoryEventCatalogue::count(), 2);
    CHECK(StoryEventCatalogue::contains("forest.arrival"));

    CHECK(
        !StoryEventCatalogue::replaceFromJsonStrict(
             R"({"schema":"eve.rpg.story-events","version":1,"events":[{"id":"bad","repeatable":false,"steps":[{"kind":"wait","reference":"","actorId":"","x":0,"y":0,"duration":0}]}]})")
             .ok());
    CHECK_EQ(StoryEventCatalogue::count(), 2);

    GameState         state;
    StoryEventSession first;
    REQUIRE(first.begin("forest.arrival", &state).ok());
    CHECK(first.isActive());
    CHECK_EQ(first.getStepKind(), std::string("dialogue"));
    CHECK_EQ(first.getReference(), std::string("story.forest.arrival"));
    auto advanced = first.advance(&state);
    REQUIRE(advanced.ok());
    CHECK_EQ(advanced.value(), 1);
    StoryEventSession stale;
    REQUIRE(stale.begin("forest.arrival", &state).ok());
    REQUIRE(first.advance(&state).ok());
    CHECK(!stale.advance(&state).ok());

    state.setSelfVariable("story.event:forest.arrival", "cursor", 1.0);
    state.setSelfVariable("story.event:forest.arrival", "completed", 0.0);
    auto snapshot = state.snapshotJson();
    REQUIRE(snapshot.ok());
    GameState restored;
    REQUIRE(restored.restoreSnapshotJson(snapshot.value()).ok());
    StoryEventSession resumed;
    REQUIRE(resumed.begin("forest.arrival", &restored).ok());
    CHECK_EQ(resumed.getStepIndex(), 1);
    CHECK_EQ(resumed.getStepKind(), std::string("message"));
    CHECK_EQ(resumed.getReference(), std::string("林间道路已经开放。"));

    StoryEventCatalogue::clear();
    CHECK_EQ(resumed.getStepKind(), std::string("message"));
    auto finished = resumed.advance(&restored);
    REQUIRE(finished.ok());
    CHECK_EQ(finished.value(), 0);
    CHECK(!resumed.isActive());
    CHECK(resumed.isFinished());
    CHECK(!StoryEventSession().begin("forest.arrival", &restored).ok());

    REQUIRE(StoryEventCatalogue::replaceFromJsonStrict(content).ok());
    StoryEventSession completed;
    CHECK(!completed.begin("forest.arrival", &restored).ok());
    StoryEventSession repeatable;
    REQUIRE(repeatable.begin("inn.rest", &restored).ok());
    REQUIRE(repeatable.advance(&restored).ok());
    REQUIRE(repeatable.begin("inn.rest", &restored).ok());
    CHECK_EQ(repeatable.getStepIndex(), 0);
    StoryEventCatalogue::clear();
}
