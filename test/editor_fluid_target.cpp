#include "editor/EditorFluidTarget.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::editor;

TEST_CASE("editor.fluids.settings_are_reversible_persistent_and_budgeted") {
    FluidSimulationTarget fluid("waterfall");
    SelectionSnapshot selection;
    selection.items.push_back({SelectionDomain::Asset, TargetId("waterfall"), StableId("settings"), "fluid"});
    auto radius = fluid.makeSet(selection, PropertyPath("particleRadius"), 0.08, PropertySetMode::Absolute);
    REQUIRE(radius.value); CHECK(fluid.applyDomainOperation(*radius.value).isAccepted());
    auto support = fluid.makeSet(selection, PropertyPath("supportRadius"), 0.32, PropertySetMode::Absolute);
    REQUIRE(support.value); CHECK(fluid.applyDomainOperation(*support.value).isAccepted());
    CHECK(fluid.validate().empty());
    const auto preview = fluid.previewBudget();
    CHECK_EQ(static_cast<int>(preview.status), static_cast<int>(EditorStatus::Applied));
    CHECK(preview.estimatedBytes > 0U); CHECK(preview.estimatedNeighborChecks > 0U);
    DomainOperation undo = *support.value; undo.payload = support.value->inverse;
    CHECK(fluid.applyDomainOperation(undo).isAccepted());
    CHECK_EQ(fluid.settings().supportRadius, 0.20);
    FluidSimulationTarget restored("waterfall"); CHECK(restored.loadSnapshot(fluid.snapshotValue()).isAccepted());
    CHECK_EQ(restored.snapshotValue(), fluid.snapshotValue());
    CHECK_EQ(static_cast<int>(fluid.previewBudget(1, 1).status), static_cast<int>(EditorStatus::Rejected));
}
