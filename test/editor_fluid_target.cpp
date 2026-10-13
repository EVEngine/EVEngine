#include "fluids/VolumeFluid.h"
#include "fluids/editing/FluidTarget.h"
#include "fluids/editing/VolumeFluidTarget.h"
#include "fluids/editor/FluidsEditorModule.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// editor_fluid_target.cpp
using namespace eve::fluids_editing;
using namespace eve::editing;

TEST_CASE("editor.fluids.settings_are_reversible_persistent_and_budgeted") {
    FluidSimulationTarget fluid("waterfall");
    SelectionSnapshot     selection;
    selection.items.push_back({SelectionDomain::Asset, TargetId("waterfall"), StableId("settings"), "fluid"});
    auto radius = fluid.makeSet(selection, PropertyPath("particleRadius"), 0.08, PropertySetMode::Absolute);
    REQUIRE(radius.ok());
    CHECK(fluid.applyDomainOperation(radius.value()).ok());
    auto support = fluid.makeSet(selection, PropertyPath("supportRadius"), 0.32, PropertySetMode::Absolute);
    REQUIRE(support.ok());
    CHECK(fluid.applyDomainOperation(support.value()).ok());
    CHECK(fluid.validate().empty());
    const auto preview = fluid.previewBudget();
    CHECK_EQ(static_cast<int>(preview.status), static_cast<int>(EditorStatus::Applied));
    CHECK(preview.estimatedBytes > 0U);
    CHECK(preview.estimatedNeighborChecks > 0U);
    DomainOperation undo = support.value();
    undo.payload         = support.value().inverse;
    CHECK(fluid.applyDomainOperation(undo).ok());
    CHECK_EQ(fluid.settings().supportRadius, 0.20);
    FluidSimulationTarget restored("waterfall");
    CHECK(restored.loadSnapshot(fluid.snapshotValue()).ok());
    CHECK_EQ(restored.snapshotValue(), fluid.snapshotValue());
    CHECK_EQ(static_cast<int>(fluid.previewBudget(1, 1).status), static_cast<int>(EditorStatus::Rejected));
}

// editor_fluids_module.cpp
using namespace eve::editor;
using namespace eve::fluids_editor;

TEST_CASE("editor.fluids_factory_creates_all_schema_targets") {
    FluidsAutomationTargetFactory factory;
    for (const auto* type : {"fluid-simulation", "surface-fluid", "volume-fluid"}) {
        REQUIRE(factory.supports(type));
        auto created = factory.create(TargetId(std::string("target-") + type), type, {});
        REQUIRE(created.ok());
        REQUIRE(created.value().target != nullptr);
        CHECK_EQ(created.value().target->describe().type, std::string(type));
    }
    CHECK(!factory.supports("unknown"));
    CHECK_EQ(static_cast<int>(factory.create(TargetId("unknown"), "unknown", {}).code()),
             static_cast<int>(EditorStatus::Unsupported));
}

TEST_CASE("editor.fluids_factory_loads_volume_snapshot_atomically") {
    eve::fluids_editing::VolumeFluidTarget source("source");
    EditorValue::Object                    request;
    request.emplace("snapshot", source.snapshotValue());

    FluidsAutomationTargetFactory factory;
    auto                          created = factory.create(TargetId("volume"), "volume-fluid", request);
    REQUIRE(created.ok());
    auto* volume = dynamic_cast<eve::fluids_editing::VolumeFluidTarget*>(created.value().target.get());
    REQUIRE(volume != nullptr);
    CHECK_EQ(volume->snapshotValue(), source.snapshotValue());

    auto* root = request.at("snapshot").getIf<EditorValue::Object>();
    REQUIRE(root != nullptr);
    (*root)["unknown"] = true;
    CHECK_EQ(static_cast<int>(factory.create(TargetId("bad"), "volume-fluid", request).code()),
             static_cast<int>(EditorStatus::Unsupported));
}

// editor_volume_fluid_target.cpp
using namespace eve::editing;
using namespace eve::fluids_editing;

namespace {
SelectionSnapshot selected(const char* target) {
    SelectionSnapshot value;
    value.items.push_back({SelectionDomain::Asset, TargetId(target), StableId("volume"), "fluids.volume"});
    return value;
}
}  // namespace

TEST_CASE("editor.volume_fluid_schema_edits_are_reversible_and_budgeted") {
    VolumeFluidTarget target("tank");
    const auto        selection = selected("tank");
    CHECK_EQ(target.schema(selection).properties.size(), static_cast<std::size_t>(7));
    auto iterations = target.makeSet(selection, PropertyPath("iterations"), std::int64_t{2}, PropertySetMode::Absolute);
    REQUIRE(iterations.ok());
    REQUIRE(target.applyDomainOperation(iterations.value()).ok());
    CHECK_EQ(target.settings().iterations, 2U);
    CHECK_EQ(static_cast<int>(target.previewBudget().status), static_cast<int>(EditorStatus::Applied));
    DomainOperation undo = iterations.value();
    undo.payload         = iterations.value().inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    CHECK_EQ(target.settings().iterations, 5U);
    CHECK_EQ(static_cast<int>(target.previewBudget(1, 1).status), static_cast<int>(EditorStatus::Rejected));
}

TEST_CASE("editor.volume_fluid_snapshot_is_strict_and_atomic") {
    VolumeFluidTarget target("tank");
    EditorValue       snapshot = target.snapshotValue();
    auto*             root     = snapshot.getIf<EditorValue::Object>();
    REQUIRE(root != nullptr);
    (*root)["unknown"] = true;
    const auto before  = target.settings();
    CHECK_EQ(static_cast<int>(target.loadSnapshot(snapshot).code()), static_cast<int>(EditorStatus::Unsupported));
    CHECK_EQ(target.settings().capacity, before.capacity);
    CHECK_EQ(target.settings().spacing, before.spacing);
    VolumeFluidTarget restored("restored");
    REQUIRE(restored.loadSnapshot(target.snapshotValue()).ok());
    CHECK_EQ(restored.snapshotValue(), target.snapshotValue());
}

TEST_CASE("editor.volume_fluid_runtime_publish_uses_atomic_restore") {
    eve::fluids::VolumeFluidSettings settings;
    settings.capacity = 16;
    auto created      = eve::fluids::VolumeFluid::create(settings);
    REQUIRE(created.ok());
    VolumeFluidTarget target("tank");
    const auto        selection = selected("tank");
    auto iterations = target.makeSet(selection, PropertyPath("iterations"), std::int64_t{2}, PropertySetMode::Absolute);
    REQUIRE(iterations.ok());
    REQUIRE(target.applyDomainOperation(iterations.value()).ok());
    REQUIRE(VolumeFluidRuntimeApplier().apply(target, created.value().get()).ok());
    const auto applied = created.value()->snapshot();
    CHECK_EQ(applied.settings.capacity, 8192U);
    CHECK_EQ(applied.settings.iterations, 2U);
    CHECK_EQ(applied.settings.spacing, 0.1f);
    CHECK_EQ(static_cast<int>(VolumeFluidRuntimeApplier().apply(target, nullptr).code()),
             static_cast<int>(EditorStatus::Rejected));
}
