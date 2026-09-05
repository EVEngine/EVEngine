#include "procgen_editor/ProcgenScriptEditorScriptBindings.h"

#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"
#include "editor/EditorProperty.h"
#include "editor/EditorWorkspace.h"
#include "procgen/PointSet.h"
#include "procgen_editor/ProcgenEditorModule.h"
#include "procgen_editor/ProcgenScriptEditor.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace eve::procgen_editor {
namespace {

constexpr const char* kBindingSource = "editor.procgen.script.squirrel";

Status statusFrom(const procgen_editing::EditorResult<void>& result) { return result.status(); }

template <class T>
Status statusFrom(const procgen_editing::EditorResult<T>& result) {
    return result.status();
}

ssq::Table project(HSQUIRRELVM vm, const procgen_editing::EditorResult<void>& result) {
    return script::projectStatusResult(vm, statusFrom(result), result.ok(), false);
}

template <class T>
ssq::Table project(HSQUIRRELVM vm, const procgen_editing::EditorResult<T>& result, Value value) {
    const bool hasValue = result.ok();
    return script::projectStatusResult(vm, statusFrom(result), hasValue, hasValue, value);
}

ssq::Table bindingFailure(HSQUIRRELVM vm, DiagnosticCode code, std::string message, std::string path = {}) {
    return script::projectStatusResult(
        vm, Status::failure(Diagnostic::error(code, std::move(message), std::move(path), {}, kBindingSource)), false,
        false);
}

class ScriptProcgenScriptEditor {
public:
    explicit ScriptProcgenScriptEditor(std::string targetId) : editor_(std::move(targetId)) {}

    ProcgenScriptEditor&       editor() noexcept { return editor_; }
    const ProcgenScriptEditor& editor() const noexcept { return editor_; }

private:
    ProcgenScriptEditor editor_;
};

ssq::Table loadModuleFromScript(HSQUIRRELVM vm, ScriptProcgenScriptEditor* self, const std::string& uri,
                                const std::string& id, const std::string& displayName, const std::string& kind,
                                const ssq::Object& schema) {
    if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "procgen editor must not be null");
    auto converted = script::valueFromSquirrel(schema);
    if (!converted.ok())
        return script::projectStatusResult(vm, converted.status(), false, false);
    return project(vm, self->editor().loadModule(uri, id, displayName, kind,
                                                 editor::toEditorValue(converted.value())));
}

}  // namespace

void exposeProcgenScriptEditorScriptBindings(ssq::Table& table, ssq::Class& moduleClass) {
    const HSQUIRRELVM vm = table.getHandle();
    auto procgenEditor   = table.addClass<ScriptProcgenScriptEditor>(
        "ProcgenScriptEditor",
        std::function<ScriptProcgenScriptEditor*()>([]() -> ScriptProcgenScriptEditor* { return nullptr; }), true);

    procgenEditor.addFunc("configureWorkspace",
                          [vm](ScriptProcgenScriptEditor* self, editor::EditorWorkspace* workspace) {
                              if (!self || !workspace)
                                  return bindingFailure(vm, DiagnosticCode::InvalidArgument,
                                                        "procgen editor and workspace must not be null", "workspace");
                              return project(vm, self->editor().configureWorkspace(*workspace));
                          });
    procgenEditor.addFunc("loadModule",
                          [vm](ScriptProcgenScriptEditor* self, const std::string& uri, const std::string& id,
                               const std::string& displayName, const std::string& kind, const ssq::Object& schema) {
                              return loadModuleFromScript(vm, self, uri, id, displayName, kind, schema);
                          });
    procgenEditor.addFunc("setInt", [vm](ScriptProcgenScriptEditor* self, const std::string& key, int value) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "procgen editor must not be null");
        return project(vm, self->editor().setInt(key, value));
    });
    procgenEditor.addFunc("setFloat", [vm](ScriptProcgenScriptEditor* self, const std::string& key, float value) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "procgen editor must not be null");
        return project(vm, self->editor().setFloat(key, static_cast<double>(value)));
    });
    procgenEditor.addFunc("setBool", [vm](ScriptProcgenScriptEditor* self, const std::string& key, bool value) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "procgen editor must not be null");
        return project(vm, self->editor().setBool(key, value));
    });
    procgenEditor.addFunc("setString",
                          [vm](ScriptProcgenScriptEditor* self, const std::string& key, const std::string& value) {
                              if (!self)
                                  return bindingFailure(vm, DiagnosticCode::InvalidArgument,
                                                        "procgen editor must not be null");
                              return project(vm, self->editor().setString(key, value));
                          });
    procgenEditor.addFunc("publishPreview",
                          [vm](ScriptProcgenScriptEditor* self, procgen::PointSet* points, const std::string& stage,
                               int expectedRevision) {
                              if (!self)
                                  return bindingFailure(vm, DiagnosticCode::InvalidArgument,
                                                        "procgen editor must not be null");
                              return project(vm, self->editor().publishPreview(
                                                     points, stage, static_cast<std::uint64_t>(expectedRevision)));
                          });
    procgenEditor.addFunc("publishStage", [vm](ScriptProcgenScriptEditor* self, procgen::PointSet* points,
                                               const std::string& stage) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "procgen editor must not be null");
        return project(vm, self->editor().publishStage(points, stage));
    });
    procgenEditor.addFunc("failPreview",
                          [vm](ScriptProcgenScriptEditor* self, const std::string& message, int expectedRevision) {
                              if (!self)
                                  return bindingFailure(vm, DiagnosticCode::InvalidArgument,
                                                        "procgen editor must not be null");
                              return project(vm, self->editor().failPreview(
                                                     message, static_cast<std::uint64_t>(expectedRevision)));
                          });
    procgenEditor.addFunc("selectStage", [vm](ScriptProcgenScriptEditor* self, const std::string& stage) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "procgen editor must not be null");
        return project(vm, self->editor().selectStage(stage));
    });
    procgenEditor.addFunc("setPointBudget", [vm](ScriptProcgenScriptEditor* self, int budget) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "procgen editor must not be null");
        return project(vm, self->editor().setPointBudget(budget));
    });
    procgenEditor.addFunc("setLive", [vm](ScriptProcgenScriptEditor* self, bool enabled) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "procgen editor must not be null");
        return project(vm, self->editor().setLive(enabled));
    });
    procgenEditor.addFunc("undo", [vm](ScriptProcgenScriptEditor* self) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "procgen editor must not be null");
        auto result = self->editor().undo();
        return project(vm, result, Value(result.ok() ? static_cast<std::int64_t>(result.value().afterRevision) : 0));
    });
    procgenEditor.addFunc("redo", [vm](ScriptProcgenScriptEditor* self) {
        if (!self) return bindingFailure(vm, DiagnosticCode::InvalidArgument, "procgen editor must not be null");
        auto result = self->editor().redo();
        return project(vm, result, Value(result.ok() ? static_cast<std::int64_t>(result.value().afterRevision) : 0));
    });
    procgenEditor.addFunc("canUndo", [](ScriptProcgenScriptEditor* self) { return self && self->editor().canUndo(); });
    procgenEditor.addFunc("canRedo", [](ScriptProcgenScriptEditor* self) { return self && self->editor().canRedo(); });
    procgenEditor.addFunc("isDirty", [](ScriptProcgenScriptEditor* self) { return self && self->editor().isDirty(); });
    procgenEditor.addFunc("isLive", [](ScriptProcgenScriptEditor* self) {
        return self && self->editor().continuousRebuild();
    });
    procgenEditor.addFunc("getRevision", [](ScriptProcgenScriptEditor* self) {
        return self ? static_cast<int>(self->editor().revision()) : 0;
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

    moduleClass.addFunc("create", [vm](ProcgenEditorModule*, const std::string& targetId) {
        if (targetId.empty())
            return bindingFailure(vm, DiagnosticCode::InvalidArgument, "procgen target id must not be empty",
                                  "targetId");
        auto object = script::makeOwnedSquirrelInstance<ScriptProcgenScriptEditor>(
            vm, std::make_unique<ScriptProcgenScriptEditor>(targetId));
        if (!object) return script::projectStatusResult(vm, object.status(), false, false);
        ssq::Object owned = std::move(object).takeValue();
        auto        result = script::projectStatusResult(vm, Status::success(StatusCode::Applied), true, false);
        result.set("value", owned);
        result.set("ownership", std::string("owned"));
        return result;
    });
}

}  // namespace eve::procgen_editor
