#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "building/Building.h"
#include "building/BuildingDef.h"
#include "building/Ghost.h"
#include "building/PlacementSystem.h"
#include "building/PlacementWorld.h"
#include "buildingfx/BuildingFx.h"

using namespace eve::building;
using namespace eve::buildingfx;

TEST_CASE("buildingfx.sync.lifecycle") {
    BuildingRegistry::clear();
    PlacementSystem::clearEvents();
    PlacementSystem::ensureBuiltins();

    BuildingDefinition hut;
    hut.id = "hut";
    hut.displayName = "Hut";
    hut.footprintW = 2;
    hut.footprintH = 1;
    hut.renderMode = "2d";
    hut.visual2d["colorR"] = "0.8";
    hut.visual2d["colorG"] = "0.4";
    hut.visual2d["colorB"] = "0.2";
    BuildingRegistry::registerBuilding(hut);

    PlacementWorld world(8, 8, 32.f);
    BuildingFx *fx = BuildingFx::create();
    REQUIRE(fx != nullptr);

    REQUIRE(fx->attach(&world));
    REQUIRE(fx->isAttached(&world));
    REQUIRE_EQ(fx->getVisualCount(&world), 0);

    const int a = world.placeAt("hut", 1, 1, 0.f);
    const int b = world.placeAt("hut", 4, 2, 0.f);
    REQUIRE(a > 0);
    REQUIRE(b > 0);
    fx->sync(&world);
    REQUIRE_EQ(fx->getVisualCount(&world), 2);

    REQUIRE(world.removeBuilding(a));
    fx->sync(&world);
    REQUIRE_EQ(fx->getVisualCount(&world), 1);

    Ghost ghost;
    ghost.setBuildingId("hut");
    ghost.setFromWorld(&world, 140.f, 80.f);
    fx->updateGhost(&world, &ghost);
    fx->hideGhost(&world);

    fx->setGridVisible(&world, true);
    REQUIRE(fx->getGridVisible(&world));
    fx->setGridVisible(&world, false);
    REQUIRE(!fx->getGridVisible(&world));

    REQUIRE(fx->detach(&world));
    REQUIRE(!fx->isAttached(&world));
    REQUIRE_EQ(fx->getVisualCount(&world), 0);

    BuildingRegistry::clear();
}
