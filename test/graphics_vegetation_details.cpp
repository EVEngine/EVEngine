#include "graphics/VegetationDetails.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::graphics;

TEST_CASE("graphics.vegetationDetailsProjectsGlobalStateAtomically") {
    PbrSurface baseSurface;
    baseSurface.specularFactor = .25f;
    VegetationMotion baseMotion;
    baseMotion.time = 7;
    VegetationDetailSettings settings;
    settings.colorsLayer          = 2;
    settings.extrasLayer          = 3;
    settings.motionLayer          = 4;
    settings.globalColor          = .6f;
    settings.globalAlpha          = .7f;
    settings.globalOverlay        = .8f;
    settings.globalWetness        = .9f;
    settings.colorMaskMinimum     = .1f;
    settings.colorMaskMaximum     = .2f;
    settings.overlayMaskMinimum   = .3f;
    settings.overlayMaskMaximum   = .4f;
    settings.alphaThreshold       = .75f;
    settings.motionHighlight      = {2, 3, 4};
    settings.bendingAmplitude     = 1.2f;
    settings.bendingSpeed         = 11;
    settings.bendingScale         = 5;
    settings.flutterAmplitude     = .3f;
    settings.flutterSpeed         = 17;
    settings.flutterScale         = 8;
    settings.interactionAmplitude = 1.5f;
    settings.perspectivePush      = 1;
    settings.perspectiveNoise     = 2;
    settings.perspectiveAngle     = 3;
    auto projected                = configureVegetationDetails(baseSurface, baseMotion, settings);
    REQUIRE(projected.ok());
    REQUIRE_EQ(projected.value().surface.vegetationColors.layer, uint32_t(2));
    REQUIRE_EQ(projected.value().surface.vegetationExtras.layer, uint32_t(3));
    REQUIRE_EQ(projected.value().surface.vegetationMotion.layer, uint32_t(4));
    REQUIRE_EQ(projected.value().motion.motionLayer, uint8_t(4));
    REQUIRE_EQ(projected.value().surface.vegetationColor.colorsCoverage, .6f);
    REQUIRE_EQ(projected.value().surface.vegetationAlpha.global, .7f);
    REQUIRE_EQ(projected.value().surface.motionHighlightColor[2], 4.f);
    REQUIRE_EQ(projected.value().surface.vegetationMotion.bending, 1.2f);
    REQUIRE_EQ(projected.value().motion.flutterSpeed, 17.f);
    REQUIRE_EQ(projected.value().motion.interaction, 1.5f);
    REQUIRE_EQ(projected.value().colorMaskMinimum, .1f);
    REQUIRE_EQ(projected.value().overlayMaskMaximum, .4f);
    REQUIRE_EQ(projected.value().alphaThresholdOffset, .25f);
    REQUIRE_EQ(projected.value().surface.vegetationColor.globalColorMaskMinimum, .1f);
    REQUIRE_EQ(projected.value().surface.vegetationColor.globalColorMaskMaximum, .2f);
    REQUIRE_EQ(projected.value().surface.vegetationColor.globalOverlayMaskMinimum, .3f);
    REQUIRE_EQ(projected.value().surface.vegetationColor.globalOverlayMaskMaximum, .4f);
    REQUIRE_EQ(projected.value().surface.vegetationColor.globalAlphaThresholdOffset, .25f);
    REQUIRE_EQ(projected.value().surface.specularFactor, .25f);
    REQUIRE_EQ(projected.value().motion.time, 7.0);

    settings.motionLayer = 9;
    REQUIRE(!configureVegetationDetails(baseSurface, baseMotion, settings).ok());
    REQUIRE_EQ(baseSurface.specularFactor, .25f);
    REQUIRE_EQ(baseMotion.time, 7.0);
}

TEST_CASE("graphics.vegetationDetailsPersistenceIsExactAndTransactional") {
    VegetationDetailSettings settings;
    settings.colorsLayer          = 8;
    settings.extrasLayer          = 7;
    settings.motionLayer          = 6;
    settings.globalColor          = .25f;
    settings.overlayMaskMinimum   = .15f;
    settings.overlayMaskMaximum   = .85f;
    settings.motionHighlight      = {2.f, 3.f, 4.f};
    settings.interactionAmplitude = 1.25f;
    auto document                 = snapshotVegetationDetails(settings);
    REQUIRE(document.ok());
    auto restored = restoreVegetationDetails(document.value());
    REQUIRE(restored.ok());
    CHECK_EQ(restored.value().colorsLayer, uint8_t(8));
    CHECK_EQ(restored.value().extrasLayer, uint8_t(7));
    CHECK_EQ(restored.value().motionLayer, uint8_t(6));
    CHECK_EQ(restored.value().globalColor, .25f);
    CHECK_EQ(restored.value().overlayMaskMinimum, .15f);
    CHECK_EQ(restored.value().overlayMaskMaximum, .85f);
    CHECK_EQ(restored.value().motionHighlight[2], 4.f);
    CHECK_EQ(restored.value().interactionAmplitude, 1.25f);

    auto unknown                                     = document.value();
    (*unknown.getIf<eve::Value::Object>())["future"] = eve::Value(true);
    CHECK(!restoreVegetationDetails(unknown).ok());
    (*unknown.getIf<eve::Value::Object>()).erase("future");
    (*unknown.getIf<eve::Value::Object>())["version"] = eve::Value(2);
    auto future                                       = restoreVegetationDetails(unknown);
    REQUIRE(!future.ok());
    CHECK_EQ(future.error()->code(), eve::DiagnosticCode::UnknownVersion);

    settings.globalAlpha = 2.f;
    CHECK(!snapshotVegetationDetails(settings).ok());
}
