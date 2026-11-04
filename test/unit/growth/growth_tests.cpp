// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "growth.cpp"
  ]
}
*/
// clang-format on

#include "growth/Growth.h"
#include "growth/GrowthCalendar.h"
#include "growth/GrowthSystem.h"
#include "growth/Organism.h"
#include "growth/Species.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <string>

using namespace eve::growth;

namespace {

struct CleanGrowth {
    CleanGrowth() {
        GrowthWorld::resetInstance();
        SpeciesRegistry::clear();
    }
    ~CleanGrowth() {
        GrowthWorld::resetInstance();
        SpeciesRegistry::clear();
    }
};

SpeciesDef wheat() {
    SpeciesDef def;
    def.id                   = "crop.wheat";
    def.displayName          = "Wheat";
    def.kind                 = OrganismKind::Plant;
    def.stages               = {{"seed", 1.0}, {"sprout", 1.0}, {"ripe", 1.0}};
    def.waterDrainPerDay     = 100.0;
    def.fertilityDrainPerDay = 10.0;
    def.minWaterPermille     = 100.0;
    def.minFertilityPermille = 50.0;
    def.seasonMul[0]         = 1.0;
    def.seasonMul[1]         = 1.2;
    def.seasonMul[2]         = 0.8;
    def.seasonMul[3]         = 0.2;
    def.harvestYield         = {{"item.wheat", 2, 2}};
    def.diseases             = {{"blight", 1.0, 2.0, 0.25}};
    def.baseDiseaseResist    = 0.0;
    return def;
}

SpeciesDef cow() {
    SpeciesDef def;
    def.id               = "animal.cow";
    def.displayName      = "Cow";
    def.kind             = OrganismKind::Animal;
    def.stages           = {{"calf", 2.0}, {"adult", 1.0}};
    def.waterDrainPerDay = 50.0;
    def.foodDrainPerDay  = 50.0;
    def.minWaterPermille = 100.0;
    def.minFoodPermille  = 100.0;
    def.seasonMul[0] = def.seasonMul[1] = def.seasonMul[2] = def.seasonMul[3] = 1.0;
    def.produceIntervalDays                                                   = 2.0;
    def.produceYield                                                          = {{"item.milk", 1, 1}};
    def.canBreed                                                              = true;
    def.gestationDays                                                         = 3.0;
    def.offspringSpeciesId                                                    = "animal.cow";
    return def;
}

void keepHydrated(Organism& organism) {
    REQUIRE(organism.waterBy(1000.0).ok());
    if (organism.kind() == OrganismKind::Plant) {
        REQUIRE(organism.fertilize(1000.0).ok());
    } else {
        REQUIRE(organism.feed(1000.0).ok());
    }
}

}  // namespace

TEST_CASE("growth.species.registry_json_and_lookup") {
    CleanGrowth clean;
    auto        registered = SpeciesRegistry::registerSpecies(wheat());
    REQUIRE(registered.ok());
    CHECK(SpeciesRegistry::has("crop.wheat"));
    CHECK_EQ(SpeciesRegistry::count(), 1);

    const char* json =
        "[{\"id\":\"animal.chicken\",\"kind\":\"animal\",\"stages\":["
        "{\"id\":\"chick\",\"durationDays\":1},{\"id\":\"hen\",\"durationDays\":1}],"
        "\"foodDrainPerDay\":20,\"canBreed\":true,\"gestationDays\":2}]";
    auto loaded = SpeciesRegistry::loadFromJson(json);
    REQUIRE(loaded.ok());
    CHECK_EQ(loaded.value(), 1);
    CHECK(SpeciesRegistry::has("animal.chicken"));
}

TEST_CASE("growth.crop.water_gate_and_stage") {
    CleanGrowth clean;
    REQUIRE(SpeciesRegistry::registerSpecies(wheat()).ok());
    auto planted = Organism::createOrganism("crop.wheat");
    REQUIRE(planted.ok());
    Organism* crop = planted.value();
    CHECK_EQ(crop->stageId(), std::string("seed"));
    CHECK(!crop->isMature());

    crop->vitalNeeds()->waterPermille = 0.0;
    REQUIRE(GrowthWorld::instance().advanceDays(2.0).ok());
    CHECK_EQ(crop->stageIndex(), 0);

    REQUIRE(crop->waterBy(1000.0).ok());
    REQUIRE(crop->fertilize(1000.0).ok());
    REQUIRE(GrowthWorld::instance().advanceDays(1.0).ok());
    CHECK_EQ(crop->stageIndex(), 1);
    CHECK_EQ(crop->stageId(), std::string("sprout"));
}

TEST_CASE("growth.crop.mature_and_harvest") {
    CleanGrowth clean;
    REQUIRE(SpeciesRegistry::registerSpecies(wheat()).ok());
    auto planted = Organism::createOrganism("crop.wheat");
    REQUIRE(planted.ok());
    Organism* crop = planted.value();
    for (int i = 0; i < 6; ++i) {
        keepHydrated(*crop);
        REQUIRE(GrowthWorld::instance().advanceDays(1.0).ok());
    }
    CHECK(crop->isMature());
    auto yield = crop->harvest();
    REQUIRE(yield.ok());
    REQUIRE_EQ(yield.value().size(), static_cast<std::size_t>(1));
    CHECK_EQ(yield.value()[0].itemId, std::string("item.wheat"));
    CHECK_EQ(yield.value()[0].count, 2);
    CHECK(crop->isHarvested());
    CHECK(!crop->harvest().ok());
}

TEST_CASE("growth.crop.winter_slower_than_spring") {
    CleanGrowth clean;
    REQUIRE(SpeciesRegistry::registerSpecies(wheat()).ok());
    auto springCrop = Organism::createOrganism("crop.wheat");
    REQUIRE(springCrop.ok());
    keepHydrated(*springCrop.value());
    REQUIRE(GrowthWorld::instance().advanceDays(0.5).ok());
    const double springProgress = springCrop.value()->stageProgressDays();
    CHECK(springProgress > 0.4);

    GrowthWorld::resetInstance();
    REQUIRE(SpeciesRegistry::registerSpecies(wheat()).ok());
    REQUIRE(GrowthWorld::instance().calendar().setDaysPerSeason(28).ok());
    GrowthWorld::instance().calendar().advanceWholeDays(28 * 3);
    CHECK_EQ(static_cast<int>(GrowthWorld::instance().calendar().season()), static_cast<int>(Season::Winter));
    auto winterCrop = Organism::createOrganism("crop.wheat");
    REQUIRE(winterCrop.ok());
    keepHydrated(*winterCrop.value());
    REQUIRE(GrowthWorld::instance().advanceDays(0.5).ok());
    const double winterProgress = winterCrop.value()->stageProgressDays();
    CHECK(winterProgress + 1e-6 < springProgress);
}

TEST_CASE("growth.livestock.feed_produce_breed") {
    CleanGrowth clean;
    REQUIRE(SpeciesRegistry::registerSpecies(cow()).ok());

    auto a = Organism::createOrganism("animal.cow");
    auto b = Organism::createOrganism("animal.cow");
    REQUIRE(a.ok());
    REQUIRE(b.ok());

    for (int i = 0; i < 5; ++i) {
        keepHydrated(*a.value());
        keepHydrated(*b.value());
        REQUIRE(GrowthWorld::instance().advanceDays(1.0).ok());
    }
    CHECK(a.value()->isMature());
    CHECK(b.value()->isMature());

    GrowthWorld::instance().events().clear();
    keepHydrated(*a.value());
    keepHydrated(*b.value());
    REQUIRE(GrowthWorld::instance().advanceDays(2.0).ok());
    bool produced = false;
    for (int i = 0; i < GrowthWorld::instance().events().count(); ++i) {
        const GrowthEvent* event = GrowthWorld::instance().events().at(i);
        if (event && event->kind == GrowthEventKind::Produced) produced = true;
    }
    CHECK(produced);

    REQUIRE(GrowthWorld::instance().tryBreed(*a.value(), *b.value()).ok());
    CHECK(a.value()->isPregnant());
    const int before = static_cast<int>(Organism::liveOrganisms().size());
    for (int i = 0; i < 4; ++i) {
        keepHydrated(*a.value());
        keepHydrated(*b.value());
        REQUIRE(GrowthWorld::instance().advanceDays(1.0).ok());
    }
    CHECK(!a.value()->isPregnant());
    CHECK(static_cast<int>(Organism::liveOrganisms().size()) > before);
}

TEST_CASE("growth.disease.infects_when_stressed") {
    CleanGrowth clean;
    REQUIRE(SpeciesRegistry::registerSpecies(wheat()).ok());
    GrowthWorld::instance().setRngSeed(7);
    auto planted = Organism::createOrganism("crop.wheat");
    REQUIRE(planted.ok());
    planted.value()->vitalNeeds()->waterPermille = 0.0;
    REQUIRE(GrowthWorld::instance().advanceDays(1.0).ok());
    CHECK(planted.value()->isDiseased());
    CHECK_EQ(planted.value()->diseaseId(), std::string("blight"));
}

TEST_CASE("growth.calendar.and_module_facade") {
    CleanGrowth clean;
    Growth      module;
    REQUIRE(module.setDaysPerSeason(10).ok());
    CHECK_EQ(module.seasonName(), std::string("spring"));
    REQUIRE(module.advanceDays(10).ok());
    CHECK_EQ(module.seasonName(), std::string("summer"));
    REQUIRE(module.advanceDays(20).ok());
    CHECK_EQ(module.seasonName(), std::string("winter"));
}
