#include "sensing/Sensing.h"
#include "common/Module.h"
#include "simplesquirrel/simplesquirrel.hpp"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

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
