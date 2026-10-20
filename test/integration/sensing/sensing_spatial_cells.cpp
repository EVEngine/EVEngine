#include "sensing/Sensing.h"
#include "zeroerr/unittest.h"

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

using namespace eve::sensing;

namespace {
void ensure(bool condition, const char* reason) {
    if (!condition) throw std::runtime_error(reason);
}
}  // namespace

TEST_CASE("sensing.spatialCellCandidatesMatchColdQueries") {
    SensingWorld cached;
    SensingWorld cold;
    ensure(cached.setSpatialIndexEnabled(true, 8.f).ok(), "enable cached index");
    ensure(cold.setSpatialIndexEnabled(true, 8.f).ok(), "enable cold index");

    const auto upsert = [&](const char* id, float x, float y, const char* faction, const char* tags) {
        ensure(cached.upsert(id, x, y, faction, tags, "").ok(), "cached upsert");
        ensure(cold.upsert(id, x, y, faction, tags, "").ok(), "cold upsert");
    };
    upsert("west", -7.f, -1.f, "red", "combat-target");
    upsert("edge", 0.f, 0.f, "blue", "combat-target");
    upsert("east", 7.f, 1.f, "red", "combat-target");
    upsert("corner", 8.f, 8.f, "blue", "combat-target");
    upsert("other", 30.f, 30.f, "red", "prop");

    const auto compare = [&](float x, float y, float radius, float warmX) {
        ensure(cold.setSpatialIndexEnabled(true, 8.f).ok(), "rebuild cold index");
        QuerySpec spec;
        spec.shape         = QueryCircle{x, y, radius};
        spec.requiredTags  = {"combat-target"};
        spec.maxRange      = radius;
        QuerySpec warmSpec = spec;
        warmSpec.shape     = QueryCircle{warmX, y, radius};
        ensure(cached.query(QueryOrigin{warmX, y, std::nullopt}, warmSpec).ok(), "warm cell cache");
        auto reused  = cached.query(QueryOrigin{x, y, std::nullopt}, spec);
        auto rebuilt = cold.query(QueryOrigin{x, y, std::nullopt}, spec);
        ensure(reused.ok(), "cached query");
        ensure(rebuilt.ok(), "cold query");
        ensure(cached.debugLastQueryJson() == cold.debugLastQueryJson(), "last-query diagnostics differ");
        ensure(reused.value().size() == rebuilt.value().size(), "candidate count differs");
        for (std::size_t i = 0; i < reused.value().size(); ++i)
            ensure(reused.value().ranked()[i].id == rebuilt.value().ranked()[i].id, "ranked candidate differs");
    };

    compare(-1.f, -1.f, 8.f, -1.f);
    compare(-0.5f, -1.f, 8.f, -1.f);  // Same cell footprint, different exact circle.
    compare(-1.f, -1.f, 8.f, -1.f);   // Cache hit must preserve scanned/debug facts.
    upsert("west", -20.f, -1.f, "red", "combat-target");
    compare(-1.f, -1.f, 8.f, -1.f);  // A position change invalidates the cell cache.
    upsert("edge", 0.f, 0.f, "blue", "prop");
    compare(-1.f, -1.f, 8.f, -1.f);  // Metadata-only upsert is visible through stable ids.
    upsert("edge", 1.f, 0.f, "blue", "prop");
    ensure(cached.subjects().at("edge").x == 1.f, "unchanged metadata must still update position");
    compare(-1.f, -1.f, 8.f, -1.f);
    ensure(cached.upsert("edge", 1.f, 0.f, "blue", "prop,prop,,", "").ok(), "duplicate CSV upsert");
    ensure(cached.subjects().at("edge").tags.size() == 1, "duplicate CSV must preserve tag set");
    ensure(cached.setZones("edge", "zone:test").ok(), "set zone");
    ensure(cached.upsert("edge", 1.f, 0.f, "blue", "prop", "").ok(), "upsert with zone");
    ensure(cached.subjects().at("edge").zones.empty(), "upsert must retain original zone reset semantics");
    ensure(cached.remove("east").ok(), "cached removal");
    ensure(cold.remove("east").ok(), "cold removal");
    compare(-1.f, -1.f, 8.f, -1.f);
    const auto snapshot = cached.snapshotJson();
    ensure(cached.restoreJson(snapshot).ok(), "restore cached world");
    compare(-1.f, -1.f, 8.f, -1.f);  // Restore rebuilds the spatial index and invalidates cached pointers.

    SensingWorld movedCached;
    SensingWorld movedCold;
    ensure(movedCached.setSpatialIndexEnabled(true, 8.f).ok(), "enable moved cached index");
    ensure(movedCold.setSpatialIndexEnabled(true, 8.f).ok(), "enable moved cold index");
    ensure(movedCached.upsert("replacement", -1.f, -1.f, "red", "combat-target", "").ok(),
           "upsert moved cached subject");
    ensure(movedCold.upsert("replacement", -1.f, -1.f, "red", "combat-target", "").ok(), "upsert moved cold subject");
    cached = std::move(movedCached);
    cold   = std::move(movedCold);
    compare(-1.f, -1.f, 8.f, -1.f);  // A moved-in index must replace the prior owner's cache.

    std::optional<SensingWorld> reusedAddress;
    reusedAddress.emplace();
    ensure(reusedAddress->setSpatialIndexEnabled(true, 8.f).ok(), "enable first reused index");
    ensure(reusedAddress->upsert("old", 0.f, 0.f, "red", "combat-target", "").ok(), "upsert old subject");
    QuerySpec reuseSpec;
    reuseSpec.shape    = QueryCircle{0.f, 0.f, 4.f};
    reuseSpec.maxRange = 4.f;
    ensure(reusedAddress->query(QueryOrigin{0.f, 0.f, std::nullopt}, reuseSpec).ok(), "warm reused cache");
    reusedAddress.reset();
    reusedAddress.emplace();
    ensure(reusedAddress->setSpatialIndexEnabled(true, 8.f).ok(), "enable second reused index");
    ensure(reusedAddress->upsert("new", 0.f, 0.f, "red", "combat-target", "").ok(), "upsert new subject");
    auto fresh = reusedAddress->query(QueryOrigin{0.f, 0.f, std::nullopt}, reuseSpec);
    ensure(fresh.ok() && fresh.value().size() == 1 && fresh.value().ranked()[0].id == "new",
           "reused world address must not expose stale cached subjects");
}
