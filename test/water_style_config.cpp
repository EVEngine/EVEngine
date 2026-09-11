#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "graphics/WaterStyleConfig.h"

#include <cmath>

using eve::DiagnosticCode;
using eve::graphics::WaterStyleConfig;

TEST_CASE("graphics.waterStyleConfig.roundTrip") {
    WaterStyleConfig authored;
    authored.deepColor                     = {0.01F, 0.08F, 0.18F};
    authored.shallowColor                  = {0.18F, 0.72F, 0.64F};
    authored.waveSharpness                 = 2.25F;
    authored.depthDistance                 = 7.5F;
    authored.foamStrength                  = 1.2F;
    authored.screenSpaceReflection         = true;
    authored.screenSpaceReflectionStrength = 0.7F;
    authored.refractionStrength            = 0.04F;
    authored.causticsStrength              = 0.65F;
    authored.causticsScale                 = 4.5F;

    auto json = authored.toJson();
    REQUIRE(static_cast<bool>(json));
    auto decoded = WaterStyleConfig::fromJson(json.value());
    REQUIRE(static_cast<bool>(decoded));

    CHECK(std::abs(decoded.value().deepColor.z - 0.18F) < 0.0001F);
    CHECK(std::abs(decoded.value().shallowColor.y - 0.72F) < 0.0001F);
    CHECK(std::abs(decoded.value().waveSharpness - 2.25F) < 0.0001F);
    CHECK(std::abs(decoded.value().depthDistance - 7.5F) < 0.0001F);
    CHECK(std::abs(decoded.value().foamStrength - 1.2F) < 0.0001F);
    CHECK(decoded.value().screenSpaceReflection);
    CHECK(std::abs(decoded.value().refractionStrength - 0.04F) < 0.0001F);
    CHECK(std::abs(decoded.value().causticsStrength - 0.65F) < 0.0001F);
    CHECK(std::abs(decoded.value().causticsScale - 4.5F) < 0.0001F);
}

TEST_CASE("graphics.waterStyleConfig.rejectsUnknownFields") {
    WaterStyleConfig config;
    auto             encoded = config.toValue();
    REQUIRE(static_cast<bool>(encoded));

    auto* root   = encoded.value().getIf<eve::Value::Object>();
    auto* colors = root->at("colors").getIf<eve::Value::Object>();
    colors->emplace("surprise", 1.0);
    auto decoded = WaterStyleConfig::fromValue(encoded.value());
    CHECK(!decoded);
    REQUIRE(decoded.error() != nullptr);
    CHECK(decoded.error()->code() == DiagnosticCode::InvalidArgument);
}

TEST_CASE("graphics.waterStyleConfig.rejectsFutureVersionsAndInvalidRanges") {
    WaterStyleConfig config;
    auto             encoded = config.toValue();
    REQUIRE(static_cast<bool>(encoded));

    auto* root = encoded.value().getIf<eve::Value::Object>();
    root->at("schemaVersion") = static_cast<std::int64_t>(WaterStyleConfig::SchemaVersion + 1);
    auto future = WaterStyleConfig::fromValue(encoded.value());
    CHECK(!future);
    REQUIRE(future.error() != nullptr);
    CHECK(future.error()->code() == DiagnosticCode::UnknownVersion);

    config.depthDistance = 0.0F;
    auto invalid = config.validate();
    CHECK(!invalid);
}
