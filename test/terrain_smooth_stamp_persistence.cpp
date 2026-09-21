#include "common/Value.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainGenerationSession.h"
#include "procgen/heightmap/TerrainSpawnPlan.h"
#include "procgen/heightmap/TerrainStamp.h"
#include "zeroerr/unittest.h"

#include <algorithm>

using namespace eve::procgen;
namespace {
Heightmap filled(int size, float value) {
    Heightmap map(size, size);
    std::fill(map.data().begin(), map.data().end(), value);
    return map;
}
TerrainStampSettings settings(TerrainStampOperation operation) {
    TerrainStampSettings s;
    s.centerX = s.centerZ = 2;
    s.width = s.depth = 4;
    s.operation       = operation;
    s.smoothWidth     = 4;
    s.edgeFade        = 2;
    return s;
}
// A single-stamp session/plan ends with its stamp settings; remove v2/v4's two floats.
std::string oldStampSnapshot(const std::string& snapshot, int version) {
    auto value = eve::Value::fromJson(snapshot);
    if (!value.ok()) return {};
    auto payload = value.value().find("payload")->asString();
    payload.resize(payload.size() - 16);
    value.value().set("payload", payload);
    value.value().set("version", version);
    auto text = value.value().toJson();
    if (!text.ok()) return {};
    return text.value();
}
}  // namespace

TEST_CASE("procgen.smoothStamp.sessionUndoReplaySnapshotAndLegacy") {
    const auto               baseline = filled(5, 10), stamp = filled(1, 10), one = filled(1, 1);
    auto                     s = settings(TerrainStampOperation::SmoothRaise);
    TerrainGenerationSession session;
    REQUIRE(session.reset(baseline).ok());
    REQUIRE(session.stamp(stamp, s, one, one).ok());
    auto output = baseline;
    REQUIRE(session.copyTerrain(output).ok());
    CHECK_EQ(output.height(2, 2), 11.f);
    CHECK_EQ(output.height(0, 2), 10.f);
    const auto expected = output.data();
    REQUIRE(session.undo().ok());
    REQUIRE(session.copyTerrain(output).ok());
    CHECK(output.data() == baseline.data());
    REQUIRE(session.redo().ok());
    REQUIRE(session.replay().ok());
    REQUIRE(session.copyTerrain(output).ok());
    CHECK(output.data() == expected);
    auto snapshot = session.snapshotJson();
    REQUIRE(snapshot.ok());
    TerrainGenerationSession restored;
    REQUIRE(restored.restoreJson(snapshot.value()).ok());
    REQUIRE(restored.copyTerrain(output).ok());
    CHECK(output.data() == expected);
    CHECK_EQ(restored.snapshotJson().value(), snapshot.value());
    // An old reader version must not silently accept the new enum.
    CHECK(!restored.restoreJson(oldStampSnapshot(snapshot.value(), 1)).ok());
    CHECK_EQ(restored.snapshotJson().value(), snapshot.value());
    auto truncated = snapshot.value();
    truncated.resize(truncated.size() / 2);
    CHECK(!restored.restoreJson(truncated).ok());
    CHECK_EQ(restored.snapshotJson().value(), snapshot.value());

    REQUIRE(session.reset(baseline).ok());
    s.operation = TerrainStampOperation::Set;
    REQUIRE(session.stamp(stamp, s, one, one).ok());
    auto legacy = oldStampSnapshot(session.snapshotJson().value(), 1);
    REQUIRE(restored.restoreJson(legacy).ok());
    REQUIRE(restored.copyTerrain(output).ok());
    CHECK(output.data() == baseline.data());
    REQUIRE(restored.undo().ok());
    REQUIRE(restored.redo().ok());
}

TEST_CASE("procgen.smoothStamp.spawnPlanSnapshotAndLegacy") {
    const auto       stamp = filled(1, 10), one = filled(1, 1);
    auto             s = settings(TerrainStampOperation::SmoothRaise);
    TerrainSpawnPlan plan;
    REQUIRE(plan.addModifierStamp("mountain", stamp, s, one, one).ok());
    auto snapshot = plan.snapshotJson();
    REQUIRE(snapshot.ok());
    TerrainSpawnPlan restored;
    REQUIRE(restored.restoreJson(snapshot.value()).ok());
    CHECK_EQ(restored.snapshotJson().value(), snapshot.value());
    CHECK(!restored.restoreJson(oldStampSnapshot(snapshot.value(), 3)).ok());
    CHECK_EQ(restored.snapshotJson().value(), snapshot.value());
    TerrainSpawnPlan old;
    s.operation = TerrainStampOperation::Raise;
    REQUIRE(old.addModifierStamp("mountain", stamp, s, one, one).ok());
    REQUIRE(restored.restoreJson(oldStampSnapshot(old.snapshotJson().value(), 3)).ok());
    const auto       migrated = restored.snapshotJson().value();
    TerrainSpawnPlan defaults;
    s.smoothWidth = 0;
    s.edgeFade    = 0;
    REQUIRE(defaults.addModifierStamp("mountain", stamp, s, one, one).ok());
    CHECK_EQ(migrated, defaults.snapshotJson().value());
}

TEST_CASE("procgen.smoothStamp.spawnPlanV0DetailMigration") {
    const auto raster = filled(2, 1);
    auto       s      = settings(TerrainStampOperation::Raise);
    s.smoothWidth     = 0;
    s.edgeFade        = 0;
    TerrainSpawnPlan      plan;
    TerrainDetailSettings detail;
    REQUIRE(plan.addDetail("detail", raster, detail, s, TerrainDetailMode::Add, 17).ok());
    auto value = eve::Value::fromJson(plan.snapshotJson().value());
    REQUIRE(value.ok());
    auto              payload       = value.value().find("payload")->asString();
    const std::size_t enabledOffset = 4 + 1 + 4 + std::string("detail").size();
    const std::size_t rasterBytes   = 8 + raster.data().size() * sizeof(float);
    const std::size_t oldStampBytes = 9 * sizeof(double) + 3 * sizeof(float) + sizeof(int);
    payload.erase((enabledOffset + 1 + rasterBytes + oldStampBytes) * 2, 16);
    payload.erase(enabledOffset * 2, 2);
    value.value().set("payload", payload);
    value.value().set("version", 0);
    auto legacy = value.value().toJson();
    REQUIRE(legacy.ok());
    TerrainSpawnPlan restored;
    REQUIRE(restored.restoreJson(legacy.value()).ok());
    CHECK_EQ(restored.snapshotJson().value(), plan.snapshotJson().value());
}
