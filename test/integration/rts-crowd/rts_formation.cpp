#include "zeroerr/assert.h"
#include <cstdio>
#include <limits>
#include <numbers>
#include <simplesquirrel/simplesquirrel.hpp>
#include "RtsCompositionFixtures.h"
#include "rts/RTSReplay.h"

TEST_CASE("rts.formation.largeTranslatedGridDoesNotReverseMarchingRows") {
    ecs::Table                   world;
    ecs::ScopedTable             guard(world);
    eve::rts::RTS                rts;
    std::vector<eve::SubjectRef> ids;
    for (int index = 0; index < 512; ++index) {
        char text[37];
        std::snprintf(text, sizeof(text), "00000000-0000-7000-8000-%012x", 0xb000 + index);
        const auto id         = subject(text);
        auto*      unit       = rts.newUnit(id).takeValue();
        unit->motion()->x     = 20.f - float(index % 8);
        unit->motion()->y     = 4.f + float(index / 8);
        unit->crowd()->radius = 0.35f;
        ids.push_back(id);
    }
    CommandSpec move;
    move.kind   = OrderKind::Move;
    move.target = {36.f, 35.5f};
    REQUIRE(rts.commandUnits(ids, move, {FormationKind::Grid, 1.f, 8}).ok());
    for (const auto& id : ids) {
        auto*      unit   = rts.findUnit(id);
        const auto target = unit->orders()->values.current().value().target;
        CHECK(std::abs((target.x - unit->motion()->x) - 19.5f) < 1e-4f);
        CHECK(std::abs(target.y - unit->motion()->y) < 1e-4f);
    }
}
