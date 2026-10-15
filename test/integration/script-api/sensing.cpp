#include "sensing/Sensing.h"
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include "common/Module.h"
#include "sensing/LineOfSightRouter.h"
#include "sensing/TargetingPipeline.h"
#include "simplesquirrel/simplesquirrel.hpp"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// sensing.cpp
using namespace eve::sensing;


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


// sensing_line_of_sight_router.cpp
namespace {

/** @brief A backend that answers everything one way, so routing is what the test observes. */
class StubBackend final : public eve::sensing::ILineOfSightQuery {
public:
    explicit StubBackend(bool answer) : answer_(answer) {}

    [[nodiscard]] eve::Result<eve::sensing::LineOfSightResult> query(
        const eve::sensing::TargetLocation&, const eve::sensing::TargetLocation&) const override {
        ++calls;
        eve::sensing::LineOfSightResult result;
        result.visible = answer_;
        return eve::Result<eve::sensing::LineOfSightResult>::success(result);
    }

    mutable int calls = 0;

private:
    bool answer_ = false;
};

[[maybe_unused]] eve::sensing::TargetLocation grid(std::int32_t x, std::int32_t y) {
    return eve::sensing::TargetLocation(eve::sensing::GridPoint::grid2D(x, y));
}

[[maybe_unused]] eve::sensing::TargetLocation world(float x, float y, float z) {
    // World points are validated on construction, so the test checks the Result here instead of
    // letting a bad literal silently become an invalid location.
    auto point = eve::sensing::WorldPoint::world3D(x, y, z);
    REQUIRE(point.ok());
    return eve::sensing::TargetLocation(std::move(point).takeValue());
}

}  // namespace
