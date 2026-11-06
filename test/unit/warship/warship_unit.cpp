// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "warship.cpp"
  ]
}
*/
// clang-format on

#include "warship/Warship.h"
#include "warship/WarshipRegistry.h"
#include "warship/WarshipSystem.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <string>

using namespace eve::warship;

namespace {

constexpr auto functions = R"([
  {"id":"bridge","name":"Bridge","powerDraw":2,"crewCapacity":8},
  {"id":"reactor","name":"Reactor","powerProvide":12},
  {"id":"engine","name":"Main Engine","powerDraw":4,"thrustBonus":0.25},
  {"id":"hangar","name":"Hangar","powerDraw":3,"hangarCapacity":6},
  {"id":"weapon_bay","name":"Weapon Bay","powerDraw":2,"weaponBayCount":2},
  {"id":"quarters","name":"Crew Quarters","powerDraw":1,"crewCapacity":20},
  {"id":"sail_rig","name":"Sail Rig","sailPower":0.8,"thrustBonus":0.05},
  {"id":"shield","name":"Shield Gen","powerDraw":5}
])";

constexpr auto hulls = R"([
  {"id":"destroyer","category":"warship","mobility":"surface",
   "length":140,"beam":24,"maxSpeed":48,"accel":14,"turnRate":28,"maxHealth":900,
   "grid":{"cols":4,"rows":2,"decks":1},
   "defaultLayout":[
     {"deck":0,"row":0,"col":0,"function":"bridge"},
     {"deck":0,"row":0,"col":1,"function":"reactor"},
     {"deck":0,"row":1,"col":0,"function":"engine"},
     {"deck":0,"row":1,"col":1,"function":"weapon_bay"}
   ]},
  {"id":"clipper","category":"sailship","mobility":"sail",
   "length":90,"beam":18,"maxSpeed":36,"accel":8,"turnRate":32,"maxHealth":400,
   "grid":{"cols":3,"rows":1,"decks":1},
   "defaultLayout":[
     {"deck":0,"row":0,"col":0,"function":"bridge"},
     {"deck":0,"row":0,"col":1,"function":"sail_rig"},
     {"deck":0,"row":0,"col":2,"function":"quarters"}
   ]},
  {"id":"carrier","category":"carrier","mobility":"carrier",
   "length":280,"beam":60,"maxSpeed":30,"accel":6,"turnRate":12,"maxHealth":2200,
   "grid":{"cols":5,"rows":2,"decks":2},
   "defaultLayout":[
     {"deck":0,"row":0,"col":0,"function":"bridge"},
     {"deck":0,"row":0,"col":1,"function":"reactor"},
     {"deck":0,"row":1,"col":0,"function":"hangar"},
     {"deck":0,"row":1,"col":1,"function":"hangar"},
     {"deck":1,"row":0,"col":0,"function":"engine"}
   ]},
  {"id":"cruiser","category":"starship","mobility":"space",
   "length":220,"beam":48,"maxSpeed":70,"accel":20,"turnRate":40,"maxHealth":1600,
   "grid":{"cols":4,"rows":2,"decks":1},
   "defaultLayout":[
     {"deck":0,"row":0,"col":0,"function":"bridge"},
     {"deck":0,"row":0,"col":1,"function":"reactor"},
     {"deck":0,"row":0,"col":2,"function":"shield"},
     {"deck":0,"row":1,"col":0,"function":"engine"}
   ]}
])";

void seedCatalogues(Warship& module) {
    module.clearHullDefinitions();
    module.clearFunctionDefinitions();
    auto fn = module.registerFunctionsFromJson(functions);
    REQUIRE(fn.ok());
    CHECK_EQ(fn.value(), 8);
    auto hull = module.registerHullsFromJson(hulls);
    REQUIRE(hull.ok());
    CHECK_EQ(hull.value(), 4);
}

WarshipEntity& spawn(Warship& module, const char* defId) {
    auto ship = module.newWarship(defId, 0.f, 0.f, 0.f, "blue");
    REQUIRE(ship.hasValue());
    REQUIRE(ship.value() != nullptr);
    return *ship.value();
}

}  // namespace

TEST_CASE("warship.definition.validAndMalformedInput") {
    Warship module;
    seedCatalogues(module);
    CHECK(module.hasHullDefinition("destroyer"));
    CHECK_EQ(module.getHullDefinitionCategory("destroyer"), std::string("warship"));
    CHECK_EQ(module.getHullDefinitionMobility("clipper"), std::string("sail"));
    CHECK_EQ(module.getHullDefinitionCategory("carrier"), std::string("carrier"));
    CHECK_EQ(module.getHullDefinitionMobility("cruiser"), std::string("space"));
    auto bad = module.registerHullsFromJson("not json");
    CHECK(!bad.ok());
    auto missing = module.registerHullsFromJson(R"([{"name":"no-id"}])");
    CHECK(!missing.ok());
}

TEST_CASE("warship.factory.seedsLayoutAndCapabilities") {
    Warship module;
    seedCatalogues(module);
    WarshipEntity& ship = spawn(module, "destroyer");
    CHECK_EQ(module.getCategory(&ship), std::string("warship"));
    CHECK_EQ(module.getLength(&ship), 140.f);
    CHECK_EQ(module.getBeam(&ship), 24.f);
    CHECK_EQ(module.getGridCols(&ship), 4);
    CHECK_EQ(module.getGridRows(&ship), 2);
    CHECK_EQ(module.getCompartmentFunction(&ship, 0, 0, 0), std::string("bridge"));
    CHECK_EQ(module.getCompartmentFunction(&ship, 0, 1, 1), std::string("weapon_bay"));
    CHECK_EQ(module.getWeaponBayCount(&ship), 2);
    CHECK(module.getPowerNet(&ship) > 0.f);
    CHECK(!module.isUnderpowered(&ship));
    CHECK(module.getThrustMul(&ship) > 1.f);
}

TEST_CASE("warship.layout.placeSwapClearAndAtomicRearrange") {
    Warship module;
    seedCatalogues(module);
    WarshipEntity& ship = spawn(module, "destroyer");
    module.clearEvents();

    auto placed = module.placeCompartment(&ship, 0, 0, 2, "hangar");
    REQUIRE(placed.ok());
    CHECK_EQ(module.getCompartmentFunction(&ship, 0, 0, 2), std::string("hangar"));
    CHECK_EQ(module.getHangarCapacity(&ship), 6);

    auto swapped = module.swapCompartments(&ship, 0, 0, 2, 0, 1, 1);
    REQUIRE(swapped.ok());
    CHECK_EQ(module.getCompartmentFunction(&ship, 0, 0, 2), std::string("weapon_bay"));
    CHECK_EQ(module.getCompartmentFunction(&ship, 0, 1, 1), std::string("hangar"));

    const std::string beforeFail = module.getCompartmentFunction(&ship, 0, 0, 0);
    auto              bad = module.rearrangeFromJson(&ship, R"([{"deck":0,"row":0,"col":0,"function":"missing_fn"}])");
    CHECK(!bad.ok());
    CHECK_EQ(module.getCompartmentFunction(&ship, 0, 0, 0), beforeFail);

    auto rearranged = module.rearrangeFromJson(&ship, R"([
      {"deck":0,"row":0,"col":0,"function":"bridge"},
      {"deck":0,"row":0,"col":1,"function":"reactor"},
      {"deck":0,"row":1,"col":0,"function":"engine"},
      {"deck":0,"row":1,"col":2,"function":"quarters"}
    ])");
    REQUIRE(rearranged.ok());
    CHECK_EQ(module.getCompartmentFunction(&ship, 0, 1, 1), std::string{});
    CHECK_EQ(module.getCompartmentFunction(&ship, 0, 1, 2), std::string("quarters"));
    CHECK_EQ(module.getCrewCapacity(&ship), 28);

    auto cleared = module.clearCompartment(&ship, 0, 0, 1);
    REQUIRE(cleared.ok());
    CHECK(module.isUnderpowered(&ship));
    CHECK(module.getEventCount() > 0);
}

TEST_CASE("warship.motion.moveToAndDamage") {
    Warship module;
    seedCatalogues(module);
    WarshipEntity& ship = spawn(module, "destroyer");
    module.moveTo(&ship, 200.f, 0.f);
    for (int i = 0; i < 240; ++i) module.update(1.f / 60.f);
    CHECK(module.getX(&ship) > 10.f);
    CHECK(std::fabs(module.getY(&ship)) < 40.f);

    module.applyDamage(&ship, 500.f);
    CHECK_EQ(module.getHealth(&ship), 400.f);
    CHECK(!module.isDestroyed(&ship));
    module.applyDamage(&ship, 500.f);
    CHECK(module.isDestroyed(&ship));
    CHECK_EQ(module.getHealth(&ship), 0.f);
}

TEST_CASE("warship.categories.coverSailCarrierStarship") {
    Warship module;
    seedCatalogues(module);
    CHECK_EQ(module.getCategory(&spawn(module, "clipper")), std::string("sailship"));
    CHECK_EQ(module.getCategory(&spawn(module, "carrier")), std::string("carrier"));
    CHECK_EQ(module.getGridDecks(&spawn(module, "carrier")), 2);
    CHECK_EQ(module.getCategory(&spawn(module, "cruiser")), std::string("starship"));
    CHECK(module.getMobilityCount() >= 4);
}
