#include "editor/EditorSimulationPreview.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace eve::editor {

EditorResult<void> SimulationPreviewController::setFixedDelta(double seconds) {
    if (!std::isfinite(seconds) || seconds <= 0.0 || seconds > 1.0)
        return EditorResult<void>::error(EditorStatus::Rejected,
            RuleId("editor.simulation.invalid-fixed-delta"),
            "Simulation preview delta must be finite and within (0, 1] seconds");
    fixedDelta_ = seconds;
    return EditorResult<void>::applied();
}

EditorResult<SimulationPreviewFrame> SimulationPreviewController::advance(
    IEditorSimulationBackend& backend, std::uint64_t tick) const {
    auto stepped = backend.step(tick, fixedDelta_);
    if (!stepped.isAccepted())
        return EditorResult<SimulationPreviewFrame>::error(stepped.status,
            stepped.diagnostics.empty() ? RuleId("editor.simulation.step-failed") : stepped.diagnostics.front().rule,
            stepped.diagnostics.empty() ? "Simulation preview step failed" : stepped.diagnostics.front().message);
    auto captured = backend.capture();
    if (!captured.value)
        return EditorResult<SimulationPreviewFrame>::error(captured.status,
            captured.diagnostics.empty() ? RuleId("editor.simulation.capture-failed") : captured.diagnostics.front().rule,
            captured.diagnostics.empty() ? "Simulation preview capture failed" : captured.diagnostics.front().message);
    std::set<std::string> ids;
    for (const auto& object : *captured.value) {
        if (object.object.empty() || !ids.insert(object.object).second)
            return EditorResult<SimulationPreviewFrame>::error(EditorStatus::Conflict,
                RuleId("editor.simulation.unstable-object-id"),
                "Simulation preview samples require unique stable object ids");
        const double values[]{object.positionX, object.positionY, object.positionZ,
                              object.rotationX, object.rotationY, object.rotationZ, object.rotationW};
        if (std::any_of(std::begin(values), std::end(values), [](double value) { return !std::isfinite(value); }))
            return EditorResult<SimulationPreviewFrame>::error(EditorStatus::Failed,
                RuleId("editor.simulation.nonfinite-sample"), "Simulation preview produced a non-finite transform");
    }
    std::sort(captured.value->begin(), captured.value->end(),
              [](const auto& a, const auto& b) { return a.object < b.object; });
    return EditorResult<SimulationPreviewFrame>::applied(
        {tick, static_cast<double>(tick) * fixedDelta_, std::move(*captured.value)});
}

EditorResult<SimulationPreviewFrame> SimulationPreviewController::singleStep(
    const IEditorSimulationBackend& source) {
    if (!previewBackend_) previewBackend_ = source.cloneForPreview();
    if (!previewBackend_)
        return EditorResult<SimulationPreviewFrame>::error(EditorStatus::Unsupported,
            RuleId("editor.simulation.clone-unsupported"), "Simulation backend cannot create an isolated preview");
    auto result = advance(*previewBackend_, tick_ + 1);
    if (result.isAccepted()) ++tick_;
    return result;
}

void SimulationPreviewController::rewind() {
    tick_ = 0;
    previewBackend_.reset();
}

EditorResult<SimulationPreviewFrame> SimulationPreviewController::update(
    const IEditorSimulationBackend& source) {
    if (paused_) {
        EditorResult<SimulationPreviewFrame> result;
        result.status = EditorStatus::NoOp;
        return result;
    }
    return singleStep(source);
}

SimulationBakeResult SimulationPreviewController::bake(const IEditorSimulationBackend& source,
                                                        std::uint64_t sourceRevision,
                                                        int steps, int maxSteps) const {
    SimulationBakeResult result;
    result.sourceRevision = sourceRevision;
    result.fixedDelta = fixedDelta_;
    if (steps < 0 || maxSteps <= 0 || steps > maxSteps) {
        result.status = EditorStatus::Rejected;
        result.diagnostics.push_back({RuleId("editor.simulation.bake-budget"), DiagnosticSeverity::Error,
                                      "Simulation bake step count exceeds the configured budget"});
        return result;
    }
    auto backend = source.cloneForPreview();
    if (!backend) {
        result.status = EditorStatus::Unsupported;
        result.diagnostics.push_back({RuleId("editor.simulation.clone-unsupported"), DiagnosticSeverity::Error,
                                      "Simulation backend cannot create an isolated preview"});
        return result;
    }
    auto initial = backend->capture();
    if (!initial.value) {
        result.status = initial.status; result.diagnostics = std::move(initial.diagnostics); return result;
    }
    std::sort(initial.value->begin(), initial.value->end(), [](const auto& a, const auto& b) { return a.object < b.object; });
    result.frames.push_back({0, 0.0, std::move(*initial.value)});
    for (int index = 1; index <= steps; ++index) {
        auto frame = advance(*backend, static_cast<std::uint64_t>(index));
        if (!frame.value) { result.status = frame.status; result.diagnostics = std::move(frame.diagnostics); return result; }
        result.frames.push_back(std::move(*frame.value));
    }
    result.status = EditorStatus::Applied;
    return result;
}

}  // namespace eve::editor
