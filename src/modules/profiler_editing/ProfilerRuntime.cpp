#include "profiler_editing/ProfilerModel.h"

#include "profiler/Profiler.h"

#include <utility>

namespace eve::profiler_editing {

EditorResult<void> EditorProfilerCollector::collect(const profiler::Profiler& profiler,
                                                     EditorProfilerModel& model) const {
    auto captured = profiler.captureFrame();
    if (!captured.hasValue()) {
        std::vector<EditorDiagnostic> diagnostics;
        for (const auto& diagnostic : captured.diagnostics())
            diagnostics.push_back(eve::editing::ruleDiagnostic(
                diagnostic.code(), RuleId("editor.profiler.capture"),
                DiagnosticSeverity::Error, diagnostic.message()));
        const EditorStatus status = captured.code() == eve::StatusCode::NotFound
                                        ? EditorStatus::NotFound
                                        : EditorStatus::Failed;
        return EditorResult<void>::failure(eve::Status(status, std::move(diagnostics)));
    }
    auto runtimeFrame = std::move(captured).takeValue();
    EditorProfilerFrame frame;
    frame.sequence = runtimeFrame.sequence;
    frame.cpuFrameMs = runtimeFrame.cpuFrameMs;
    frame.gpuFrameMs = runtimeFrame.gpuFrameMs;
    frame.gpuTimingAvailable = runtimeFrame.gpuTimingAvailable;
    frame.zones.reserve(runtimeFrame.zones.size());
    for (auto& zone : runtimeFrame.zones) {
        frame.zones.push_back({std::move(zone.module), std::move(zone.name),
                               std::move(zone.thread), zone.selfMs, zone.totalMs, zone.count,
                               zone.depth});
    }
    return model.ingest(std::move(frame));
}

}  // namespace eve::profiler_editing
