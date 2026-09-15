#include "zeroerr/unittest.h"

#include "graphics/WaterSystem.h"

using namespace eve::graphics;

TEST_CASE("graphics.WaterSystem.infiniteIntervalAndSeaLevel") {
    WaterSystemState state;
    WaterSystemSettings settings;
    WaterSceneConditions scene;
    auto initialized = initializeWaterSystem(state, settings, scene, 7);
    REQUIRE(static_cast<bool>(initialized));

    auto first = advanceWaterSystem(state, settings, scene, 4.0F, -3.0F, 0.25F, 9);
    REQUIRE(static_cast<bool>(first));
    CHECK(!first.value());
    CHECK(state.positionX == 4.0F);
    CHECK(state.positionY == 25.0F);
    CHECK(state.positionZ == -3.0F);

    auto second = advanceWaterSystem(state, settings, scene, 8.0F, 2.0F, 0.3F, 9);
    REQUIRE(static_cast<bool>(second));
    CHECK(second.value());
    CHECK(state.refreshRevision == 1);

    auto level = updateWaterSeaLevel(state, 31.5F, true);
    REQUIRE(static_cast<bool>(level));
    CHECK(level.value());
    CHECK(state.positionY == 31.5F);
    CHECK(state.refreshRevision == 2);
}

TEST_CASE("graphics.WaterSystem.sceneConditionsAndAtomicFailure") {
    WaterSystemState state;
    WaterSystemSettings settings;
    settings.autoUpdateMode = WaterAutoUpdateMode::SceneConditions;
    settings.ignoreSceneConditions = false;
    WaterSceneConditions scene;
    auto initialized = initializeWaterSystem(state, settings, scene, 0);
    REQUIRE(static_cast<bool>(initialized));

    WaterSceneConditions changedScene = scene;
    changedScene.sunIntensity = 2.0F;
    auto refresh = advanceWaterSystem(state, settings, changedScene, 0.0F, 0.0F, 0.0F, 8);
    REQUIRE(static_cast<bool>(refresh));
    CHECK(refresh.value());
    CHECK(state.sceneCheckFrames == 7);

    const WaterSystemState before = state;
    auto invalid = advanceWaterSystem(state, settings, changedScene, 0.0F, 0.0F, -1.0F, 8);
    CHECK(!invalid);
    CHECK(state.refreshRevision == before.refreshRevision);
    CHECK(state.positionX == before.positionX);
    CHECK(state.sceneCheckFrames == before.sceneCheckFrames);
}
