#pragma once

#include "editor/EditorProtocol.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace eve::profiler {
class Profiler;
struct ProfilerFrameSnapshot;
}  // namespace eve::profiler

namespace eve::editor {

/** @brief One owning profiler hotspot row projected for editor presentation. */
struct EditorProfilerZone {
    std::string module;
    std::string name;
    std::string thread;
    double      selfMs  = 0.0;
    double      totalMs = 0.0;
    int         count   = 0;
    int         depth   = 0;
};

/** @brief One owning completed-frame sample retained by the editor. */
struct EditorProfilerFrame {
    std::uint64_t                 sequence = 0;
    double                        cpuFrameMs = 0.0;
    double                        gpuFrameMs = 0.0;
    bool                          gpuTimingAvailable = false;
    std::vector<EditorProfilerZone> zones;
};

/** @brief Sort key used by the profiler hotspot table. */
enum class EditorProfilerSort { SelfTime, TotalTime, Count, Name };

/** @brief Non-persistent query applied to the latest profiler frame. */
struct EditorProfilerQuery {
    std::string        module;
    std::string        thread;
    std::string        text;
    EditorProfilerSort sort = EditorProfilerSort::SelfTime;
    bool               descending = true;
    std::size_t        limit = 500;
};

/** @brief Aggregated module timing for the latest completed frame. */
struct EditorProfilerModuleSummary {
    std::string module;
    double      selfMs = 0.0;
    double      totalMs = 0.0;
    int         count = 0;
    int         zones = 0;
};

/** @brief Configurable performance budgets used only for editor diagnostics. */
struct EditorProfilerBudgets {
    double cpuFrameMs = 16.667;
    double gpuFrameMs = 16.667;
    double zoneSelfMs = 4.0;
    std::size_t historyFrames = 300;
};

/**
 * @brief Bounded owning profiler history and deterministic presentation queries.
 * @thread Main-thread only.
 * @reentrancy Does not invoke callbacks.
 * @ownership Owns all retained frame and string data; it does not own the runtime profiler.
 */
class EditorProfilerModel {
public:
    /** @brief Construct with default budgets and a 300-frame history. */
    EditorProfilerModel();

    /**
     * @brief Validate and atomically replace editor-only performance budgets.
     * @param budgets Candidate budgets and history capacity.
     * @return Applied, or Rejected without changing the current budgets.
     */
    [[nodiscard]] EditorResult<void> configure(EditorProfilerBudgets budgets);

    /**
     * @brief Append an owning completed-frame snapshot.
     * @param frame Candidate frame with a strictly increasing sequence.
     * @return Applied, or Conflict/Rejected without changing history.
     */
    [[nodiscard]] EditorResult<void> ingest(EditorProfilerFrame frame);

    /** @brief Clear retained frames without changing configured budgets. */
    void clear();

    /** @brief Return the current editor-only budgets by value. */
    EditorProfilerBudgets budgets() const { return budgets_; }

    /** @brief Return the bounded owning frame history. */
    const std::deque<EditorProfilerFrame>& history() const { return history_; }

    /** @brief Query and sort copied hotspot rows from the latest frame. */
    std::vector<EditorProfilerZone> query(const EditorProfilerQuery& query) const;

    /** @brief Aggregate copied rows by module for the latest frame. */
    std::vector<EditorProfilerModuleSummary> moduleSummary() const;

    /** @brief Produce CPU/GPU/zone budget diagnostics for the latest frame. */
    std::vector<EditorDiagnostic> diagnostics() const;

private:
    EditorProfilerBudgets          budgets_;
    std::deque<EditorProfilerFrame> history_;
};

/**
 * @brief Optional profiler-module bridge that copies one completed runtime frame.
 * @thread Main-thread only, after the runtime profiler completed frame aggregation.
 * @reentrancy Does not invoke callbacks or scripts.
 */
class EditorProfilerCollector {
public:
    /**
     * @brief Copy and ingest the latest runtime frame.
     * @param profiler Immediate borrowed runtime profiler; never retained.
     * @param model Editor-owned destination history.
     * @return Runtime capture or model validation status.
     */
    [[nodiscard]] EditorResult<void> collect(const profiler::Profiler& profiler,
                                             EditorProfilerModel& model) const;
};

}  // namespace eve::editor
