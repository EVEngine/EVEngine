#include "procgen_editor/ProcgenScriptEditor.h"

#include "editor/EditorProtocol.h"
#include "editor/EditorSelection.h"
#include "procgen/PointSet.h"

#include <algorithm>
#include <string>
#include <utility>

namespace eve::procgen_editor {
namespace {

template <class T = void>
procgen_editing::EditorResult<T> editorError(procgen_editing::EditorStatus status, std::string rule,
                                             std::string message) {
    return eve::editing::failed<T>(status, eve::editing::RuleId(std::move(rule)), std::move(message));
}

}  // namespace

ProcgenScriptEditor::ProcgenScriptEditor(std::string targetId)
    : target_(std::move(targetId)), authority_(&target_), transactions_(&authority_) {}

procgen_editing::EditorResult<void> ProcgenScriptEditor::configureWorkspace(
    editor::EditorWorkspace& workspace) const {
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

procgen_editing::EditorResult<void> ProcgenScriptEditor::commit(
    procgen_editing::EditorResult<editing::DomainOperation> operation, std::string label) {
    if (!operation.ok()) return procgen_editing::EditorResult<void>::failure(operation.status());
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
        return procgen_editing::EditorResult<void>::failure(appended.status());
    }
    auto committed = transactions_.commit();
    if (!committed.ok()) return procgen_editing::EditorResult<void>::failure(committed.status());
    dirty_     = true;
    previewFailureSummary_.clear();
    return eve::editing::applied<void>();
}

procgen_editing::EditorResult<void> ProcgenScriptEditor::loadModule(procgen_editing::ProcgenScriptModuleSpec spec) {
    return commit(target_.makeLoadModule(std::move(spec)), "Load generator module");
}

procgen_editing::EditorResult<void> ProcgenScriptEditor::loadModule(std::string uri, std::string id,
                                                                   std::string displayName, std::string kind,
                                                                   const procgen_editing::EditorValue& schema) {
    auto parsed = procgen_editing::ProcgenScriptDocumentTarget::parseSpec(std::move(uri), std::move(id),
                                                                          std::move(displayName), std::move(kind),
                                                                          schema);
    if (!parsed.ok()) return procgen_editing::EditorResult<void>::failure(parsed.status());
    return loadModule(std::move(parsed).takeValue());
}

procgen_editing::EditorResult<void> ProcgenScriptEditor::setParam(std::string key,
                                                                 procgen_editing::EditorValue value) {
    return commit(target_.makeSet(selection(), editing::PropertyPath("param." + key), std::move(value),
                                  editing::PropertySetMode::Absolute),
                  "Set generator parameter");
}

procgen_editing::EditorResult<void> ProcgenScriptEditor::setInt(std::string key, int value) {
    return setParam(std::move(key), procgen_editing::EditorValue(std::int64_t{value}));
}

procgen_editing::EditorResult<void> ProcgenScriptEditor::setFloat(std::string key, double value) {
    return setParam(std::move(key), procgen_editing::EditorValue(value));
}

procgen_editing::EditorResult<void> ProcgenScriptEditor::setBool(std::string key, bool value) {
    return setParam(std::move(key), procgen_editing::EditorValue(value));
}

procgen_editing::EditorResult<void> ProcgenScriptEditor::setString(std::string key, std::string value) {
    return setParam(std::move(key), procgen_editing::EditorValue(std::move(value)));
}

procgen_editing::EditorResult<std::vector<ProcgenScriptEditor::PreviewPoint>> ProcgenScriptEditor::copyPoints(
    const procgen::PointSet* points) const {
    if (!points)
        return editorError<std::vector<PreviewPoint>>(procgen_editing::EditorStatus::Rejected,
                                                      "editor.procgen.preview", "Preview PointSet must not be null");
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

procgen_editing::EditorResult<void> ProcgenScriptEditor::publishPreview(const procgen::PointSet* points,
                                                                        std::string stage,
                                                                        std::uint64_t expectedRevision) {
    if (expectedRevision != target_.revision())
        return editorError(procgen_editing::EditorStatus::Conflict, "editor.procgen.stale-preview",
                           "Preview revision does not match the generator document");
    auto copied = copyPoints(points);
    if (!copied.ok()) return procgen_editing::EditorResult<void>::failure(copied.status());
    if (stage.empty()) stage = "output";
    if (std::find(stageOrder_.begin(), stageOrder_.end(), stage) == stageOrder_.end())
        stageOrder_.push_back(stage);
    stages_[stage]     = std::move(copied).takeValue();
    selectedStage_     = stage;
    previewRevision_   = target_.revision();
    dirty_             = false;
    previewFailureSummary_.clear();
    return eve::editing::applied<void>();
}

procgen_editing::EditorResult<void> ProcgenScriptEditor::publishStage(const procgen::PointSet* points,
                                                                     std::string stage) {
    if (stage.empty())
        return editorError(procgen_editing::EditorStatus::Rejected, "editor.procgen.stage",
                           "Debug stage name must not be empty");
    auto copied = copyPoints(points);
    if (!copied.ok()) return procgen_editing::EditorResult<void>::failure(copied.status());
    if (std::find(stageOrder_.begin(), stageOrder_.end(), stage) == stageOrder_.end())
        stageOrder_.push_back(stage);
    stages_[stage] = std::move(copied).takeValue();
    return eve::editing::applied<void>();
}

procgen_editing::EditorResult<void> ProcgenScriptEditor::failPreview(std::string message,
                                                                    std::uint64_t expectedRevision) {
    if (expectedRevision != target_.revision())
        return editorError(procgen_editing::EditorStatus::Conflict, "editor.procgen.stale-preview",
                           "Failed preview revision does not match the generator document");
    dirty_                  = false;
    previewFailureSummary_  = std::move(message);
    return eve::editing::applied<void>();
}

procgen_editing::EditorResult<void> ProcgenScriptEditor::selectStage(std::string stage) {
    if (stages_.find(stage) == stages_.end())
        return editorError(procgen_editing::EditorStatus::NotFound, "editor.procgen.stage",
                           "Debug stage was not found");
    selectedStage_ = std::move(stage);
    return eve::editing::applied<void>();
}

procgen_editing::EditorResult<void> ProcgenScriptEditor::setPointBudget(int budget) {
    if (budget < 0)
        return editorError(procgen_editing::EditorStatus::Rejected, "editor.procgen.point-budget",
                           "Point budget must not be negative");
    pointBudget_ = budget;
    return eve::editing::applied<void>();
}

procgen_editing::EditorResult<void> ProcgenScriptEditor::setLive(bool enabled) {
    continuousRebuild_ = enabled;
    return eve::editing::applied<void>();
}

procgen_editing::EditorResult<editor::TransactionReceipt> ProcgenScriptEditor::undo() {
    auto result = transactions_.undo();
    if (!result.ok()) return result;
    dirty_ = true;
    previewFailureSummary_.clear();
    return result;
}

procgen_editing::EditorResult<editor::TransactionReceipt> ProcgenScriptEditor::redo() {
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
        case procgen::ParamKind::Integer:
            return "int";
        case procgen::ParamKind::Float:
            return "float";
        case procgen::ParamKind::Boolean:
            return "bool";
        case procgen::ParamKind::Choice:
            return "choice";
        case procgen::ParamKind::String:
            return "string";
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
    const auto found = stages_.find(selectedStage_);
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
