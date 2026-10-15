#include "daynight/sky/SkyProfile.h"
#include <fstream>
#include <iterator>
#include <limits>
#include "common/Value.h"
#include "daynight/sky/SkyRuntime.h"
#include "graphics/sky/SkyAtmospherePass.h"
#include "zeroerr/unittest.h"

TEST_CASE("daynight.sky profile validates the authored reference and owns imported values") {
    std::ifstream file(std::string(EVENGINE_SOURCE_DIR) + "/examples/uds-sky/sky.json");
    REQUIRE(file.good());
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    auto              parsed = eve::Value::fromJson(text);
    REQUIRE(parsed.ok());
    auto document = std::move(parsed).takeValue();
    auto profile  = eve::daynight::SkyProfile::decode(document);
    REQUIRE(profile.ok());
    REQUIRE(profile.value().settings().clock.initialHour == 12);
    REQUIRE(profile.value().settings().sunIrradiance[0] == 5);
    REQUIRE(profile.value().atmosphere().groundRadiusKm == 6360);
    auto rejected = document;
    rejected.set("version", 7);
    REQUIRE(!eve::daynight::SkyProfile::decode(rejected).ok());
    auto withWisps = document;
    withWisps.set("version", 3);
    withWisps.set("wispsAsset", "assets/wisps.json");
    auto authored = eve::daynight::SkyProfile::decode(withWisps);
    REQUIRE(authored.ok());
    REQUIRE(authored.value().wispsAsset() == "assets/wisps.json");
    REQUIRE(authored.value().settings().cloudMotion.speed == 0);
    auto animatedDocument = withWisps;
    animatedDocument.set("version", 4);
    animatedDocument.set(
        "cloudMotion",
        eve::Value::object({{"phase", -.25}, {"speed", .35}, {"timeScale", 1.0}, {"windMultiplier", 2.0}}));
    auto animated = eve::daynight::SkyProfile::decode(animatedDocument);
    REQUIRE(animated.ok());
    REQUIRE(animated.value().settings().cloudMotion.phase == -.25);
    REQUIRE(animated.value().settings().cloudMotion.speed == .35);
    auto daylight = animatedDocument;
    daylight.set("version", 5);
    daylight.find("heightFog")->set("directionalElevationRange", eve::Value::array({0.0, .2}));
    auto decodedDaylight = eve::daynight::SkyProfile::decode(daylight);
    REQUIRE(decodedDaylight.ok());
    REQUIRE(decodedDaylight.value().atmosphere().heightFog.directionalElevationRange[1] == .2f);
    REQUIRE(animated.value().atmosphere().heightFog.directionalElevationRange[1] == 0);
    auto generated = daylight;
    generated.set("version", 6);
    generated.set("wispsAsset", eve::Value{});
    generated.set("proceduralSeed", 0);
    auto defaultPack = eve::daynight::SkyProfile::decode(generated);
    REQUIRE(defaultPack.ok());
    REQUIRE(defaultPack.value().proceduralSeed().has_value());
    REQUIRE(*defaultPack.value().proceduralSeed() == 0);
    generated.set("proceduralSeed", -1);
    REQUIRE(!eve::daynight::SkyProfile::decode(generated).ok());
    generated.set("proceduralSeed", 2026);
    generated.set("wispsAsset", "pack/sky.json");
    auto externalPack = eve::daynight::SkyProfile::decode(generated);
    REQUIRE(externalPack.ok());
    REQUIRE(!externalPack.value().proceduralSeed());
    generated.set("wispsAsset", "");
    REQUIRE(!eve::daynight::SkyProfile::decode(generated).ok());
    daylight.find("heightFog")->set("directionalElevationRange", eve::Value::array({.2, 0.0}));
    REQUIRE(!eve::daynight::SkyProfile::decode(daylight).ok());
    daylight.find("heightFog")
        ->set("directionalElevationRange", eve::Value::array({0.0, std::numeric_limits<double>::infinity()}));
    REQUIRE(!eve::daynight::SkyProfile::decode(daylight).ok());
    auto badMotion = animatedDocument;
    badMotion.find("cloudMotion")->set("speed", -1.0);
    REQUIRE(!eve::daynight::SkyProfile::decode(badMotion).ok());
    badMotion = animatedDocument;
    badMotion.find("cloudMotion")->set("windMultiplier", std::numeric_limits<double>::infinity());
    REQUIRE(!eve::daynight::SkyProfile::decode(badMotion).ok());
    badMotion = animatedDocument;
    badMotion.find("cloudMotion")->set("future", true);
    REQUIRE(!eve::daynight::SkyProfile::decode(badMotion).ok());
    REQUIRE(profile.value().wispsAsset().empty());
    auto legacy = eve::Value::object({});
    for (const auto& key : document.keys())
        if (key != "heightFog") legacy.set(key, *document.find(key));
    legacy.set("version", 1);
    auto migrated = eve::daynight::SkyProfile::decode(legacy);
    REQUIRE(migrated.ok());
    REQUIRE(migrated.value().atmosphere().heightFog.densityPerMetre == 0);
    REQUIRE(profile.value().atmosphere().heightFog.densityPerMetre > 0);
    rejected = document;
    rejected.find("heightFog")->set("densityPerMetre", -1);
    REQUIRE(!eve::daynight::SkyProfile::decode(rejected).ok());
    rejected = document;
    rejected.find("heightFog")->set("future", 1);
    REQUIRE(!eve::daynight::SkyProfile::decode(rejected).ok());
    rejected = document;
    rejected.set("mode", "volumetric-clouds");
    REQUIRE(!eve::daynight::SkyProfile::decode(rejected).ok());
    rejected = document;
    rejected.set("typo", 1);
    REQUIRE(!eve::daynight::SkyProfile::decode(rejected).ok());
    rejected = document;
    rejected.find("clock")->set("initialHour", std::numeric_limits<double>::quiet_NaN());
    REQUIRE(!eve::daynight::SkyProfile::decode(rejected).ok());
    rejected = document;
    rejected.find("atmosphere")->set("groundRadiusKm", -1);
    REQUIRE(!eve::daynight::SkyProfile::decode(rejected).ok());
    rejected = document;
    rejected.find("sun")->set("irradiance", eve::Value::array({1, 2}));
    REQUIRE(!eve::daynight::SkyProfile::decode(rejected).ok());
    rejected = document;
    rejected.find("clock")->set("unknown", 0);
    REQUIRE(!eve::daynight::SkyProfile::decode(rejected).ok());
    rejected = document;
    rejected.find("atmosphere")->set("mieHeightKm", 1e100);
    REQUIRE(!eve::daynight::SkyProfile::decode(rejected).ok());
    rejected = document;
    rejected.find("sun")->set("irradiance", eve::Value::array({-1e-300, 5, 5}));
    REQUIRE(!eve::daynight::SkyProfile::decode(rejected).ok());
    document = eve::Value();
    REQUIRE(!eve::daynight::SkyProfile::decode(document).ok());
    REQUIRE(profile.value().settings().clock.initialHour == 12);
    REQUIRE(profile.value().atmosphere().groundRadiusKm == 6360);
}
