#include "editor/EditorProfiler.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>
#include <utility>

namespace eve::editor {
namespace {

template <class T>
EditorResult<T> failure(EditorStatus status, const char* rule, std::string message) {
    return EditorResult<T>::error(status, RuleId(rule), std::move(message));
}

bool finiteNonNegative(double value) { return std::isfinite(value) && value >= 0.0; }

bool contains(const std::string& value, const std::string& filter) {
    return filter.empty() || value.find(filter) != std::string::npos;
}

}  // namespace

EditorProfilerModel::EditorProfilerModel() = default;

EditorResult<void> EditorProfilerModel::configure(EditorProfilerBudgets budgets) {
    if (!finiteNonNegative(budgets.cpuFrameMs) || !finiteNonNegative(budgets.gpuFrameMs) ||
        !finiteNonNegative(budgets.zoneSelfMs) || budgets.historyFrames < 2 ||
        budgets.historyFrames > 36000) {
        return failure<void>(EditorStatus::Rejected, "editor.profiler.invalid-budgets",
                             "Profiler budgets or history capacity are invalid");
    }
    budgets_ = budgets;
    while (history_.size() > budgets_.historyFrames) history_.pop_front();
    return EditorResult<void>::applied();
}

EditorResult<void> EditorProfilerModel::ingest(EditorProfilerFrame frame) {
    if (frame.sequence == 0 || !finiteNonNegative(frame.cpuFrameMs) ||
        !finiteNonNegative(frame.gpuFrameMs) || frame.zones.size() > 100000) {
        return failure<void>(EditorStatus::Rejected, "editor.profiler.invalid-frame",
                             "Profiler frame shape or timing is invalid");
    }
    if (!history_.empty() && frame.sequence <= history_.back().sequence) {
        return failure<void>(EditorStatus::Conflict, "editor.profiler.stale-frame",
                             "Profiler frame sequence is stale");
    }
    for (const auto& zone : frame.zones) {
        if (zone.module.empty() || zone.name.empty() || zone.thread.empty() ||
            !finiteNonNegative(zone.selfMs) || !finiteNonNegative(zone.totalMs) ||
            zone.selfMs > zone.totalMs || zone.count < 1 || zone.depth < 0) {
            return failure<void>(EditorStatus::Rejected, "editor.profiler.invalid-zone",
                                 "Profiler zone identity or timing is invalid");
        }
    }
    history_.push_back(std::move(frame));
    while (history_.size() > budgets_.historyFrames) history_.pop_front();
    return EditorResult<void>::applied();
}

void EditorProfilerModel::clear() { history_.clear(); }

std::vector<EditorProfilerZone> EditorProfilerModel::query(
    const EditorProfilerQuery& options) const {
    std::vector<EditorProfilerZone> result;
    if (history_.empty() || options.limit == 0) return result;
    for (const auto& zone : history_.back().zones) {
        if (contains(zone.module, options.module) && contains(zone.thread, options.thread) &&
            (contains(zone.name, options.text) || contains(zone.module, options.text))) {
            result.push_back(zone);
        }
    }
    const auto less = [&](const EditorProfilerZone& left, const EditorProfilerZone& right) {
        switch (options.sort) {
            case EditorProfilerSort::SelfTime: return left.selfMs < right.selfMs;
            case EditorProfilerSort::TotalTime: return left.totalMs < right.totalMs;
            case EditorProfilerSort::Count: return left.count < right.count;
            case EditorProfilerSort::Name:
                return std::tie(left.module, left.name, left.thread) <
                       std::tie(right.module, right.name, right.thread);
        }
        return false;
    };
    std::stable_sort(result.begin(), result.end(), [&](const auto& left, const auto& right) {
        return options.descending ? less(right, left) : less(left, right);
    });
    if (result.size() > options.limit) result.resize(options.limit);
    return result;
}

std::vector<EditorProfilerModuleSummary> EditorProfilerModel::moduleSummary() const {
    std::map<std::string, EditorProfilerModuleSummary> grouped;
    if (!history_.empty()) {
        for (const auto& zone : history_.back().zones) {
            auto& summary = grouped[zone.module];
            summary.module = zone.module;
            summary.selfMs += zone.selfMs;
            summary.totalMs += zone.totalMs;
            summary.count += zone.count;
            ++summary.zones;
        }
    }
    std::vector<EditorProfilerModuleSummary> result;
    for (auto& [name, summary] : grouped) result.push_back(std::move(summary));
    std::stable_sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        if (left.selfMs != right.selfMs) return left.selfMs > right.selfMs;
        return left.module < right.module;
    });
    return result;
}

std::vector<EditorDiagnostic> EditorProfilerModel::diagnostics() const {
    std::vector<EditorDiagnostic> result;
    if (history_.empty()) return result;
    const auto& frame = history_.back();
    if (budgets_.cpuFrameMs > 0.0 && frame.cpuFrameMs > budgets_.cpuFrameMs) {
        result.push_back({RuleId("editor.profiler.cpu-budget"), DiagnosticSeverity::Warning,
                          "CPU frame time exceeds the editor budget"});
    }
    if (frame.gpuTimingAvailable && budgets_.gpuFrameMs > 0.0 &&
        frame.gpuFrameMs > budgets_.gpuFrameMs) {
        result.push_back({RuleId("editor.profiler.gpu-budget"), DiagnosticSeverity::Warning,
                          "GPU frame time exceeds the editor budget"});
    }
    if (budgets_.zoneSelfMs > 0.0) {
        for (const auto& zone : frame.zones) {
            if (zone.selfMs > budgets_.zoneSelfMs) {
                result.push_back({RuleId("editor.profiler.zone-budget"),
                                  DiagnosticSeverity::Warning,
                                  "Profiler zone exceeds its self-time budget: " + zone.module +
                                      "/" + zone.name});
            }
        }
    }
    return result;
}

}  // namespace eve::editor
