#include "procgen_editor/ProcgenScriptEditor.h"
#include "editor/EditorWorkspace.h"
#include "procgen/PointSet.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>

using eve::procgen_editor::ProcgenScriptEditor;
using eve::editor::EditorWorkspace;
using eve::procgen_editing::EditorValue;
using eve::procgen_editing::ProcgenScriptModuleSpec;

static ProcgenScriptModuleSpec forestSpec(std::string extraKey = {}) {
    ProcgenScriptModuleSpec spec;
    spec.uri         = "game:/generators/forest.nut";
    spec.id          = "forest";
    spec.displayName = "Forest scatter";
    spec.kind        = "points";
    spec.params.push_back(eve::procgen::ParamDescriptor::integer("seed", "Seed", 42, 1, 9999, 1));
    spec.params.push_back(eve::procgen::ParamDescriptor::floating("density", "Density", 0.8f, 0.1f, 1.0f, 0.05f));
    spec.params.push_back(eve::procgen::ParamDescriptor::floating("pruneRadius", "Prune", 32.f, 4.f, 80.f, 1.f));
    if (!extraKey.empty())
        spec.params.push_back(eve::procgen::ParamDescriptor::integer(extraKey, extraKey, 1, 1, 8, 1));
    return spec;
}

static eve::procgen::PointSet makePoints(int count) {
    eve::procgen::PointSet points;
    for (int i = 0; i < count; ++i) points.add(static_cast<float>(i), 0.f, static_cast<float>(i * 2));
    return points;
}

TEST_CASE("editor.procgen.script_editor_installs_workspace_and_commits_params") {
    ProcgenScriptEditor editor("preview.forest");
    EditorWorkspace     workspace("procgen.preview", "Procgen Editor");
    REQUIRE(editor.configureWorkspace(workspace).ok());
    CHECK_EQ(workspace.getPanelCount(), 4);
    CHECK_EQ(workspace.getPanelId(0), std::string("procgen.modules"));
    CHECK_EQ(workspace.getPanelCapability(0), std::string("procgen.script"));
    CHECK_EQ(workspace.getPanelContext(0), std::string("list"));
    CHECK_EQ(workspace.getPanelContext(1), std::string("preview"));
    CHECK_EQ(workspace.getPanelContext(2), std::string("inspector"));
    CHECK_EQ(workspace.getPanelContext(3), std::string("stages"));

    REQUIRE(editor.loadModule(forestSpec()).ok());
    CHECK_EQ(editor.paramCount(), 3);
    CHECK_EQ(editor.getFloat("density"), 0.8f);
    CHECK(editor.isDirty());

    eve::procgen::PointSet first = makePoints(4);
    REQUIRE(editor.publishPreview(&first, "trees", editor.revision()).ok());
    CHECK_EQ(editor.pointCount(), 4);
    CHECK_EQ(editor.previewRevision(), editor.revision());
    CHECK(!editor.isDirty());

    const auto revision = editor.revision();
    REQUIRE(editor.setFloat("density", 0.5).ok());
    CHECK(editor.isDirty());
    CHECK_EQ(editor.pointCount(), 4);
    CHECK_EQ(editor.previewRevision(), revision);
    CHECK(std::abs(editor.getFloat("density") - 0.5f) < 0.0001f);
    REQUIRE(editor.undo().ok());
    CHECK(std::abs(editor.getFloat("density") - 0.8f) < 0.0001f);
    CHECK(editor.isDirty());
}

TEST_CASE("editor.procgen.script_editor_keeps_preview_on_failed_rebuild_and_stale_publish") {
    ProcgenScriptEditor editor("preview.forest");
    REQUIRE(editor.loadModule(forestSpec()).ok());
    eve::procgen::PointSet first = makePoints(3);
    REQUIRE(editor.publishPreview(&first, "trees", editor.revision()).ok());
    const int before = editor.pointCount();
    const auto previewRevision = editor.previewRevision();

    REQUIRE(editor.setFloat("density", 0.4).ok());
    REQUIRE(editor.failPreview("generate exploded", editor.revision()).ok());
    CHECK_EQ(editor.pointCount(), before);
    CHECK_EQ(editor.previewRevision(), previewRevision);
    CHECK(!editor.isDirty());
    CHECK_EQ(editor.previewFailureSummary(), std::string("generate exploded"));

    eve::procgen::PointSet next = makePoints(8);
    auto stale = editor.publishPreview(&next, "trees", previewRevision);
    CHECK(!stale.ok());
    CHECK_EQ(editor.pointCount(), before);
}

TEST_CASE("editor.procgen.script_editor_reload_drops_unknown_and_fills_defaults") {
    ProcgenScriptEditor editor("preview.forest");
    auto                first = forestSpec("scatter");
    REQUIRE(editor.loadModule(first).ok());
    REQUIRE(editor.setInt("scatter", 4).ok());
    CHECK_EQ(editor.getInt("scatter"), 4);

    auto second = forestSpec();
    second.params.push_back(eve::procgen::ParamDescriptor::floating("spacing", "Spacing", 28.f, 8.f, 64.f, 1.f));
    REQUIRE(editor.loadModule(second).ok());
    CHECK_EQ(editor.paramCount(), 4);
    CHECK_EQ(editor.getInt("scatter"), 0);
    CHECK(std::abs(editor.getFloat("spacing") - 28.f) < 0.0001f);
    CHECK(std::abs(editor.getFloat("density") - 0.8f) < 0.0001f);
}

TEST_CASE("editor.procgen.script_editor_rejects_out_of_range_and_over_budget") {
    ProcgenScriptEditor editor("preview.forest");
    REQUIRE(editor.loadModule(forestSpec()).ok());
    eve::procgen::PointSet first = makePoints(2);
    REQUIRE(editor.publishPreview(&first, "trees", editor.revision()).ok());
    const auto revision = editor.previewRevision();
    auto       rejected = editor.setFloat("density", 2.0);
    CHECK(!rejected.ok());
    CHECK_EQ(editor.previewRevision(), revision);
    CHECK_EQ(editor.pointCount(), 2);

    REQUIRE(editor.setPointBudget(1).ok());
    eve::procgen::PointSet huge = makePoints(3);
    REQUIRE(editor.setFloat("density", 0.2).ok());
    auto over = editor.publishPreview(&huge, "trees", editor.revision());
    CHECK(!over.ok());
    CHECK_EQ(editor.pointCount(), 2);
}
