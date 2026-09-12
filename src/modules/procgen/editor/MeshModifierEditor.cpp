#include "procgen/editor/MeshModifierEditor.h"

#include "editor/EditorProtocol.h"

#include <utility>

namespace eve::procgen_editor {
namespace {
procgen_editing::EditorResult<void> rejected(const char* rule, const char* message) {
    return eve::editing::failed<void>(procgen_editing::EditorStatus::Rejected,
                                      eve::editing::RuleId(rule), message);
}
}
MeshModifierEditor::MeshModifierEditor(std::string targetId) : targetId_(std::move(targetId)) {}

procgen_editing::EditorResult<void> MeshModifierEditor::configureWorkspace(editor::EditorWorkspace& workspace) const {
    if (targetId_.empty()) return rejected("editor.mesh-modifier.target", "Mesh modifier target id is empty");
    editor::EditorWorkspace candidate = workspace;
    struct Panel { const char* id; const char* title; const char* region; const char* context; int order; };
    constexpr Panel panels[] = {
        {"meshModifier.graph", "Modifier Graph", "left", "graph", 100},
        {"meshModifier.viewport", "Mesh Preview", "center", "mesh-preview", 100},
        {"meshModifier.inspector", "Modifier Inspector", "right", "inspector", 100},
        {"meshModifier.spline", "Spline", "bottom", "spline", 100},
        {"meshModifier.sculpt", "Sculpt & Damage", "bottom", "sculpt", 110},
        {"meshModifier.uvPaint", "UV Paint", "bottom", "uv-paint", 120},
    };
    for (const auto& panel : panels) {
        editor::WorkspacePanelDescriptor descriptor{panel.id, panel.title, panel.region, "procgen.mesh-modifier",
                                                     panel.context, panel.order, true, true};
        auto added = candidate.registerPanel(std::move(descriptor));
        if (!added.ok()) return rejected("editor.mesh-modifier.workspace-conflict", "Could not install mesh modifier workspace");
    }
    auto activated = candidate.activatePanel(editor::StableId("meshModifier.viewport"));
    if (!activated.ok()) return rejected("editor.mesh-modifier.workspace-activate", "Could not activate mesh preview");
    auto selected = candidate.selectItem("meshModifier", {editor::SelectionDomain::Asset, editor::TargetId(targetId_),
                                                           editor::StableId(targetId_), "mesh"}, false);
    if (!selected.ok()) return rejected("editor.mesh-modifier.workspace-selection", "Could not select mesh target");
    workspace = std::move(candidate);
    return eve::editing::applied<void>();
}

procgen_editing::EditorResult<void> MeshModifierEditor::activateTool(editor::EditorWorkspace& workspace,
                                                                     std::string tool) {
    std::string panel;
    if (tool == "graph") panel = "meshModifier.graph";
    else if (tool == "spline") panel = "meshModifier.spline";
    else if (tool == "sculpt") panel = "meshModifier.sculpt";
    else if (tool == "uvPaint") panel = "meshModifier.uvPaint";
    else return rejected("editor.mesh-modifier.tool", "Unknown mesh modifier tool");
    editor::EditorWorkspace candidate = workspace;
    auto activated = candidate.activatePanel(editor::StableId(panel));
    if (!activated.ok()) return rejected("editor.mesh-modifier.tool-panel", "Mesh modifier tool panel is unavailable");
    auto mode = candidate.setModeId(editor::StableId("meshModifier." + tool));
    if (!mode.ok()) return rejected("editor.mesh-modifier.tool-mode", "Could not set mesh modifier tool mode");
    auto focus = candidate.focusItem("meshModifier", editor::StableId(panel), editor::StableId(targetId_));
    if (!focus.ok()) return rejected("editor.mesh-modifier.tool-focus", "Could not focus mesh modifier tool");
    workspace = std::move(candidate); activeTool_ = std::move(tool);
    return eve::editing::applied<void>();
}

procgen_editing::EditorResult<void> MeshModifierEditor::observeRevision(std::string document, std::uint64_t revision) {
    std::uint64_t* destination = nullptr;
    if (document == "graph") destination = &graphRevision_;
    else if (document == "spline") destination = &splineRevision_;
    else if (document == "mesh") destination = &meshRevision_;
    else if (document == "paint") destination = &paintRevision_;
    else return rejected("editor.mesh-modifier.document", "Unknown mesh modifier document");
    if (revision < *destination) return rejected("editor.mesh-modifier.stale-revision", "Document revision cannot move backwards");
    *destination = revision;
    return eve::editing::applied<void>();
}
}  // namespace eve::procgen_editor
