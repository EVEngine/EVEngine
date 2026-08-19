#pragma once

#include "common/Export.h"

#include <cstdint>
#include <iterator>
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
 * Event storage is a fixed ring buffer: when full, the write cursor advances and
 * overwrites the oldest slot in place (O(1); monotonic ids).
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
    size_t eventCount() const { return count_; }

    // --- recording ---------------------------------------------------------
    uint32_t onCall(const SourceLoc& loc, const std::string& funcName = {});
    uint32_t onReturn(const SourceLoc& loc, const std::string& funcName = {});
    uint32_t onLine(const SourceLoc& loc);
    uint32_t onDef(const SourceLoc& loc, const std::string& var);
    uint32_t onUse(const SourceLoc& loc, const std::string& var);

    /** Convenience: Call then Line at the same site. */
    uint32_t enter(const SourceLoc& loc, const std::string& funcName);

    // --- queries -----------------------------------------------------------
    /** Chronological view over the live ring window (oldest → newest). */
    class EVENGINE_API EventsView {
    public:
        class EVENGINE_API const_iterator {
        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type        = TraceEvent;
            using difference_type   = std::ptrdiff_t;
            using pointer           = const TraceEvent*;
            using reference         = const TraceEvent&;

            const_iterator() = default;
            reference operator*() const { return (*g_)[idx_]; }
            pointer   operator->() const { return &(*g_)[idx_]; }
            const_iterator& operator++() {
                ++idx_;
                return *this;
            }
            const_iterator operator++(int) {
                const_iterator t = *this;
                ++(*this);
                return t;
            }
            bool operator==(const const_iterator& o) const {
                return g_ == o.g_ && idx_ == o.idx_;
            }
            bool operator!=(const const_iterator& o) const { return !(*this == o); }

        private:
            friend class EventsView;
            const_iterator(const CallGraph* g, size_t idx) : g_(g), idx_(idx) {}
            const CallGraph* g_   = nullptr;
            size_t           idx_ = 0;
        };

        explicit EventsView(const CallGraph* g) : g_(g) {}

        const_iterator begin() const { return const_iterator(g_, 0); }
        const_iterator end() const { return const_iterator(g_, g_ ? g_->count_ : 0); }
        bool   empty() const { return !g_ || g_->count_ == 0; }
        size_t size() const { return g_ ? g_->count_ : 0; }
        const TraceEvent& operator[](size_t i) const { return (*g_)[i]; }
        const TraceEvent& front() const { return (*g_)[0]; }
        const TraceEvent& back() const { return (*g_)[g_->count_ - 1]; }

    private:
        const CallGraph* g_ = nullptr;
    };

    EventsView events() const { return EventsView(this); }
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
    friend class EventsView;
    friend class EventsView::const_iterator;

    uint32_t append(TraceKind kind, const SourceLoc& loc, const std::string& name);
    void     linkData(uint32_t useEventId, const std::string& var);
    void     retireSlot(size_t physical);
    void     ensureRing();
    uint32_t findSeedEvent(const SliceCriterion& c) const;
    void     collectSeeds(const SliceCriterion& c, std::vector<uint32_t>& out) const;

    size_t physicalIndex(size_t chrono) const {
        return (head_ + chrono) % maxEvents_;
    }
    const TraceEvent& operator[](size_t chrono) const {
        return slots_[physicalIndex(chrono)];
    }
    TraceEvent& operator[](size_t chrono) { return slots_[physicalIndex(chrono)]; }
    TraceEvent& newest() { return (*this)[count_ - 1]; }

    size_t maxEvents_ = 100000;

    std::vector<TraceEvent> slots_;  // fixed capacity ring
    size_t                  head_  = 0;  // chronological oldest
    size_t                  count_ = 0;

    std::vector<uint32_t> frameStack_;  // active frame ids (bottom→top)
    uint32_t              nextFrameId_ = 1;
    uint32_t              nextEventId_ = 1;
    uint32_t              lastEventId_ = 0;

    // last Def event id per (frameId, var)
    std::unordered_map<uint32_t, std::unordered_map<std::string, uint32_t>> lastDef_;

    // Use/Call event → Def/Call events that feed it
    std::unordered_map<uint32_t, std::vector<uint32_t>> dataDeps_;

    // frameId → Call event that opened the activation
    std::unordered_map<uint32_t, uint32_t> frameToCallEvent_;
};

}  // namespace eve::dev
