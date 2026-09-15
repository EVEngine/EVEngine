#include "sensing/Sensing.h"
#include "sensing/TargetingPipeline.h"
#include "common/Module.h"
#include "simplesquirrel/simplesquirrel.hpp"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cstddef>
#include <optional>
#include <string>

using namespace eve::sensing;

TEST_CASE("sensing.filtersAndSortsCandidates") {
    SensingWorld w;
    auto         b = w.upsert("b", 3, 4, "red", "unit,tank", "p1");
    auto         a = w.upsert("a", 1, 0, "blue", "unit,infantry", "p1,p2");
    auto         c = w.upsert("c", 2, 0, "red", "building", "p1");
    REQUIRE(b.ok());
    REQUIRE(a.ok());
    REQUIRE(c.ok());
    auto count = w.circle(0, 0, 10, "unit", "", "", "blue", "p1", 8);
    REQUIRE(count.ok());
    CHECK_EQ(count.value(), 1);
    auto candidate = w.resultAt(0);
    REQUIRE(candidate.has_value());
    CHECK_EQ(candidate->get().id, std::string("b"));
}
TEST_CASE("sensing.ordersByDistanceThenIdAndLimits") {
    SensingWorld w;
    auto         z = w.upsert("z", 1, 0, "", "u", "");
    auto         a = w.upsert("a", -1, 0, "", "u", "");
    REQUIRE(z.ok());
    REQUIRE(a.ok());
    auto count = w.circle(0, 0, 2, "u", "", "", "", "", 1);
    REQUIRE(count.ok());
    CHECK_EQ(count.value(), 1);
    auto candidate = w.resultAt(0);
    REQUIRE(candidate.has_value());
    CHECK_EQ(candidate->get().id, std::string("a"));
}
TEST_CASE("sensing.snapshotIsTransactional") {
    SensingWorld w;
    auto         inserted = w.upsert("u", 2, 3, "f", "b,a", "p");
    REQUIRE(inserted.ok());
    auto         s = w.snapshotJson();
    SensingWorld x;
    auto         restored = x.restoreJson(s);
    REQUIRE(restored.ok());
    CHECK_EQ(x.snapshotJson(), s);
    auto before = x.snapshotJson();
    auto rejected = x.restoreJson("{}");
    CHECK(!rejected.ok());
    CHECK_EQ(x.snapshotJson(), before);
}

TEST_CASE("sensing.querySpecRanksAndHonorsCountPolicy") {
    SensingWorld w;
    REQUIRE(w.upsert("near", 1, 0, "red", "unit", "").ok());
    REQUIRE(w.upsert("mid", 3, 0, "red", "unit", "").ok());
    REQUIRE(w.upsert("far", 9, 0, "red", "unit", "").ok());

    QuerySpec truncate;
    truncate.requiredTags = {"unit"};
    truncate.minRange     = 0.f;
    truncate.maxRange     = 10.f;
    truncate.maxCount     = 2;
    truncate.countPolicy  = CountPolicy::TruncateToMax;
    truncate.sortKey      = SortKey::DistanceAscending;
    auto ranked = w.query(QueryOrigin{0.f, 0.f, std::nullopt}, truncate);
    REQUIRE(ranked.ok());
    CHECK_EQ(ranked.value().size(), 2u);
    CHECK_EQ(ranked.value().ranked()[0].id, std::string("near"));
    CHECK_EQ(ranked.value().ranked()[1].id, std::string("mid"));
    CHECK_EQ(w.resultAt(0)->get().id, std::string("near"));

    QuerySpec fail = truncate;
    fail.countPolicy = CountPolicy::FailIfOutOfRange;
    auto failed = w.query(QueryOrigin{0.f, 0.f, std::nullopt}, fail);
    CHECK(!failed.ok());
    CHECK_EQ(failed.code(), eve::StatusCode::Rejected);

    QuerySpec sugarLike;
    sugarLike.shape         = QueryCircle{0.f, 0.f, 10.f};
    sugarLike.requiredTags  = {"unit"};
    sugarLike.maxRange      = 10.f;
    sugarLike.maxCount      = 8;
    sugarLike.countPolicy   = CountPolicy::TruncateToMax;
    auto viaQuery           = w.query(QueryOrigin{0.f, 0.f, std::nullopt}, sugarLike);
    auto viaCircle          = w.circle(0.f, 0.f, 10.f, "unit", "", "", "", "", 8);
    REQUIRE(viaQuery.ok());
    REQUIRE(viaCircle.ok());
    CHECK_EQ(viaQuery.value().size(), static_cast<std::size_t>(viaCircle.value()));
}

TEST_CASE("sensing.queryConeFiltersByFacing") {
    SensingWorld w;
    REQUIRE(w.upsert("ahead", 5.f, 0.f, "red", "unit", "").ok());
    REQUIRE(w.upsert("side", 0.f, 5.f, "red", "unit", "").ok());
    REQUIRE(w.upsert("behind", -5.f, 0.f, "red", "unit", "").ok());

    QuerySpec cone;
    cone.shape        = QueryCone{0.f, 0.f, 1.f, 0.f, 0.785398163f, 10.f};
    cone.requiredTags = {"unit"};
    cone.maxRange     = 10.f;
    cone.countPolicy  = CountPolicy::TruncateToMax;
    cone.sortKey      = SortKey::DistanceAscending;
    auto ranked       = w.query(QueryOrigin{0.f, 0.f, std::nullopt}, cone);
    REQUIRE(ranked.ok());
    CHECK_EQ(ranked.value().size(), 1u);
    CHECK_EQ(ranked.value().ranked()[0].id, std::string("ahead"));
}

TEST_CASE("sensing.executePresetRunsBuiltinConeSelect") {
    SensingWorld w;
    REQUIRE(w.upsert("ahead", 4.f, 0.f, "red", "unit", "").ok());
    REQUIRE(w.upsert("left", 0.f, 4.f, "red", "unit", "").ok());
    REQUIRE(w.upsert("farAhead", 40.f, 0.f, "red", "unit", "").ok());

    TargetingPipeline pipeline = TargetingPipeline::withBuiltins();
    TargetingPreset   preset;
    preset.id = "test.coneSelect";
    {
        TargetingTaskStep select;
        select.taskId                = "sensing.select.world";
        select.querySpec.maxRange    = 20.f;
        select.querySpec.requiredTags = {"unit"};
        select.querySpec.countPolicy = CountPolicy::TruncateToMax;
        select.querySpec.sortKey     = SortKey::None;
        preset.steps.push_back(std::move(select));
    }
    {
        TargetingTaskStep cone;
        cone.taskId        = "sensing.filter.cone";
        cone.coneHalfAngle = 0.5f;
        cone.coneRange     = 20.f;
        preset.steps.push_back(std::move(cone));
    }
    {
        TargetingTaskStep sort;
        sort.taskId = "sensing.sort.distance";
        preset.steps.push_back(std::move(sort));
    }
    {
        TargetingTaskStep truncate;
        truncate.taskId        = "sensing.sort.truncate";
        truncate.truncateCount = 4;
        preset.steps.push_back(std::move(truncate));
    }
    REQUIRE(pipeline.registerPreset(std::move(preset)).ok());

    TargetingSourceContext context;
    context.world  = &w;
    context.origin = QueryOrigin{0.f, 0.f, std::nullopt};
    context.dirX   = 1.f;
    context.dirY   = 0.f;
    auto executed  = pipeline.executePreset(context, "test.coneSelect");
    REQUIRE(executed.ok());
    CHECK_EQ(executed.value().size(), 1u);
    CHECK_EQ(executed.value().ranked()[0].id, std::string("ahead"));

    // Shared builtins path used by SensingWorld::executePreset
    auto viaWorld = w.executePreset("sensing.builtin.coneSelect", 0.f, 0.f, 1.f, 0.f);
    REQUIRE(viaWorld.ok());
    CHECK_EQ(viaWorld.value(), 1);
    CHECK_EQ(w.resultAt(0)->get().id, std::string("ahead"));
}

TEST_CASE("sensing.worldHandleAndScriptResultContract") {
    auto created = Sensing::newWorld();
    REQUIRE(created.ok());
    const auto reference = std::move(created).takeValue();
    auto       borrowed  = Sensing::resolve(reference);
    REQUIRE(borrowed.isBound());
    auto released = Sensing::release(reference);
    REQUIRE(released.ok());
    CHECK(Sensing::isStale(reference));
    CHECK(!Sensing::resolve(reference).isBound());

    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local sensing = eve.Sensing();
        local created = sensing.newWorld();
        if (created.ok && created.value.ownership() == "owned" && !created.value.isStale()) {
            local world = created.value;
            local inserted = world.upsert("script.subject", 0.0, 0.0, "red", "unit", "all");
            local queried = world.circle(0.0, 0.0, 2.0, "unit", "", "", "", "all", 4);
            local candidate = world.resultAt(0);
            local snapshot = world.snapshotJson();
            local restored = snapshot.ok ? world.restoreJson(snapshot.value) : { ok = false };
            local rejected = world.restoreJson("{}");
            local released = world.release();
            if (inserted.ok && queried.ok && queried.value == 1 && candidate != null &&
                snapshot.ok && restored.ok && !rejected.ok && released.ok && world.isStale())
                result = "ok";
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}
