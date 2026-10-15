#include "fluids/OceanInteractionCodec.h"
#include "fluids/OceanSpectrumCodec.h"
#include "zeroerr/unittest.h"

#include <limits>

using namespace eve;
using namespace eve::fluids;

TEST_CASE("fluids.ocean.spectrumDefinitionPreservesSeedAndWaves") {
    OceanSpectrumBuilder original;
    original.settings.seed          = std::numeric_limits<std::uint64_t>::max();
    original.settings.bands         = {{16, 64.0, 8.0, 64.0}};
    original.settings.current       = {0.8, -0.2};
    original.settings.windDirection = {0.3, 0.7};
    original.settings.seaLevel      = 12.25;
    auto json                       = encodeOceanSpectrumSettings(original.settings).toJson();
    REQUIRE(json.ok());
    auto value = Value::fromJson(json.value());
    REQUIRE(value.ok());
    auto decoded = decodeOceanSpectrumSettings(value.value());
    REQUIRE(decoded.ok());
    CHECK(decoded.value().seed == original.settings.seed);
    OceanSpectrumBuilder restored;
    restored.settings = decoded.value();
    auto a = original.build(), b = restored.build();
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    auto fa = a.value().frameBuilder(37.25).build(), fb = b.value().frameBuilder(37.25).build();
    REQUIRE(fa.ok());
    REQUIRE(fb.ok());
    for (std::size_t i = 0; i < fa.value().grids()[0].displacement.size(); ++i) {
        CHECK(glm::length(fa.value().grids()[0].displacement[i] - fb.value().grids()[0].displacement[i]) == 0.0);
        CHECK(glm::length(fa.value().grids()[0].velocity[i] - fb.value().grids()[0].velocity[i]) == 0.0);
    }
}

TEST_CASE("fluids.ocean.spectrumDefinitionRejectsAmbiguousInput") {
    const auto canonical = encodeOceanSpectrumSettings(OceanSpectrumSettings{});
    for (const auto* text : {"", "01", "+1", "-1", "1.0", "1 ", "18446744073709551616"}) {
        auto value = canonical;
        value.set("seed", Value(text));
        CHECK(!decodeOceanSpectrumSettings(value).ok());
    }
    for (auto replacement : {Value(1), Value(1.0), Value(true)}) {
        auto value = canonical;
        value.set("seed", replacement);
        CHECK(!decodeOceanSpectrumSettings(value).ok());
    }
    for (auto version : {Value(0), Value(2), Value(1.0), Value("1")}) {
        auto value = canonical;
        value.set("version", version);
        CHECK(!decodeOceanSpectrumSettings(value).ok());
    }
    auto unknown = canonical;
    unknown.set("typo", Value(1));
    CHECK(!decodeOceanSpectrumSettings(unknown).ok());
    unknown = canonical;
    unknown.find("bands")->at(0).set("typo", Value(1));
    CHECK(!decodeOceanSpectrumSettings(unknown).ok());
    auto missing = canonical;
    std::get<Value::Object>(missing.storage()).erase("gravity");
    CHECK(!decodeOceanSpectrumSettings(missing).ok());
    auto invalid = canonical;
    invalid.set("peakPeriod", Value(std::numeric_limits<double>::infinity()));
    CHECK(!decodeOceanSpectrumSettings(invalid).ok());
    invalid = canonical;
    invalid.find("bands")->at(0).set("resolution", Value(64.5));
    CHECK(!decodeOceanSpectrumSettings(invalid).ok());
    invalid = canonical;
    invalid.set("windDirection", Value::Array{Value(1)});
    CHECK(!decodeOceanSpectrumSettings(invalid).ok());
    invalid = canonical;
    invalid.set("bands", Value::Array(5, canonical.find("bands")->at(0)));
    CHECK(!decodeOceanSpectrumSettings(invalid).ok());
    // Structural decode is deliberately cheap; the canonical builder owns domain validation.
    invalid = canonical;
    invalid.set("peakPeriod", Value(-1.0));
    auto decoded = decodeOceanSpectrumSettings(invalid);
    REQUIRE(decoded.ok());
    OceanSpectrumBuilder builder;
    builder.settings = decoded.value();
    CHECK(!builder.build().ok());
    CHECK(decodeOceanSpectrumSettings(canonical).ok());
}

TEST_CASE("fluids.ocean.interactionArgumentsAreStrictAndOwning") {
    OceanWakeSettings original;
    original.resolution      = 16;
    original.length          = 32.0;
    original.absorptionWidth = 8.0;
    original.center          = {3.0, -4.0};
    auto wakeValue           = encodeOceanWakeSettings(original);
    auto decoded             = decodeOceanWakeSettings(wakeValue);
    REQUIRE(decoded.ok());
    auto wake = OceanWake::create(decoded.value());
    REQUIRE(wake.ok());
    CHECK(wake.value()->settings().center.x == 3.0);
    wakeValue.set("center", Value::Array{Value(1)});
    CHECK(!decodeOceanWakeSettings(wakeValue).ok());
    CHECK(decoded.value().center.x == 3.0);
    wakeValue = encodeOceanWakeSettings(original);
    wakeValue.set("resolution", Value(16.5));
    CHECK(!decodeOceanWakeSettings(wakeValue).ok());
    wakeValue = encodeOceanWakeSettings(original);
    wakeValue.set("unexpected", Value(true));
    CHECK(!decodeOceanWakeSettings(wakeValue).ok());
    OceanHullSettings hull;
    hull.cells       = {{{1.0, -0.25, 2.0}, 0.5, 0.2}};
    auto hullValue   = encodeOceanHullSettings(hull);
    auto hullDecoded = decodeOceanHullSettings(hullValue);
    REQUIRE(hullDecoded.ok());
    CHECK(hullDecoded.value().cells.size() == 1);
    CHECK(hullDecoded.value().cells[0].center.z == 2.0);
    CHECK(hullDecoded.value().cells[0].volume == 0.2);
    hullValue.find("cells")->at(0).set("unknown", Value(0));
    CHECK(!decodeOceanHullSettings(hullValue).ok());
    hullValue = encodeOceanHullSettings(hull);
    hullValue.set("cells", Value::Array(65, hullValue.find("cells")->at(0)));
    CHECK(!decodeOceanHullSettings(hullValue).ok());
    hullValue = encodeOceanHullSettings(hull);
    hullValue.set("drag", Value::Array{Value(1.0), Value(2.0), Value(std::numeric_limits<double>::infinity())});
    CHECK(!decodeOceanHullSettings(hullValue).ok());
    hullValue = encodeOceanHullSettings(hull);
    std::get<Value::Object>(hullValue.storage()).erase("waterDensity");
    CHECK(!decodeOceanHullSettings(hullValue).ok());
}
