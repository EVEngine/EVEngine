#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "rpg/Quest.h"
#include "rpg/Tracker.h"
#include "rpg_tracker.pb.h"

TEST_CASE("rpg.tracker.protobufRoundTripAndAtomicRejection") {
    using namespace eve::rpg;
    QuestRegistry::clear();
    REQUIRE_EQ(QuestRegistry::loadFromJson(R"([
      {"id":"quest.hunt","startPolicy":"auto","completePolicy":"claim",
       "objectives":[{"id":"kill","topic":"kill","target":"slime","count":3}]}
    ])"),
               1);
    Tracker source;
    source.notify("kill", "slime", 2);
    auto encoded = source.snapshotBinary();
    REQUIRE(encoded.ok());

    Tracker restored;
    restored.notify("kill", "slime", 1);
    REQUIRE(restored.restoreSnapshotBinary(encoded.value()).ok());
    CHECK_EQ(restored.getObjectiveCurrent("quest.hunt", 0), 2);

    std::string withUnknown = encoded.value();
    withUnknown.append("\xa0\x06\x01", 3);  // field 100, varint 1
    REQUIRE(restored.restoreSnapshotBinary(withUnknown).ok());
    CHECK_EQ(restored.getObjectiveCurrent("quest.hunt", 0), 2);

    auto rejected = restored.restoreSnapshotBinary(std::string("\xff", 1));
    CHECK(!rejected.ok());
    CHECK_EQ(restored.getObjectiveCurrent("quest.hunt", 0), 2);

    proto::TrackerSave future;
    REQUIRE(future.ParseFromString(encoded.value()));
    future.set_version(2);
    std::string futureBytes;
    REQUIRE(future.SerializeToString(&futureBytes));
    CHECK(!restored.restoreSnapshotBinary(futureBytes).ok());
    CHECK_EQ(restored.getObjectiveCurrent("quest.hunt", 0), 2);

    future.set_version(1);
    future.mutable_entries(0)->mutable_objectives(0)->set_current(4);
    std::string invalidProgress;
    REQUIRE(future.SerializeToString(&invalidProgress));
    CHECK(!restored.restoreSnapshotBinary(invalidProgress).ok());
    CHECK_EQ(restored.getObjectiveCurrent("quest.hunt", 0), 2);

    auto json = source.snapshotJson();
    REQUIRE(json.ok());
    REQUIRE(restored.restoreSnapshotJson(json.value()).ok());
    CHECK_EQ(restored.getObjectiveCurrent("quest.hunt", 0), 2);
    QuestRegistry::clear();
}
