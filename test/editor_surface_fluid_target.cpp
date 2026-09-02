#include "fluids_editing/SurfaceFluidTarget.h"

#include "fluids/SurfaceDropletSimulation.h"
#include "fluids/SurfaceFluidRenderData.h"
#include "fluids/SurfaceWetnessField.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::fluids_editing;
using namespace eve::editing;

namespace {
SelectionSnapshot selection(const char* target) {
    SelectionSnapshot value;
    value.items.push_back({SelectionDomain::Asset, TargetId(target), StableId("surface"),
                           "fluids.surface"});
    return value;
}
}

TEST_CASE("editor.surface_fluid_exposes_reversible_droplet_wetness_and_material_properties") {
    SurfaceFluidTarget target("waterfall");
    const auto selected = selection("waterfall");
    CHECK_EQ(target.schema(selected).properties.size(), static_cast<std::size_t>(22));
    auto contact = target.makeSet(selected, PropertyPath("contactAngleDegrees"), 105.0,
                                  PropertySetMode::Absolute);
    REQUIRE(contact.ok()); REQUIRE(target.applyDomainOperation(contact.value()).ok());
    CHECK_EQ(target.settings().contactAngleDegrees, 105.0);
    auto wetness = target.makeSet(selected, PropertyPath("maxWetness"), 2.0,
                                  PropertySetMode::Absolute);
    REQUIRE(wetness.ok()); REQUIRE(target.applyDomainOperation(wetness.value()).ok());
    CHECK_EQ(target.settings().maxWetness, 2.0);
    DomainOperation undo = wetness.value(); undo.payload = wetness.value().inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    CHECK_EQ(target.settings().maxWetness, 1.0);
    CHECK_EQ(static_cast<int>(target.makeSet(selected, PropertyPath("maxAspectRatio"), 0.5,
        PropertySetMode::Absolute).code()), static_cast<int>(EditorStatus::Rejected));
}

TEST_CASE("editor.surface_fluid_snapshot_load_is_atomic_and_material_warnings_are_structured") {
    SurfaceFluidTarget source("source");
    const auto selected = selection("source");
    auto roughness = source.makeSet(selected, PropertyPath("wetRoughness"), 0.8,
                                    PropertySetMode::Absolute);
    REQUIRE(roughness.ok()); REQUIRE(source.applyDomainOperation(roughness.value()).ok());
    CHECK(!source.validate().empty());
    SurfaceFluidTarget restored("restored");
    REQUIRE(restored.loadSnapshot(source.snapshotValue()).ok());
    CHECK_EQ(restored.settings().wetRoughness, 0.8);
    const auto before = restored.settings();
    EditorValue invalid = source.snapshotValue();
    auto* root = invalid.getIf<EditorValue::Object>();
    auto* settings = (*root)["settings"].getIf<EditorValue::Object>();
    (*settings)["contactAngleDegrees"] = 180.0;
    CHECK_EQ(static_cast<int>(restored.loadSnapshot(invalid).code()),
             static_cast<int>(EditorStatus::Rejected));
    CHECK(restored.settings() == before);
}

TEST_CASE("editor.surface_fluid_runtime_bridge_rejects_incomplete_live_targets") {
    SurfaceFluidTarget target("waterfall");
    SurfaceFluidRuntimeApplier bridge;
    CHECK_EQ(static_cast<int>(bridge.apply(target, nullptr, nullptr, nullptr).code()),
             static_cast<int>(EditorStatus::Rejected));
}

TEST_CASE("editor.surface_fluid_runtime_bridge_publishes_all_parameter_groups") {
    SurfaceFluidTarget target("waterfall");
    const auto selected = selection("waterfall");
    auto friction = target.makeSet(selected, PropertyPath("friction"), 3.25,
                                   PropertySetMode::Absolute);
    REQUIRE(friction.ok()); REQUIRE(target.applyDomainOperation(friction.value()).ok());
    auto diffusion = target.makeSet(selected, PropertyPath("diffusion"), 0.4,
                                    PropertySetMode::Absolute);
    REQUIRE(diffusion.ok()); REQUIRE(target.applyDomainOperation(diffusion.value()).ok());
    auto darkening = target.makeSet(selected, PropertyPath("wetDarkening"), 0.35,
                                    PropertySetMode::Absolute);
    REQUIRE(darkening.ok()); REQUIRE(target.applyDomainOperation(darkening.value()).ok());
    eve::fluids::SurfaceDropletSimulation simulation(nullptr);
    eve::fluids::SurfaceFluidRenderParams render;
    eve::fluids::SurfaceWetnessParams wetness;
    REQUIRE(SurfaceFluidRuntimeApplier().apply(target, &simulation, &render, &wetness).ok());
    CHECK_EQ(simulation.params().friction, 3.25f);
    CHECK_EQ(wetness.diffusion, 0.4f);
    CHECK_EQ(render.wetMaterial.wetDarkening, 0.35f);
}
