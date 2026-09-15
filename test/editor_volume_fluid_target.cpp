#include "fluids/editing/VolumeFluidTarget.h"

#include "fluids/VolumeFluid.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

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
