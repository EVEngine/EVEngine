#include "lighting_editing/LightingTarget.h"

#include "daynight/DayNight.h"
#include "graphics/Light.h"
#include "weather/Weather.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::lighting_editing;
using namespace eve::editing;

namespace {

SelectionSnapshot selection(const LightingPropertyTargetBase& target) {
    SelectionSnapshot result;
    result.items.push_back({SelectionDomain::Scene, TargetId(target.targetId()), StableId("lighting"),
                            target.describe().type});
    return result;
}

void set(LightingPropertyTargetBase& target, const char* path, EditorValue value) {
    auto operation = target.makeSet(selection(target), PropertyPath(path), value, PropertySetMode::Absolute);
    REQUIRE(operation.value);
    REQUIRE(target.applyDomainOperation(*operation.value).isAccepted());
}

}  // namespace

TEST_CASE("editor.lighting.light3d_properties_validate_snapshot_and_apply_runtime") {
    Light3DDocumentTarget document("key-light");
    set(document, "light.type", "dir");
    set(document, "transform.direction", EditorValue::Array{0.0, -1.0, 0.0});
    set(document, "light.color", EditorValue::Array{1.0, 0.8, 0.6, 1.0});
    set(document, "light.intensity", 4.0);
    set(document, "shadow.cast", true);
    set(document, "shadow.strength", 0.75);
    CHECK(document.validate().empty());

    eve::graphics::Light3D* light = eve::graphics::Light3D::createLight();
    REQUIRE(light);
    Light3DRuntimeApplier applier;
    REQUIRE(applier.apply(document, light).isAccepted());
    CHECK_EQ(light->getType(), "dir");
    CHECK_EQ(light->getDirY(), -1.f);
    CHECK_EQ(light->data()->intensity, 4.f);
    CHECK(light->getCastShadow());
    CHECK_EQ(light->getShadowStrength(), 0.75f);

    const EditorValue snapshot = document.snapshotValue();
    Light3DDocumentTarget restored("restored-light");
    REQUIRE(restored.loadSnapshot(snapshot).isAccepted());
    CHECK_EQ(restored.snapshotValue(), snapshot);
}

TEST_CASE("editor.lighting.light3d_rejects_zero_direction_before_runtime_mutation") {
    Light3DDocumentTarget document("invalid-light");
    set(document, "transform.direction", EditorValue::Array{0.0, 0.0, 0.0});
    REQUIRE(!document.validate().empty());
    eve::graphics::Light3D* light = eve::graphics::Light3D::createLight();
    REQUIRE(light);
    light->setPosition(9.f, 8.f, 7.f);
    const auto rejected = Light3DRuntimeApplier{}.apply(document, light);
    CHECK_EQ(static_cast<int>(rejected.status), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(light->getX(), 9.f);
}

TEST_CASE("editor.lighting.environment_applies_daynight_and_weather_modes") {
    EnvironmentDocumentTarget daynightDocument("sky");
    set(daynightDocument, "environment.mode", "daynight");
    set(daynightDocument, "daynight.time", 18.5);
    set(daynightDocument, "daynight.speed", 2.0);
    set(daynightDocument, "daynight.turbidity", 6.0);
    set(daynightDocument, "environment.exposure", 1.8);
    eve::daynight::DayNight daynight;
    EnvironmentRuntimeApplier applier;
    REQUIRE(applier.applyDayNight(daynightDocument, &daynight).isAccepted());
    CHECK_EQ(daynight.getTimeOfDay(), 18.5f);
    CHECK_EQ(daynight.getSpeed(), 2.f);
    CHECK_EQ(daynight.getTurbidity(), 6.f);
    CHECK_EQ(daynight.getSkyExposure(), 1.8f);

    EnvironmentDocumentTarget weatherDocument("weather");
    set(weatherDocument, "environment.mode", "weather");
    set(weatherDocument, "weather.preset", "storm");
    set(weatherDocument, "weather.intensity", 0.9);
    set(weatherDocument, "weather.wind-speed", 20.0);
    set(weatherDocument, "weather.lightning", true);
    eve::weather::Weather weather;
    REQUIRE(applier.applyWeather(weatherDocument, &weather).isAccepted());
    CHECK_EQ(weather.getPreset(), "storm");
    CHECK_EQ(weather.getIntensity(), 0.9f);
    CHECK_EQ(weather.getWindSpeed(), 20.f);
    CHECK(weather.isLightningEnabled());
    CHECK_EQ(static_cast<int>(applier.applyDayNight(weatherDocument, &daynight).status),
             static_cast<int>(EditorStatus::Rejected));
}

TEST_CASE("editor.lighting.environment_detects_multiple_environment_owners") {
    EnvironmentDocumentTarget document("owners");
    set(document, "environment.mode", "daynight");
    set(document, "weather.environment-enabled", true);
    const auto diagnostics = document.validate();
    REQUIRE_EQ(diagnostics.size(), size_t{1});
    CHECK_EQ(diagnostics.front().rule.value(), "editor.environment.multiple-owners");
}
