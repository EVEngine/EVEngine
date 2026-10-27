// clang-format off
/* eve-test-policy:
{"schema":"evengine.test-policy","version":1,"role":"tests","default":"performance","module":"rts"}
*/
// clang-format on

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include "common/ECS.h"
#include "crowd/Crowd.h"
#include "rts/RTSSystems.h"
#include "rts/RTSTypes.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace test_rts_crowd_overlap_benchmark {
using namespace eve::rts;
TEST_CASE("rts.largeExactlyOverlappingCrowdUnlocksDeterministically") {
    auto simulate = [&] {
        ecs::Table        world;
        ecs::ScopedTable  guard(world);
        eve::crowd::Crowd crowd;
        crowd.setClampToField(false);
        crowd.setResolveOverlaps(true);
        crowd.setSeparationRadius(2.0f);
        crowd.setSeparationWeight(1.0f);
        std::vector<Unit*> units;
        units.reserve(96);
        for (int index = 0; index < 96; ++index) {
            Unit* unit = Unit::createUnit();
            auto  link = eve::rts::CrowdLink::bind("rts/overlap/" + std::to_string(index));
            REQUIRE(link.ok());
            unit->crowd()->link   = std::move(link).takeValue();
            unit->crowd()->radius = 0.45f;
            unit->motion()->speed = 4.0f;
            CommandSpec move;
            move.kind   = OrderKind::Move;
            move.target = {20.0f, 10.0f};
            REQUIRE(unit->orders()->values.enqueue(move).ok());
            units.push_back(unit);
        }
        for (std::uint64_t tick = 1; tick <= 120; ++tick) {
            const eve::SimulationStep step{eve::SimulationTick{tick},
                                           eve::Duration::fromSeconds(0.05).expect("large crowd dt")};
            REQUIRE(eve::rts::CrowdMotionSystem::step(step, crowd).ok());
        }
        std::vector<WorldPosition> positions;
        positions.reserve(units.size());
        for (Unit* unit : units) positions.push_back({unit->motion()->x, unit->motion()->y});
        for (Unit* unit : units) unit->release();
        return positions;
    };

    const auto first  = simulate();
    const auto second = simulate();
    REQUIRE_EQ(first.size(), second.size());
    int progressed = 0;
    for (std::size_t index = 0; index < first.size(); ++index) {
        progressed += first[index].x > 5.0f;
        REQUIRE(std::abs(first[index].x - second[index].x) < 1e-5f);
        REQUIRE(std::abs(first[index].y - second[index].y) < 1e-5f);
    }
    REQUIRE(progressed > 80);
}

}  // namespace test_rts_crowd_overlap_benchmark
