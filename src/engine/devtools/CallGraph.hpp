#pragma once

#include "common/Export.h"

#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace eve::dev {

/** Source location in a Squirrel (or synthetic) script. */
struct EVENGINE_API SourceLoc {
    std::string source;
    int         line = 0;
    std::string function;

    bool empty() const { return source.empty() && line <= 0 && function.empty(); }
    bool matches(const SourceLoc& o) const;
    std::string toString() const;
};

enum class TraceKind : uint8_t {
    Call = 0,
    Return,
    Line,
    Def,  // variable definition / write
    Use,  // variable use / read
};

/** One recorded runtime event used by the dynamic slicer. */
struct EVENGINE_API TraceEvent {
    uint32_t  id      = 0;
    TraceKind kind    = TraceKind::Line;
    SourceLoc loc;
    std::string name;       // function name (Call/Return) or variable (Def/Use)
    uint32_t  frameId = 0;  // activation record id
    uint32_t  parentEventId = 0;  // control predecessor (previous event in flow)
};

struct EVENGINE_API CallFrame {
    uint32_t  frameId = 0;
    SourceLoc loc;
    uint32_t  callEventId = 0;
};

struct EVENGINE_API DataFlowEdge {
    uint32_t fromEventId = 0;  // Def (or Call arg)
    uint32_t toEventId   = 0;  // Use (or callee)
    std::string var;
};

/** Criterion for a Weiser-style dynamic backward slice. */
struct EVENGINE_API SliceCriterion {
    SourceLoc                loc;         // error site (source+line preferred)
    std::vector<std::string> variables;   // empty ⇒ all vars live at site
    uint32_t                 eventId = 0; // optional exact seed event
};

struct EVENGINE_API SliceResult {
    std::vector<uint32_t>     eventIds;   // chronological subset of the slice
    std::vector<SourceLoc>    locations;  // unique source locations in slice
    std::vector<CallFrame>    callStack;  // stack at criterion
    std::vector<DataFlowEdge> dataFlow;   // edges contributing to the error
    std::string               summary;
};

/**
 * Runtime call graph + dynamic data-flow tracer with backward slicing.
 *
 * Feed events from a Squirrel debug hook (see DevTool) or manually in tests.
 * On script failure, use sliceBackward() to recover the statements and
 * definitions that influenced the error site — analogous to a dynamic slicer.
 *
 * Event storage is a ring buffer: when full, each new append drops exactly one
 * oldest event (monotonic ids; lookups ignore ids that have slid out).
 */
class EVENGINE_API CallGraph {
public:
    CallGraph();
    ~CallGraph();

    CallGraph(const CallGraph&)            = delete;
    CallGraph& operator=(const CallGraph&) = delete;

    void clear();
    void setMaxEvents(size_t n);
    size_t maxEvents() const { return maxEvents_; }
    size_t eventCount() const { return events_.size(); }

    // --- recording ---------------------------------------------------------
    uint32_t onCall(const SourceLoc& loc, const std::string& funcName = {});
    uint32_t onReturn(const SourceLoc& loc, const std::string& funcName = {});
    uint32_t onLine(const SourceLoc& loc);
    uint32_t onDef(const SourceLoc& loc, const std::string& var);
    uint32_t onUse(const SourceLoc& loc, const std::string& var);

    /** Convenience: Call then Line at the same site. */
    uint32_t enter(const SourceLoc& loc, const std::string& funcName);

    // --- queries -----------------------------------------------------------
    const std::deque<TraceEvent>& events() const { return events_; }
    const TraceEvent* event(uint32_t id) const;

    std::vector<CallFrame> currentStack() const;
    /** Stack reconstructed at (or just before) a given event. */
    std::vector<CallFrame> stackAt(uint32_t eventId) const;

    std::vector<std::pair<SourceLoc, SourceLoc>> callEdges() const;

    /**
     * Dynamic backward slice from an error criterion.
     * Follows data dependencies (Use←Def) and control predecessors
     * (line order + call/return nesting) until a fixed point.
     */
    SliceResult sliceBackward(const SliceCriterion& criterion) const;

    /** Human-readable report: message + call stack + data-flow + slice locs. */
    std::string formatErrorReport(const std::string& errorMessage,
                                  const SliceCriterion& criterion) const;

private:
    uint32_t append(TraceKind kind, const SourceLoc& loc, const std::string& name);
    void     linkData(uint32_t useEventId, const std::string& var);
    void     dropOldest();
    void     ensureCapacity();
    uint32_t findSeedEvent(const SliceCriterion& c) const;
    void     collectSeeds(const SliceCriterion& c, std::vector<uint32_t>& out) const;

    size_t maxEvents_ = 100000;

    std::deque<TraceEvent> events_;  // ring buffer window (oldest at front)
    std::vector<uint32_t>  frameStack_;  // active frame ids (bottom→top)
    uint32_t               nextFrameId_ = 1;
    uint32_t               nextEventId_ = 1;
    uint32_t               lastEventId_ = 0;

    // last Def event id per (frameId, var)
    std::unordered_map<uint32_t, std::unordered_map<std::string, uint32_t>> lastDef_;

    // Use/Call event → Def/Call events that feed it
    std::unordered_map<uint32_t, std::vector<uint32_t>> dataDeps_;

    // frameId → Call event that opened the activation
    std::unordered_map<uint32_t, uint32_t> frameToCallEvent_;
};

}  // namespace eve::dev
