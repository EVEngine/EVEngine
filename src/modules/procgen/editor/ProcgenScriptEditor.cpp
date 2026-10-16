#include "procgen/editor/ProcgenScriptEditor.h"
#include <algorithm>
#include <cstdint>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include <utility>
#include "editor/EditorProperty.h"
#include "editor/EditorProtocol.h"
#include "editor/EditorScriptProjection.h"
#include "editor/EditorSelection.h"
#include "editor/EditorWorkspace.h"
#include "procgen/PointSet.h"
#include "procgen/editor/MeshModifierEditor.h"
#include "procgen/editor/ProcgenEditorModule.h"
#include "procgen/editor/ProcgenScriptEditorScriptBindings.h"

// ProcgenScriptEditor.cpp
namespace eve::procgen_editor {
namespace {

template <class T = void>
procgen_editing::Result<T> editorError(procgen_editing::EditorStatus status, std::string rule, std::string message) {
    return eve::editing::failed<T>(status, eve::editing::RuleId(std::move(rule)), std::move(message));
}

}  // namespace

ProcgenScriptEditor::ProcgenScriptEditor(std::string targetId)
    : target_(std::move(targetId)), authority_(&target_), transactions_(&authority_) {}

procgen_editing::Result<void> ProcgenScriptEditor::configureWorkspace(editor::EditorWorkspace& workspace) const {
    editor::EditorWorkspace candidate = workspace;
    struct Panel {
        const char* id;
        const char* title;
        const char* region;
        const char* context;
        int         order;
    };
    constexpr Panel panels[] = {
        {"procgen.modules", "Modules", "left", "list", 100},
        {"procgen.preview", "World Preview", "center", "preview", 100},
        {"procgen.inspector", "Generator Inspector", "right", "inspector", 100},
        {"procgen.stages", "Debug Stages", "bottom", "stages", 100},
    };
    for (const auto& panel : panels) {
        if (!candidate.registerPanel(panel.id, panel.title, panel.region, panel.order) ||
            !candidate.setPanelCapability(panel.id, "procgen.script") ||
            !candidate.setPanelContext(panel.id, panel.context))
            return editorError(procgen_editing::EditorStatus::Rejected, "editor.procgen.workspace-conflict",
                               "Could not install the procgen script workspace composition");
    }
    if (!candidate.activatePanel("procgen.preview"))
        return editorError(procgen_editing::EditorStatus::Rejected, "editor.procgen.workspace-activate",
                           "Could not activate the procgen preview panel");
    workspace = std::move(candidate);
    return eve::editing::applied<void>();
}

editor::SelectionSnapshot ProcgenScriptEditor::selection() const {
    editor::SelectionSnapshot snapshot;
    snapshot.channel = "procgen";
    editor::SelectionItem item;
    item.domain = editor::SelectionDomain::Asset;
    item.target = editor::TargetId(target_.targetId());
    item.item   = editor::StableId(target_.moduleId().empty() ? target_.targetId().value() : target_.moduleId());
    item.type   = "procgen-script";
    snapshot.items.push_back(item);
    snapshot.primary = item;
    return snapshot;
}

procgen_editing::Result<void> ProcgenScriptEditor::commit(procgen_editing::Result<editing::DomainOperation> operation,
                                                          std::string                                       label) {
    if (!operation.ok()) return procgen_editing::Result<void>::failure(operation.status());
    editor::TransactionSpec spec;
    spec.id           = editor::TransactionId("procgen.script.tx." + std::to_string(++txSequence_));
    spec.label        = std::move(label);
    spec.target       = editor::TargetId(target_.targetId());
    spec.baseRevision = target_.revision();
    auto begun        = transactions_.begin(std::move(spec));
    if (!begun.ok())
        return editorError(begun.code(), "editor.procgen.begin", "Could not begin the generator transaction");
    auto appended = transactions_.append(std::move(operation).takeValue());
    if (!appended.ok()) {
        auto discarded = transactions_.discard();
        if (!discarded.ok()) discarded.ignore("pending procgen script transaction already inactive");
        return procgen_editing::Result<void>::failure(appended.status());
    }
    auto committed = transactions_.commit();
    if (!committed.ok()) return procgen_editing::Result<void>::failure(committed.status());
    dirty_ = true;
    previewFailureSummary_.clear();
    return eve::editing::applied<void>();
}

procgen_editing::Result<void> ProcgenScriptEditor::loadModule(procgen_editing::ProcgenScriptModuleSpec spec) {
    return commit(target_.makeLoadModule(std::move(spec)), "Load generator module");
}

procgen_editing::Result<void> ProcgenScriptEditor::loadModule(std::string uri, std::string id, std::string displayName,
                                                              std::string                         kind,
                                                              const procgen_editing::EditorValue& schema) {
    auto parsed = procgen_editing::ProcgenScriptDocumentTarget::parseSpec(
        std::move(uri), std::move(id), std::move(displayName), std::move(kind), schema);
    if (!parsed.ok()) return procgen_editing::Result<void>::failure(parsed.status());
    return loadModule(std::move(parsed).takeValue());
}

procgen_editing::Result<void> ProcgenScriptEditor::setParam(std::string key, procgen_editing::EditorValue value) {
    return commit(target_.makeSet(selection(), editing::PropertyPath("param." + key), std::move(value),
                                  editing::PropertySetMode::Absolute),
                  "Set generator parameter");
}

procgen_editing::Result<void> ProcgenScriptEditor::setInt(std::string key, int value) {
    return setParam(std::move(key), procgen_editing::EditorValue(std::int64_t{value}));
}

procgen_editing::Result<void> ProcgenScriptEditor::setFloat(std::string key, double value) {
    return setParam(std::move(key), procgen_editing::EditorValue(value));
}

procgen_editing::Result<void> ProcgenScriptEditor::setBool(std::string key, bool value) {
    return setParam(std::move(key), procgen_editing::EditorValue(value));
}

procgen_editing::Result<void> ProcgenScriptEditor::setString(std::string key, std::string value) {
    return setParam(std::move(key), procgen_editing::EditorValue(std::move(value)));
}

procgen_editing::Result<std::vector<ProcgenScriptEditor::PreviewPoint>> ProcgenScriptEditor::copyPoints(
    const procgen::PointSet* points) const {
    if (!points)
        return editorError<std::vector<PreviewPoint>>(procgen_editing::EditorStatus::Rejected, "editor.procgen.preview",
                                                      "Preview PointSet must not be null");
    if (pointBudget_ > 0 && points->getCount() > pointBudget_)
        return editorError<std::vector<PreviewPoint>>(procgen_editing::EditorStatus::Rejected,
                                                      "editor.procgen.point-budget",
                                                      "Preview exceeded the point budget");
    std::vector<PreviewPoint> next;
    next.reserve(static_cast<std::size_t>(points->getCount()));
    for (int i = 0; i < points->getCount(); ++i) {
        PreviewPoint point;
        point.x    = points->getX(i);
        point.z    = points->getZ(i);
        point.seed = points->getPointSeed(i);
        next.push_back(point);
    }
    return eve::editing::applied(std::move(next));
}

procgen_editing::Result<void> ProcgenScriptEditor::publishPreview(const procgen::PointSet* points, std::string stage,
                                                                  std::uint64_t expectedRevision) {
    if (expectedRevision != target_.revision())
        return editorError(procgen_editing::EditorStatus::Conflict, "editor.procgen.stale-preview",
                           "Preview revision does not match the generator document");
    auto copied = copyPoints(points);
    if (!copied.ok()) return procgen_editing::Result<void>::failure(copied.status());
    if (stage.empty()) stage = "output";
    if (std::find(stageOrder_.begin(), stageOrder_.end(), stage) == stageOrder_.end()) stageOrder_.push_back(stage);
    stages_[stage]   = std::move(copied).takeValue();
    selectedStage_   = stage;
    previewRevision_ = target_.revision();
    dirty_           = false;
    previewFailureSummary_.clear();
    return eve::editing::applied<void>();
}

procgen_editing::Result<void> ProcgenScriptEditor::publishStage(const procgen::PointSet* points, std::string stage) {
    if (stage.empty())
        return editorError(procgen_editing::EditorStatus::Rejected, "editor.procgen.stage",
                           "Debug stage name must not be empty");
    auto copied = copyPoints(points);
    if (!copied.ok()) return procgen_editing::Result<void>::failure(copied.status());
    if (std::find(stageOrder_.begin(), stageOrder_.end(), stage) == stageOrder_.end()) stageOrder_.push_back(stage);
    stages_[stage] = std::move(copied).takeValue();
    return eve::editing::applied<void>();
}

procgen_editing::Result<void> ProcgenScriptEditor::failPreview(std::string message, std::uint64_t expectedRevision) {
    if (expectedRevision != target_.revision())
        return editorError(procgen_editing::EditorStatus::Conflict, "editor.procgen.stale-preview",
                           "Failed preview revision does not match the generator document");
    dirty_                 = false;
    previewFailureSummary_ = std::move(message);
    return eve::editing::applied<void>();
}

procgen_editing::Result<void> ProcgenScriptEditor::selectStage(std::string stage) {
    if (stages_.find(stage) == stages_.end())
        return editorError(procgen_editing::EditorStatus::NotFound, "editor.procgen.stage",
                           "Debug stage was not found");
    selectedStage_ = std::move(stage);
    return eve::editing::applied<void>();
}

procgen_editing::Result<void> ProcgenScriptEditor::setPointBudget(int budget) {
    if (budget < 0)
        return editorError(procgen_editing::EditorStatus::Rejected, "editor.procgen.point-budget",
                           "Point budget must not be negative");
    pointBudget_ = budget;
    return eve::editing::applied<void>();
}

procgen_editing::Result<void> ProcgenScriptEditor::setLive(bool enabled) {
    continuousRebuild_ = enabled;
    return eve::editing::applied<void>();
}

procgen_editing::Result<editor::TransactionReceipt> ProcgenScriptEditor::undo() {
    auto result = transactions_.undo();
    if (!result.ok()) return result;
    dirty_ = true;
    previewFailureSummary_.clear();
    return result;
}

procgen_editing::Result<editor::TransactionReceipt> ProcgenScriptEditor::redo() {
    auto result = transactions_.redo();
    if (!result.ok()) return result;
    dirty_ = true;
    previewFailureSummary_.clear();
    return result;
}

const procgen::ParamDescriptor* ProcgenScriptEditor::paramAt(int index) const {
    if (index < 0 || static_cast<std::size_t>(index) >= target_.params().size()) return nullptr;
    return &target_.params()[static_cast<std::size_t>(index)];
}

std::string ProcgenScriptEditor::paramKey(int index) const {
    const auto* param = paramAt(index);
    return param ? param->key : std::string{};
}
std::string ProcgenScriptEditor::paramLabel(int index) const {
    const auto* param = paramAt(index);
    return param ? (param->displayName.empty() ? param->key : param->displayName) : std::string{};
}
std::string ProcgenScriptEditor::paramKind(int index) const {
    const auto* param = paramAt(index);
    if (!param) return {};
    switch (param->kind) {
        case procgen::ParamKind::Integer: return "int";
        case procgen::ParamKind::Float: return "float";
        case procgen::ParamKind::Boolean: return "bool";
        case procgen::ParamKind::Choice: return "choice";
        case procgen::ParamKind::String: return "string";
    }
    return {};
}
float ProcgenScriptEditor::paramMinimum(int index) const {
    const auto* param = paramAt(index);
    return param ? static_cast<float>(param->minimum) : 0.0f;
}
float ProcgenScriptEditor::paramMaximum(int index) const {
    const auto* param = paramAt(index);
    return param ? static_cast<float>(param->maximum) : 0.0f;
}
float ProcgenScriptEditor::paramStep(int index) const {
    const auto* param = paramAt(index);
    return param ? static_cast<float>(param->step) : 0.0f;
}
int ProcgenScriptEditor::paramChoiceCount(int index) const {
    const auto* param = paramAt(index);
    return param ? static_cast<int>(param->choices.size()) : 0;
}
std::string ProcgenScriptEditor::paramChoice(int paramIndex, int choiceIndex) const {
    const auto* param = paramAt(paramIndex);
    if (!param || choiceIndex < 0 || static_cast<std::size_t>(choiceIndex) >= param->choices.size()) return {};
    return param->choices[static_cast<std::size_t>(choiceIndex)];
}

int ProcgenScriptEditor::getInt(const std::string& key) const {
    const auto found = target_.values().find(key);
    if (found == target_.values().end()) return 0;
    if (const auto* integer = found->second.getIf<std::int64_t>()) return static_cast<int>(*integer);
    if (const auto* real = found->second.getIf<double>()) return static_cast<int>(*real);
    return 0;
}
float ProcgenScriptEditor::getFloat(const std::string& key) const {
    const auto found = target_.values().find(key);
    if (found == target_.values().end()) return 0.0f;
    if (const auto* real = found->second.getIf<double>()) return static_cast<float>(*real);
    if (const auto* integer = found->second.getIf<std::int64_t>()) return static_cast<float>(*integer);
    return 0.0f;
}
bool ProcgenScriptEditor::getBool(const std::string& key) const {
    const auto found = target_.values().find(key);
    if (found == target_.values().end()) return false;
    if (const auto* flag = found->second.getIf<bool>()) return *flag;
    if (const auto* integer = found->second.getIf<std::int64_t>()) return *integer != 0;
    return false;
}
std::string ProcgenScriptEditor::getString(const std::string& key) const {
    const auto found = target_.values().find(key);
    if (found == target_.values().end()) return {};
    if (const auto* text = found->second.getIf<std::string>()) return *text;
    return {};
}

const std::vector<ProcgenScriptEditor::PreviewPoint>& ProcgenScriptEditor::displayedPoints() const {
    static const std::vector<PreviewPoint> empty;
    const auto                             found = stages_.find(selectedStage_);
    return found == stages_.end() ? empty : found->second;
}

std::string ProcgenScriptEditor::stageName(int index) const {
    if (index < 0 || static_cast<std::size_t>(index) >= stageOrder_.size()) return {};
    return stageOrder_[static_cast<std::size_t>(index)];
}

int ProcgenScriptEditor::pointCount() const { return static_cast<int>(displayedPoints().size()); }

float ProcgenScriptEditor::pointX(int index) const {
    const auto& points = displayedPoints();
    if (index < 0 || static_cast<std::size_t>(index) >= points.size()) return 0.0f;
    return points[static_cast<std::size_t>(index)].x;
}
float ProcgenScriptEditor::pointZ(int index) const {
    const auto& points = displayedPoints();
    if (index < 0 || static_cast<std::size_t>(index) >= points.size()) return 0.0f;
    return points[static_cast<std::size_t>(index)].z;
}
std::uint32_t ProcgenScriptEditor::pointSeed(int index) const {
    const auto& points = displayedPoints();
    if (index < 0 || static_cast<std::size_t>(index) >= points.size()) return 0;
    return points[static_cast<std::size_t>(index)].seed;
}

}  // namespace eve::procgen_editor

// ProcgenScriptEditorScriptBindings.cpp
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
    meshEditor.addFunc("getActiveTool",
                       [](MeshModifierEditor* self) { return self ? self->activeTool() : std::string{}; });
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
    procgenEditor.addFunc("isLive",
                          [](ScriptProcgenScriptEditor* self) { return self && self->editor().isContinuousRebuild(); });
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
    procgenEditor.addFunc("getKind",
                          [](ScriptProcgenScriptEditor* self) { return self ? self->editor().kind() : std::string{}; });
    procgenEditor.addFunc("getSelectedStage", [](ScriptProcgenScriptEditor* self) {
        return self ? self->editor().selectedStage() : std::string{};
    });
    procgenEditor.addFunc("getPreviewFailureSummary", [](ScriptProcgenScriptEditor* self) {
        return self ? self->editor().previewFailureSummary() : std::string{};
    });
    procgenEditor.addFunc("getParamCount",
                          [](ScriptProcgenScriptEditor* self) { return self ? self->editor().paramCount() : 0; });
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
    procgenEditor.addFunc("getStageCount",
                          [](ScriptProcgenScriptEditor* self) { return self ? self->editor().stageCount() : 0; });
    procgenEditor.addFunc("getStageName", [](ScriptProcgenScriptEditor* self, int index) {
        return self ? self->editor().stageName(index) : std::string{};
    });
    procgenEditor.addFunc("getPointCount",
                          [](ScriptProcgenScriptEditor* self) { return self ? self->editor().pointCount() : 0; });
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
