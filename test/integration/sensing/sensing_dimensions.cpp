// clang-format off
/* eve-test-policy:
{"schema":"evengine.test-policy","version":1,"module":"sensing"}
*/
// clang-format on
#include <limits>
#include <stdexcept>
#include "npc_ai/SensingPerception.h"
#include "sensing/SensingQueryCodec.h"
#include "sensing/TargetingPipeline.h"
#include "zeroerr/unittest.h"

namespace {
using namespace eve;
using namespace eve::sensing;
class HeightSight final : public ILineOfSightQuery {
public:
    mutable int               calls = 0;
    bool                      fail  = false;
    Result<LineOfSightResult> query(const TargetLocation& from, const TargetLocation& to) const override {
        ++calls;
        const auto& a = std::get<WorldPoint>(from);
        const auto& b = std::get<WorldPoint>(to);
        if (fail) return Result<LineOfSightResult>::failure(Diagnostic::error(DiagnosticCode::Failed, "injected"));
        if (a.space() != CoordinateSpace::World3D || b.space() != CoordinateSpace::World3D)
            return Result<LineOfSightResult>::failure(Diagnostic::error(DiagnosticCode::Unsupported, "need XYZ"));
        return Result<LineOfSightResult>::success({b.y() < 5, std::nullopt});
    }
};
void populate(SensingWorld& world) {
    REQUIRE(world.upsertPosition("near", 1, 0, 0, "red", "enemy", "").ok());
    REQUIRE(world.upsertPosition("above", 0, 10, 0, "red", "enemy", "").ok());
    REQUIRE(world.upsertPosition("below", 0, -10, 0, "red", "enemy", "").ok());
    REQUIRE(world.upsertPosition("far", 30, 0, 0, "red", "enemy", "").ok());
}
}  // namespace
TEST_CASE("sensing.dimensions.indexChoicePreservesSphereAndPlanarQueries") {
    for (auto kind : {SpatialIndexKind::None, SpatialIndexKind::XY, SpatialIndexKind::XZ, SpatialIndexKind::YZ,
                      SpatialIndexKind::XYZ}) {
        SensingWorld world;
        populate(world);
        REQUIRE(world.rebuildIndex(kind, 4).ok());
        QueryOrigin origin{0, 0, std::nullopt, 0, CoordinateSpace::World3D};
        QuerySpec   spec;
        spec.axes     = QueryAxes::XYZ;
        spec.maxRange = 12;
        auto all      = world.query(origin, spec);
        REQUIRE(all.ok());
        CHECK_EQ(all.value().size(), 3u);
        spec.maxRange = 3;
        auto sphere   = world.query(origin, spec);
        REQUIRE(sphere.ok());
        CHECK_EQ(sphere.value().size(), 1u);
        spec.axes  = QueryAxes::XZ;
        spec.shape = QueryCircle{0, 0, 3};
        auto plane = world.query(origin, spec);
        REQUIRE(plane.ok());
        CHECK_EQ(plane.value().size(), 3u);
        REQUIRE(world.upsertPosition("above", 20, 10, 0, "red", "enemy", "").ok());
        REQUIRE(world.remove("below").ok());
        auto moved = world.query(origin, spec);
        REQUIRE(moved.ok());
        CHECK_EQ(moved.value().size(), 1u);
    }
}
TEST_CASE("sensing.dimensions.projectedCandidatesUseRealHeightBeforeLosLimits") {
    SensingWorld world;
    populate(world);
    REQUIRE(world.rebuildIndex(SpatialIndexKind::XZ, 4).ok());
    QueryOrigin origin{0, 0, std::nullopt, 0, CoordinateSpace::World3D};
    QuerySpec   spec;
    spec.axes               = QueryAxes::XZ;
    spec.maxRange           = 3;
    spec.maxCount           = 1;
    spec.requireLineOfSight = true;
    auto absent             = world.query(origin, spec);
    CHECK(!absent.ok());
    CHECK_EQ(absent.code(), StatusCode::Unsupported);
    HeightSight sight;
    auto        visible = world.query(origin, spec, std::cref(sight));
    REQUIRE(visible.ok());
    CHECK_EQ(visible.value().ranked()[0].id, std::string("below"));
    CHECK_EQ(sight.calls, 3);
    auto saved  = world.debugLastQueryJson();
    sight.fail  = true;
    auto failed = world.query(origin, spec, std::cref(sight));
    CHECK(!failed.ok());
    CHECK_EQ(world.debugLastQueryJson(), saved);
    auto facts   = perceptionFactsFrom(visible.value());
    auto memory  = eve::npc_ai::perceptionMemoryFrom(facts[0], 1, 10);
    auto payload = Value::fromJson(memory.payloadJson);
    CHECK(payload.ok());
}
TEST_CASE("sensing.dimensions.snapshotMigratesAndFailedRestorePreservesIndex") {
    SensingWorld world;
    REQUIRE(world.upsertPosition("up", 1, 2, 17, "red", "enemy", "").ok());
    REQUIRE(world.rebuildIndex(SpatialIndexKind::XYZ, 4).ok());
    auto snapshot = world.snapshotJson();
    REQUIRE(world.restoreJson(snapshot).ok());
    CHECK_EQ(world.subjects().at("up").z, 17.f);
    CHECK(world.indexKind() == SpatialIndexKind::XYZ);
    CHECK(!world.restoreJson(R"({"schema":"eve.sensing.world","version":3,"subjects":[]})").ok());
    CHECK_EQ(world.snapshotJson(), snapshot);
    CHECK(!world.rebuildIndex(SpatialIndexKind::XYZ, std::numeric_limits<float>::min()).ok());
    CHECK_EQ(world.snapshotJson(), snapshot);
    REQUIRE(
        world.restoreJson(R"({"schema":"eve.sensing.world","version":1,"subjects":[{"id":"old","x":2,"y":3}]})").ok());
    CHECK_EQ(world.subjects().at("old").z, 0.f);
    SensingWorld moved = std::move(world);
    QuerySpec    spec;
    spec.axes     = QueryAxes::XYZ;
    spec.maxRange = 10;
    auto query    = moved.query({0, 0, std::nullopt, 0, CoordinateSpace::World3D}, spec);
    REQUIRE(query.ok());
    CHECK_EQ(query.value().size(), 1u);
}
TEST_CASE("sensing.dimensions.codecAndEnvironmentKeepWorld3D") {
    auto query = decodeSensingQuery(
        R"({"schema":"eve.sensing.query","version":1,"origin":{"space":"world3d","x":0,"y":0,"z":0},"axes":"xz","maxRange":4,"lineOfSight":true})");
    REQUIRE(query.ok());
    CHECK(query.value().spec.axes == QueryAxes::XZ);
    CHECK(!decodeSensingQuery(R"({"schema":"eve.sensing.query","version":1,"origin":{"space":"world3d","x":0,"y":0}})")
               .ok());
    auto d = decodeEnvironmentDefinition(
        R"({"schema":"eve.sensing.environmentDefinition","version":1,"id":"height","space":"world3d","contexts":["self"],"options":[{"id":"input","tests":[{"id":"distance","context":"self","purpose":"score"}]}]})");
    REQUIRE(d.ok());
    auto plan = EnvironmentQueryPlan::compile(d.value());
    REQUIRE(plan.ok());
    SensingWorld world;
    populate(world);
    QuerySpec spec;
    spec.axes     = QueryAxes::XYZ;
    spec.maxRange = 12;
    auto input    = world.environmentInput({0, 0, std::nullopt, 0, CoordinateSpace::World3D}, spec);
    REQUIRE(input.ok());
    auto point = WorldPoint::world3D(0, 0, 0);
    REQUIRE(point.ok());
    input.value().contexts = {{"self", {TargetLocation{point.value()}}}};
    auto result            = world.executeEnvironmentQuery(plan.value(), std::move(input).takeValue(), {});
    REQUIRE(result.ok());
    CHECK_EQ(result.value().ranked.size(), 3u);
    CHECK(std::get<WorldPoint>(result.value().ranked[1].item.location).space() == CoordinateSpace::World3D);
}

#include "common/ScriptProvider.h"
#include "simplesquirrel/simplesquirrel.hpp"
TEST_CASE("sensing.dimensions.scriptQueriesAndTaskProvidersCompose") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local world=eve.Sensing().newWorld().value;
        if (!world.upsertPosition("up",0.0,10.0,0.0,"red","enemy","").ok) throw "upsert";
        if (!world.rebuildIndex("xz",4.0).ok) throw "index";
        local query=world.query(@"{""schema"":""eve.sensing.query"",""version"":1,""origin"":{""space"":""world3d"",""x"":0,""y"":0,""z"":0},""axes"":""xz"",""maxRange"":2}");
        if (!query.ok || query.value.ranked.len()!=1 || query.value.ranked[0].location.y!=10) throw "query";
        local plan=eve.compileEnvironmentQuery(@"{""schema"":""eve.sensing.environmentDefinition"",""version"":1,""id"":""height"",""space"":""world3d"",""contexts"":[""self""],""options"":[{""id"":""input"",""tests"":[{""id"":""distance"",""context"":""self"",""purpose"":""score""}]}]}");
        if(!plan.ok) throw "compile";
        local env=plan.value.run(@"{""schema"":""eve.sensing.environmentRequest"",""version"":1,""contexts"":[{""name"":""self"",""locations"":[{""space"":""world3d"",""x"":0,""y"":0,""z"":0}]}],""input"":[{""id"":1,""subjectKey"":""up"",""location"":{""space"":""world3d"",""x"":0,""y"":10,""z"":0}}]}");
        if(!env.ok || env.value.ranked.len()!=1) throw "environment";
        local ai=eve.NpcAi().newWorld(32,8).value;
        local stopped=0;
        local provider=class {
            function start(context) {
                if (ai.tick(1,0.01,8,8).ok) throw "reentrancy accepted";
                return {state="running",memoryJson="{}"};
            }
            function tick(context) { return {state="running",memoryJson="{}"}; }
            function stop(context) { stopped++; }
        }();
        if(!eve.provider.publish("test.sensing.task",provider).ok) throw "publish";
        if(!ai.registerTaskProvider("game.move","test.sensing.task").ok) throw "task registration";
        if(!ai.registerBehavior(@"{""id"":""test"",""initialState"":""idle"",""states"":[{""id"":""idle"",""tasks"":[{""id"":""move"",""type"":""game.move""}]}]}").ok) throw "behavior";
        local agent=ai.createAgent("test").value;
        if(!ai.remember(agent,"up","vision",1.0,1,20,"{}").ok) throw "memory";
        if(!ai.tick(1,0.01,8,8).ok) throw "tick";
        if(!ai.destroyAgent(agent).ok || stopped!=1) throw "stop";
        local second=ai.createAgent("test").value;
        if(!eve.provider.unload("test.sensing.task").ok) throw "unload";
        if(ai.tick(2,0.01,8,8).ok) throw "stale provider accepted";
        eve.result.ignore(ai.destroyAgent(second),"cleanup");
        if(!world.release().ok || world.query("{}").ok) throw "stale world";
        result="ok";
    )"));
    REQUIRE_EQ(vm.find("result").toString(), std::string("ok"));
    ScriptProviderRegistry::instance().clearAll();
}

TEST_CASE("sensing.dimensions.sphereShapeAndExtremeQueriesMatchLinear") {
    for (auto kind : {SpatialIndexKind::XY, SpatialIndexKind::XZ, SpatialIndexKind::YZ, SpatialIndexKind::XYZ}) {
        SensingWorld world;
        REQUIRE(world.upsertPosition("z", 0, 0, 12, "", "", "").ok());
        REQUIRE(world.upsertPosition("negative", -2, -3, -4, "", "", "").ok());
        REQUIRE(world.rebuildIndex(kind, 2).ok());
        QuerySpec spec;
        spec.axes   = QueryAxes::XYZ;
        spec.shape  = QuerySphere{0, 0, 12, 1};
        auto result = world.query({0, 0, std::nullopt, 0, CoordinateSpace::World3D}, spec);
        REQUIRE(result.ok());
        REQUIRE_EQ(result.value().size(), 1u);
        CHECK_EQ(result.value().ranked()[0].z, 12.f);
        spec.shape    = std::monostate{};
        spec.maxRange = std::numeric_limits<float>::max();
        auto huge     = world.query({0, 0, std::nullopt, 0, CoordinateSpace::World3D}, spec);
        REQUIRE(huge.ok());
        CHECK_EQ(huge.value().size(), 2u);
        REQUIRE(world.remove("z").ok());
        spec.axes     = QueryAxes::YZ;
        spec.maxRange = 1;
        spec.shape    = QueryCircle{-3, -4, 1};
        auto yz       = world.query({-2, -3, std::nullopt, -4, CoordinateSpace::World3D}, spec);
        REQUIRE(yz.ok());
        CHECK_EQ(yz.value().size(), 1u);
    }
}
TEST_CASE("sensing.dimensions.targetingProviderAcceptsWorld3D") {
    SensingWorld      world;
    const std::string id = "00000000-0000-7000-8000-000000000031";
    REQUIRE(world.upsertPosition(id, 0, 0, 8, "", "", "").ok());
    REQUIRE(world.rebuildIndex(SpatialIndexKind::XYZ, 4).ok());
    SensingWorldCandidateProvider provider(world);
    TargetingQuery                request;
    auto                          originId = PersistentId::parse("00000000-0000-7000-8000-000000000032");
    REQUIRE(originId.has_value());
    request.origin = SubjectRef::fromPersistentId(*originId);
    auto point     = WorldPoint::world3D(0, 0, 0);
    REQUIRE(point.ok());
    request.originLocation = point.value();
    request.spec.space     = CoordinateSpace::World3D;
    request.spec.maxRange  = 10;
    auto result            = provider.query(request);
    REQUIRE(result.ok());
    REQUIRE_EQ(result.value().size(), 1u);
    CHECK_EQ(std::get<WorldPoint>(result.value()[0].location).z(), 8.f);
}
