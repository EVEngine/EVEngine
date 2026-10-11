#include "procgen/editor/ProcgenScriptEditorScriptBindings.h"

#include "editor/EditorProperty.h"
#include "editor/EditorWorkspace.h"
#include "procgen/PointSet.h"
#include "procgen/editor/ProcgenEditorModule.h"
#include "procgen/editor/MeshModifierEditor.h"
#include "procgen/editor/ProcgenScriptEditor.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cstdint>
#include <string>
#include "editor/EditorScriptProjection.h"

namespace eve::procgen_editor {
namespace {

constexpr const char* kBindingSource = "editor.procgen.script.squirrel";

class ScriptProcgenScriptEditor {
public:
    explicit ScriptProcgenScriptEditor(std::string targetId) : editor_(std::move(targetId)) {}

    ProcgenScriptEditor&       editor() noexcept { return editor_; }
    const ProcgenScriptEditor& editor() const noexcept { return editor_; }

private:
    ProcgenScriptEditor editor_;
};

ssq::Table loadModuleFromScript(const editor::ScriptBind& bind, ScriptProcgenScriptEditor* self, const std::string& uri,
                                const std::string& id, const std::string& displayName, const std::string& kind,
                                const ssq::Object& schema) {
    if (!self) return bind.fail(DiagnosticCode::InvalidArgument, "procgen editor must not be null");
    auto converted = script::valueFromSquirrel(schema);
    if (!converted.ok()) return script::projectStatusResult(bind.vm(), converted.status());
    return editor::project(
        bind.vm(), self->editor().loadModule(uri, id, displayName, kind, editor::toEditorValue(converted.value())));
}

}  // namespace

void exposeProcgenScriptEditorScriptBindings(ssq::Table& table, ssq::Class& moduleClass) {
    const editor::ScriptBind bind{table.getHandle(), kBindingSource};
    auto procgenEditor = editor::addScriptClass<ScriptProcgenScriptEditor>(table, "ProcgenScriptEditor");
    auto meshEditor    = editor::addScriptClass<MeshModifierEditor>(table, "MeshModifierEditor");
    editor::registerDirectEditorWorkspace<MeshModifierEditor>(meshEditor, bind,
                                                              "mesh modifier editor and workspace required");
    meshEditor.addFunc("activateTool",
                       [bind](MeshModifierEditor* self, editor::EditorWorkspace* workspace, const std::string& tool) {
                           return bind.checked(self && workspace, "mesh modifier editor and workspace required",
                                               [&] { return self->activateTool(*workspace, tool); });
                       });
    meshEditor.addFunc("observeRevision", [bind](MeshModifierEditor* self, const std::string& document, int revision) {
        return bind.checked(self && revision >= 0, "valid editor and revision required",
                            [&] { return self->observeRevision(document, static_cast<std::uint64_t>(revision)); });
    });
    meshEditor.addFunc("getTargetId", [](MeshModifierEditor* self) { return self ? self->targetId() : std::string{}; });
    meshEditor.addFunc("getActiveTool", [](MeshModifierEditor* self) { return self ? self->activeTool() : std::string{}; });
    editor::registerEditorWorkspace<ScriptProcgenScriptEditor>(procgenEditor, bind,
                                                               "procgen editor and workspace must not be null");
    procgenEditor.addFunc("loadModule",
                          [bind](ScriptProcgenScriptEditor* self, const std::string& uri, const std::string& id,
                                 const std::string& displayName, const std::string& kind, const ssq::Object& schema) {
                              return loadModuleFromScript(bind, self, uri, id, displayName, kind, schema);
                          });
    procgenEditor.addFunc("setInt", [bind](ScriptProcgenScriptEditor* self, const std::string& key, int value) {
        return bind.checked(self, "procgen editor must not be null", [&] { return self->editor().setInt(key, value); });
    });
    procgenEditor.addFunc("setFloat", [bind](ScriptProcgenScriptEditor* self, const std::string& key, float value) {
        return bind.checked(self, "procgen editor must not be null",
                            [&] { return self->editor().setFloat(key, static_cast<double>(value)); });
    });
    procgenEditor.addFunc("setBool", [bind](ScriptProcgenScriptEditor* self, const std::string& key, bool value) {
        return bind.checked(self, "procgen editor must not be null",
                            [&] { return self->editor().setBool(key, value); });
    });
    procgenEditor.addFunc("setString",
                          [bind](ScriptProcgenScriptEditor* self, const std::string& key, const std::string& value) {
                              return bind.checked(self, "procgen editor must not be null",
                                                  [&] { return self->editor().setString(key, value); });
                          });
    procgenEditor.addFunc("publishPreview", [bind](ScriptProcgenScriptEditor* self, procgen::PointSet* points,
                                                   const std::string& stage, int expectedRevision) {
        return bind.checked(self, "procgen editor must not be null", [&] {
            return self->editor().publishPreview(points, stage, static_cast<std::uint64_t>(expectedRevision));
        });
    });
    procgenEditor.addFunc("publishStage",
                          [bind](ScriptProcgenScriptEditor* self, procgen::PointSet* points, const std::string& stage) {
                              return bind.checked(self, "procgen editor must not be null",
                                                  [&] { return self->editor().publishStage(points, stage); });
                          });
    procgenEditor.addFunc(
        "failPreview", [bind](ScriptProcgenScriptEditor* self, const std::string& message, int expectedRevision) {
            return bind.checked(self, "procgen editor must not be null", [&] {
                return self->editor().failPreview(message, static_cast<std::uint64_t>(expectedRevision));
            });
        });
    procgenEditor.addFunc("selectStage", [bind](ScriptProcgenScriptEditor* self, const std::string& stage) {
        return bind.checked(self, "procgen editor must not be null", [&] { return self->editor().selectStage(stage); });
    });
    procgenEditor.addFunc("setPointBudget", [bind](ScriptProcgenScriptEditor* self, int budget) {
        return bind.checked(self, "procgen editor must not be null",
                            [&] { return self->editor().setPointBudget(budget); });
    });
    procgenEditor.addFunc("setLive", [bind](ScriptProcgenScriptEditor* self, bool enabled) {
        return bind.checked(self, "procgen editor must not be null", [&] { return self->editor().setLive(enabled); });
    });
    editor::registerEditorHistory<ScriptProcgenScriptEditor>(procgenEditor, bind, "procgen editor must not be null");
    procgenEditor.addFunc("isDirty", [](ScriptProcgenScriptEditor* self) { return self && self->editor().isDirty(); });
    procgenEditor.addFunc("isLive", [](ScriptProcgenScriptEditor* self) {
        return self && self->editor().isContinuousRebuild();
    });
    procgenEditor.addFunc("getPreviewRevision", [](ScriptProcgenScriptEditor* self) {
        return self ? static_cast<int>(self->editor().previewRevision()) : 0;
    });
    procgenEditor.addFunc("getModuleId", [](ScriptProcgenScriptEditor* self) {
        return self ? self->editor().moduleId() : std::string{};
    });
    procgenEditor.addFunc("getModuleUri", [](ScriptProcgenScriptEditor* self) {
        return self ? self->editor().moduleUri() : std::string{};
    });
    procgenEditor.addFunc("getDisplayName", [](ScriptProcgenScriptEditor* self) {
        return self ? self->editor().displayName() : std::string{};
    });
    procgenEditor.addFunc("getKind", [](ScriptProcgenScriptEditor* self) {
        return self ? self->editor().kind() : std::string{};
    });
    procgenEditor.addFunc("getSelectedStage", [](ScriptProcgenScriptEditor* self) {
        return self ? self->editor().selectedStage() : std::string{};
    });
    procgenEditor.addFunc("getPreviewFailureSummary", [](ScriptProcgenScriptEditor* self) {
        return self ? self->editor().previewFailureSummary() : std::string{};
    });
    procgenEditor.addFunc("getParamCount", [](ScriptProcgenScriptEditor* self) {
        return self ? self->editor().paramCount() : 0;
    });
    procgenEditor.addFunc("getParamKey", [](ScriptProcgenScriptEditor* self, int index) {
        return self ? self->editor().paramKey(index) : std::string{};
    });
    procgenEditor.addFunc("getParamLabel", [](ScriptProcgenScriptEditor* self, int index) {
        return self ? self->editor().paramLabel(index) : std::string{};
    });
    procgenEditor.addFunc("getParamKind", [](ScriptProcgenScriptEditor* self, int index) {
        return self ? self->editor().paramKind(index) : std::string{};
    });
    procgenEditor.addFunc("getParamMinimum", [](ScriptProcgenScriptEditor* self, int index) {
        return self ? self->editor().paramMinimum(index) : 0.0f;
    });
    procgenEditor.addFunc("getParamMaximum", [](ScriptProcgenScriptEditor* self, int index) {
        return self ? self->editor().paramMaximum(index) : 0.0f;
    });
    procgenEditor.addFunc("getParamStep", [](ScriptProcgenScriptEditor* self, int index) {
        return self ? self->editor().paramStep(index) : 0.0f;
    });
    procgenEditor.addFunc("getParamChoiceCount", [](ScriptProcgenScriptEditor* self, int index) {
        return self ? self->editor().paramChoiceCount(index) : 0;
    });
    procgenEditor.addFunc("getParamChoice", [](ScriptProcgenScriptEditor* self, int paramIndex, int choiceIndex) {
        return self ? self->editor().paramChoice(paramIndex, choiceIndex) : std::string{};
    });
    procgenEditor.addFunc("getInt", [](ScriptProcgenScriptEditor* self, const std::string& key) {
        return self ? self->editor().getInt(key) : 0;
    });
    procgenEditor.addFunc("getFloat", [](ScriptProcgenScriptEditor* self, const std::string& key) {
        return self ? self->editor().getFloat(key) : 0.0f;
    });
    procgenEditor.addFunc("getBool", [](ScriptProcgenScriptEditor* self, const std::string& key) {
        return self && self->editor().getBool(key);
    });
    procgenEditor.addFunc("getString", [](ScriptProcgenScriptEditor* self, const std::string& key) {
        return self ? self->editor().getString(key) : std::string{};
    });
    procgenEditor.addFunc("getStageCount", [](ScriptProcgenScriptEditor* self) {
        return self ? self->editor().stageCount() : 0;
    });
    procgenEditor.addFunc("getStageName", [](ScriptProcgenScriptEditor* self, int index) {
        return self ? self->editor().stageName(index) : std::string{};
    });
    procgenEditor.addFunc("getPointCount", [](ScriptProcgenScriptEditor* self) {
        return self ? self->editor().pointCount() : 0;
    });
    procgenEditor.addFunc("getPointX", [](ScriptProcgenScriptEditor* self, int index) {
        return self ? self->editor().pointX(index) : 0.0f;
    });
    procgenEditor.addFunc("getPointZ", [](ScriptProcgenScriptEditor* self, int index) {
        return self ? self->editor().pointZ(index) : 0.0f;
    });
    procgenEditor.addFunc("getPointSeed", [](ScriptProcgenScriptEditor* self, int index) {
        return self ? static_cast<int>(self->editor().pointSeed(index)) : 0;
    });

    editor::registerEditorOwnedCreate<ScriptProcgenScriptEditor, ProcgenEditorModule>(
        moduleClass, bind, "procgen target id must not be empty");
    moduleClass.addFunc("createMeshModifier", [bind](ProcgenEditorModule*, const std::string& targetId) {
        return bind.ownedCreate<MeshModifierEditor>("mesh target id must not be empty", targetId);
    });
}

}  // namespace eve::procgen_editor
